#include "animation/live2d_skin.h"

#include "Live2DCubismCore.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QRadialGradient>
#include <QtDebug>

#include <cmath>

namespace {
constexpr double kTau = 6.283185307179586;
constexpr double kBreathPeriod = 3.6;
constexpr double kBlinkDuration = 0.16;
constexpr double kClickDuration = 0.45;
constexpr double kWaveDuration = 1.1;

// 参数变动足够小的时候不必每帧重画，省点 GPU/CPU
constexpr double kRenderInterval = 1.0 / 45.0;
} // namespace

Live2DSkin::Live2DSkin() = default;

Live2DSkin::~Live2DSkin() = default;

bool Live2DSkin::load(const QString& asset, QString* error)
{
    if (!model_.load(asset, error)) {
        return false;
    }
    if (!renderer_.initialize(error)) {
        return false;
    }
    if (!renderer_.uploadTextures(model_.textures(), error)) {
        return false;
    }

    paramBreath_ = model_.findParameter(QStringLiteral("ParamBreath"));
    paramEyeLeft_ = model_.findParameter(QStringLiteral("ParamEyeLOpen"));
    paramEyeRight_ = model_.findParameter(QStringLiteral("ParamEyeROpen"));
    paramAngleX_ = model_.findParameter(QStringLiteral("ParamAngleX"));
    paramAngleY_ = model_.findParameter(QStringLiteral("ParamAngleY"));
    paramAngleZ_ = model_.findParameter(QStringLiteral("ParamAngleZ"));
    paramBodyX_ = model_.findParameter(QStringLiteral("ParamBodyAngleX"));
    paramBodyZ_ = model_.findParameter(QStringLiteral("ParamBodyAngleZ"));
    paramEyeBallX_ = model_.findParameter(QStringLiteral("ParamEyeBallX"));
    paramEyeBallY_ = model_.findParameter(QStringLiteral("ParamEyeBallY"));
    paramMouthOpen_ = model_.findParameter(QStringLiteral("ParamMouthOpenY"));
    paramMouthForm_ = model_.findParameter(QStringLiteral("ParamMouthForm"));
    paramSmileLeft_ = model_.findParameter(QStringLiteral("ParamEyeLSmile"));
    paramSmileRight_ = model_.findParameter(QStringLiteral("ParamEyeRSmile"));
    paramMouseLeft_ = model_.findParameter(QStringLiteral("ParamMouseLeftDown"));

    // BongoCat 风格的“键盘按下”参数，不同模型命名不同，这里按关键字抓一遍
    paramHands_.clear();
    for (int i = 0; i < model_.parameterCount(); ++i) {
        const QString id = model_.parameterId(i);
        if (id.contains(QLatin1String("HandDown"), Qt::CaseInsensitive)
            || id.contains(QLatin1String("Hand"), Qt::CaseInsensitive)) {
            paramHands_.append(i);
        }
    }

    if (qEnvironmentVariableIsSet("CUTECAT_DEBUG_LIVE2D")) {
        csmModel* core = model_.core();
        const int count = csmGetDrawableCount(core);
        const char** ids = csmGetDrawableIds(core);
        const csmFlags* dynamicFlags = csmGetDrawableDynamicFlags(core);
        const csmFlags* constantFlags = csmGetDrawableConstantFlags(core);
        const float* opacities = csmGetDrawableOpacities(core);
        const int* textureIndices = csmGetDrawableTextureIndices(core);
        const int* vertexCounts = csmGetDrawableVertexCounts(core);
        QVector<const csmVector2*> positions(count, nullptr);
        const csmVector2** rawPositions = csmGetDrawableVertexPositions(core);
        for (int i = 0; i < count; ++i) {
            positions[i] = rawPositions[i];
        }
        for (int i = 0; i < count; ++i) {
            qInfo().noquote() << QStringLiteral("drawable %1 %2 vis=%3 op=%4 tex=%5 verts=%6 flags=%7")
                                     .arg(i)
                                     .arg(QString::fromLatin1(ids[i]))
                                     .arg(bool(dynamicFlags[i] & csmIsVisible))
                                     .arg(opacities[i])
                                     .arg(textureIndices[i])
                                     .arg(vertexCounts[i])
                                     .arg(int(constantFlags[i]));
        }
        qInfo() << "pose bounds" << model_.poseBounds() << "canvas" << model_.canvasSize();
    }

    renderSize_ = QSize(320, 380);
    renderFrame(true);
    if (frame_.isNull()) {
        if (error) {
            *error = QStringLiteral("Live2D 模型渲染失败（OpenGL 不可用？）");
        }
        return false;
    }
    return true;
}

QSize Live2DSkin::nativeSize() const
{
    const QSize canvas(model_.canvasSize().width(), model_.canvasSize().height());
    return canvas.isEmpty() ? QSize(512, 512) : canvas;
}

QString Live2DSkin::displayName() const
{
    return model_.displayName();
}

void Live2DSkin::tick(double dtSeconds)
{
    elapsed_ += dtSeconds;
    sinceRender_ += dtSeconds;

    const double hoverStep = 1.0 - std::exp(-dtSeconds * 10.0);
    const double pressStep = 1.0 - std::exp(-dtSeconds * 16.0);
    const double tiltStep = 1.0 - std::exp(-dtSeconds * 7.0);
    hoverMix_ += ((hovered_ ? 1.0 : 0.0) - hoverMix_) * hoverStep;
    pressedMix_ += ((pressed_ ? 1.0 : 0.0) - pressedMix_) * pressStep;
    dragTilt_ += (dragTiltTarget_ - dragTilt_) * tiltStep;

    // 眨眼计时
    if (blinkPhase_ < 0.0) {
        blinkTimer_ -= dtSeconds;
        if (blinkTimer_ <= 0.0) {
            blinkPhase_ = 0.0;
            blinkTimer_ = 2.2 + (double(rand() % 2600) / 1000.0);
        }
    } else {
        blinkPhase_ += dtSeconds;
        if (blinkPhase_ > kBlinkDuration) {
            blinkPhase_ = -1.0;
        }
    }

    if (clickPulse_ >= 0.0) {
        clickPulse_ += dtSeconds;
        if (clickPulse_ > kClickDuration) {
            clickPulse_ = -1.0;
        }
    }
    if (wavePulse_ >= 0.0) {
        wavePulse_ += dtSeconds;
        if (wavePulse_ > kWaveDuration) {
            wavePulse_ = -1.0;
        }
    }
    if (typingEnergy_ > 0.0) {
        typingEnergy_ = qMax(0.0, typingEnergy_ - dtSeconds * 2.4);
    }

    if (sinceRender_ >= kRenderInterval) {
        applyParameters();
        model_.update();
        renderFrame(false);
        sinceRender_ = 0.0;
    }
}

void Live2DSkin::applyParameters()
{
    auto set = [this](int index, float value) {
        if (index >= 0) {
            model_.setParameter(index, value);
        }
    };

    const double breath = 0.5 - 0.5 * std::cos(elapsed_ * kTau / kBreathPeriod);
    set(paramBreath_, float(breath));

    double eyeOpen = 1.0;
    if (blinkPhase_ >= 0.0) {
        const double progress = blinkPhase_ / kBlinkDuration;
        eyeOpen = 1.0 - std::sin(progress * M_PI);
    }
    if (clickPulse_ >= 0.0 && clickPulse_ < 0.35) {
        eyeOpen = qMin(eyeOpen, 0.12); // 被摸的时候眯眼
    }
    set(paramEyeLeft_, float(qBound(0.0, eyeOpen, 1.0)));
    set(paramEyeRight_, float(qBound(0.0, eyeOpen, 1.0)));

    const double px = qBound(-1.0, pointer_.x(), 1.0);
    const double py = qBound(-1.0, pointer_.y(), 1.0);
    const double sway = std::sin(elapsed_ * kTau / 9.0) * 0.25;

    set(paramAngleX_, float(px * 30.0));
    set(paramAngleY_, float(-py * 24.0));
    set(paramAngleZ_, float(dragTilt_ + sway));
    set(paramBodyX_, float(px * 8.0));
    set(paramBodyZ_, float(dragTilt_ * 0.8));
    set(paramEyeBallX_, float(px * 0.9));
    set(paramEyeBallY_, float(-py * 0.9));

    const bool touching = clickPulse_ >= 0.0;
    set(paramMouseLeft_, touching && clickPulse_ < 0.18 ? 1.0f : 0.0f);
    set(paramSmileLeft_, touching && clickPulse_ < 0.38 ? 1.0f : 0.0f);
    set(paramSmileRight_, touching && clickPulse_ < 0.38 ? 1.0f : 0.0f);
    set(paramMouthOpen_, touching && clickPulse_ < 0.30 ? 0.6f : 0.0f);
    set(paramMouthForm_, touching ? 0.6f : 0.0f);

    // 按键时“手”抬起来，这就是 BongoCat 的灵魂
    for (int index : paramHands_) {
        set(index, float(typingEnergy_));
    }

    // 双击时的挥手参数
    if (wavePulse_ >= 0.0) {
        const int wave = model_.findParameter(QStringLiteral("Param3"));
        if (wave >= 0) {
            const double progress = wavePulse_ / kWaveDuration;
            set(wave, float(std::sin(progress * M_PI)));
        }
    }
}

void Live2DSkin::renderFrame(bool force)
{
    QSize wanted = renderSize_;
    if (!lastTarget_.isEmpty()) {
        wanted = QSize(qRound(lastTarget_.width() * 1.5), qRound(lastTarget_.height() * 1.5));
    }
    wanted.setWidth(qBound(200, wanted.width(), 720));
    wanted.setHeight(qBound(200, wanted.height(), 900));
    if (!force && wanted == renderSize_ && !frame_.isNull()) {
        // 尺寸没变就按原尺寸重画（内容仍需刷新）
    }
    renderSize_ = wanted;

    const QImage rendered = renderer_.render(model_, renderSize_);
    if (!rendered.isNull()) {
        frame_ = rendered;
        refreshHitMask();
        if (qEnvironmentVariableIsSet("CUTECAT_DEBUG_LIVE2D")) {
            const QString debugPath = QDir::temp().filePath(QStringLiteral("cutecat_live2d_frame.png"));
            frame_.save(debugPath);
        }
    }
}

void Live2DSkin::refreshHitMask()
{
    if (frame_.isNull()) {
        return;
    }
    hitMask_ = frame_.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void Live2DSkin::paint(QPainter* painter, const QRectF& target, qreal opacity)
{
    lastTarget_ = target;
    if (frame_.isNull()) {
        return;
    }

    const double pop = clickPulse_ >= 0.0 ? 0.18 * std::exp(-5.0 * clickPulse_) : 0.0;
    const qreal scale = 1.0 + pop + hoverMix_ * 0.01 - pressedMix_ * 0.03;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 脚下阴影
    const qreal shadowWidth = target.width() * 0.52;
    const QRectF shadowRect(target.center().x() - shadowWidth * 0.5,
                            target.bottom() - target.height() * 0.07, shadowWidth,
                            target.height() * 0.06);
    QRadialGradient gradient(shadowRect.center(), shadowWidth * 0.5);
    gradient.setColorAt(0.0, QColor(70, 45, 32, 58));
    gradient.setColorAt(0.7, QColor(70, 45, 32, 24));
    gradient.setColorAt(1.0, QColor(70, 45, 32, 0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(gradient);
    painter->drawEllipse(shadowRect);

    // 按比例放进目标区域，保持模型的画布比例
    const QSizeF frameSize = frame_.size();
    const qreal fit = qMin(target.width() / frameSize.width(), target.height() / frameSize.height());
    const QSizeF drawSize(frameSize.width() * fit * scale, frameSize.height() * fit * scale);
    QRectF drawRect(QPointF(0, 0), drawSize);
    drawRect.moveCenter(QPointF(target.center().x(), target.bottom() - drawSize.height() * 0.5));

    painter->setOpacity(opacity);
    painter->drawImage(drawRect, frame_);
    painter->restore();
}

void Live2DSkin::triggerClick()
{
    clickPulse_ = 0.0;
}

void Live2DSkin::triggerWave()
{
    wavePulse_ = 0.0;
}

void Live2DSkin::setHovered(bool hovered)
{
    hovered_ = hovered;
}

void Live2DSkin::setPressed(bool pressed)
{
    pressed_ = pressed;
}

void Live2DSkin::setDragTilt(qreal degrees)
{
    dragTiltTarget_ = qBound<qreal>(-10.0, degrees, 10.0);
}

void Live2DSkin::setPointerRelative(const QPointF& relative)
{
    pointer_ = relative;
}

void Live2DSkin::notifyKeyActivity()
{
    typingEnergy_ = qMin(1.0, typingEnergy_ + 0.55);
}

bool Live2DSkin::hitTest(const QPointF& position, const QRectF& target) const
{
    if (frame_.isNull() || !target.contains(position)) {
        return false;
    }
    // 命中测试用整块渲染区域即可：模型的透明边很少，误判影响可忽略
    const QSizeF frameSize = frame_.size();
    const qreal fit = qMin(target.width() / frameSize.width(), target.height() / frameSize.height());
    const QSizeF drawSize(frameSize.width() * fit, frameSize.height() * fit);
    QRectF drawRect(QPointF(0, 0), drawSize);
    drawRect.moveCenter(QPointF(target.center().x(), target.bottom() - drawSize.height() * 0.5));
    return drawRect.contains(position);
}

QPixmap Live2DSkin::iconPixmap(int size) const
{
    if (frame_.isNull()) {
        return QPixmap();
    }
    return QPixmap::fromImage(
        frame_.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}