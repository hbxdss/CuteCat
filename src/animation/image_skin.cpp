#include "animation/image_skin.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <cmath>

namespace {
constexpr double kTau = 6.283185307179586;
constexpr double kBreathPeriod = 3.4; // 一次完整呼吸的秒数
constexpr double kSwayPeriod = 8.0;   // 待机左右轻摆的秒数

// 粉色小心心
QPainterPath heartPath(const QRectF& rect)
{
    QPainterPath path;
    const qreal w = rect.width();
    const qreal h = rect.height();
    path.moveTo(rect.center().x(), rect.bottom());
    path.cubicTo(rect.left() - w * 0.10, rect.top() + h * 0.45,
                 rect.left() + w * 0.25, rect.top() - h * 0.18,
                 rect.center().x(), rect.top() + h * 0.26);
    path.cubicTo(rect.right() - w * 0.25, rect.top() - h * 0.18,
                 rect.right() + w * 0.10, rect.top() + h * 0.45,
                 rect.center().x(), rect.bottom());
    path.closeSubpath();
    return path;
}

// 四角闪光
QPainterPath sparklePath(const QRectF& rect)
{
    QPainterPath path;
    const QPointF c = rect.center();
    const qreal rw = rect.width() * 0.5;
    const qreal rh = rect.height() * 0.5;
    path.moveTo(c.x(), c.y() - rh);
    path.quadTo(c.x() + rw * 0.18, c.y() - rh * 0.18, c.x() + rw, c.y());
    path.quadTo(c.x() + rw * 0.18, c.y() + rh * 0.18, c.x(), c.y() + rh);
    path.quadTo(c.x() - rw * 0.18, c.y() + rh * 0.18, c.x() - rw, c.y());
    path.quadTo(c.x() - rw * 0.18, c.y() - rh * 0.18, c.x(), c.y() - rh);
    path.closeSubpath();
    return path;
}
} // namespace

bool ImageSkin::load(const QString& asset, QString* error)
{
    if (asset.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有指定形象图片");
        }
        return false;
    }
    if (!asset.startsWith(QLatin1String(":/")) && !QFileInfo::exists(asset)) {
        if (error) {
            *error = QStringLiteral("找不到形象图片：%1").arg(asset);
        }
        return false;
    }

    QImage image(asset);
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("形象图片无法读取：%1").arg(asset);
        }
        return false;
    }

    image_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    alphaProbe_ = image_.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return true;
}

QSize ImageSkin::nativeSize() const
{
    return image_.size();
}

void ImageSkin::tick(double dtSeconds)
{
    elapsed_ += dtSeconds;

    // 指数平滑，保证状态切换顺滑（按下、悬停、拖拽回正）
    const qreal hoverStep = 1.0 - std::exp(-dtSeconds * 10.0);
    const qreal pressStep = 1.0 - std::exp(-dtSeconds * 16.0);
    const qreal tiltStep = 1.0 - std::exp(-dtSeconds * 7.0);

    hoverMix_ += ((hovered_ ? 1.0 : 0.0) - hoverMix_) * hoverStep;
    pressedMix_ += ((pressed_ ? 1.0 : 0.0) - pressedMix_) * pressStep;
    dragTilt_ += (dragTiltTarget_ - dragTilt_) * tiltStep;

    if (popTime_ >= 0.0) {
        popTime_ += dtSeconds;
        if (popTime_ > 1.2) {
            popTime_ = -1.0;
        }
    }

    for (int i = particles_.size() - 1; i >= 0; --i) {
        Particle& particle = particles_[i];
        particle.life += dtSeconds;
        particle.position += particle.velocity * dtSeconds;
        particle.velocity.setY(particle.velocity.y() + 26.0 * dtSeconds);
        particle.rotation += 40.0 * dtSeconds;
        if (particle.life >= particle.maxLife) {
            particles_.removeAt(i);
        }
    }
}

void ImageSkin::paint(QPainter* painter, const QRectF& target, qreal opacity)
{
    if (image_.isNull()) {
        return;
    }
    lastTarget_ = target;

    const double breath = std::sin(elapsed_ * kTau / kBreathPeriod);
    const double sway = std::sin(elapsed_ * kTau / kSwayPeriod) * 0.6;

    qreal pop = 0.0;
    if (popTime_ >= 0.0) {
        pop = 0.22 * std::exp(-4.5 * popTime_) * std::cos(10.0 * popTime_);
    }

    const qreal scaleX = 1.0 + breath * 0.010 + pressedMix_ * 0.060 - pop * 0.30 + hoverMix_ * 0.015;
    const qreal scaleY = 1.0 + breath * 0.018 - pressedMix_ * 0.070 + pop * 0.70 + hoverMix_ * 0.010;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 脚下的柔和阴影，随呼吸和挤压一起变化
    const qreal shadowWidth = target.width() * (0.52 + 0.02 * breath + pressedMix_ * 0.05);
    const QRectF shadowRect(target.center().x() - shadowWidth * 0.5,
                            target.bottom() - target.height() * 0.075,
                            shadowWidth,
                            target.height() * 0.070);
    QRadialGradient shadowGradient(shadowRect.center(), shadowWidth * 0.5);
    shadowGradient.setColorAt(0.0, QColor(70, 45, 32, 62));
    shadowGradient.setColorAt(0.65, QColor(70, 45, 32, 26));
    shadowGradient.setColorAt(1.0, QColor(70, 45, 32, 0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(shadowGradient);
    painter->drawEllipse(shadowRect);

    // 以“脚底中心”为锚点做缩放，呼吸时才不会整只猫上浮
    const QPointF anchor(target.center().x(), target.bottom());
    painter->translate(anchor);
    painter->rotate(sway + dragTilt_);
    painter->scale(scaleX, scaleY);
    painter->translate(-anchor);

    painter->setOpacity(opacity);
    painter->drawImage(target, image_);
    painter->restore();

    // 点击时的飘心与闪光
    if (particles_.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (const Particle& particle : particles_) {
        const qreal progress = particle.life / particle.maxLife;
        const qreal fade = 1.0 - progress;
        QColor color = particle.color;
        color.setAlphaF(color.alphaF() * fade);
        const QRectF rect(particle.position.x() - particle.size * 0.5,
                          particle.position.y() - particle.size * 0.5,
                          particle.size,
                          particle.size);
        if (particle.kind == Heart) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPath(heartPath(rect));
        } else {
            painter->setPen(QPen(color, qMax<qreal>(1.2, particle.size * 0.16),
                                 Qt::SolidLine, Qt::RoundCap));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(sparklePath(rect));
        }
    }
    painter->restore();
}

void ImageSkin::triggerClick()
{
    popTime_ = 0.0;
    spawnBurst();
}

void ImageSkin::spawnBurst()
{
    if (lastTarget_.isEmpty()) {
        return;
    }
    auto* rng = QRandomGenerator::global();
    const QPointF head(lastTarget_.center().x(), lastTarget_.top() + lastTarget_.height() * 0.16);

    for (int i = 0; i < 6; ++i) {
        Particle particle;
        particle.kind = Heart;
        particle.size = lastTarget_.width() * (0.050 + rng->generateDouble() * 0.030);
        particle.position = head + QPointF((rng->generateDouble() - 0.5) * lastTarget_.width() * 0.45,
                                           (rng->generateDouble() - 0.3) * 18.0);
        particle.velocity = QPointF((rng->generateDouble() - 0.5) * 90.0,
                                    -60.0 - rng->generateDouble() * 70.0);
        particle.maxLife = 0.9 + rng->generateDouble() * 0.6;
        particle.color = QColor::fromHsvF(float(0.945 + rng->generateDouble() * 0.055),
                                          0.55f, 1.0f);
        particles_.append(particle);
    }

    for (int i = 0; i < 7; ++i) {
        Particle particle;
        particle.kind = Sparkle;
        particle.size = lastTarget_.width() * (0.030 + rng->generateDouble() * 0.025);
        particle.position = head + QPointF((rng->generateDouble() - 0.5) * lastTarget_.width() * 0.75,
                                           (rng->generateDouble() - 0.4) * 30.0);
        particle.velocity = QPointF((rng->generateDouble() - 0.5) * 110.0,
                                    -40.0 - rng->generateDouble() * 80.0);
        particle.maxLife = 0.7 + rng->generateDouble() * 0.5;
        particle.color = QColor(255, 224, 130);
        particles_.append(particle);
    }
}

void ImageSkin::setHovered(bool hovered)
{
    hovered_ = hovered;
}

void ImageSkin::setPressed(bool pressed)
{
    pressed_ = pressed;
}

void ImageSkin::setDragTilt(qreal degrees)
{
    dragTiltTarget_ = qBound<qreal>(-8.0, degrees, 8.0);
}

bool ImageSkin::hitTest(const QPointF& position, const QRectF& target) const
{
    if (!target.contains(position) || alphaProbe_.isNull()) {
        return false;
    }
    const qreal relativeX = (position.x() - target.left()) / target.width();
    const qreal relativeY = (position.y() - target.top()) / target.height();
    const int x = qBound(0, int(relativeX * alphaProbe_.width()), alphaProbe_.width() - 1);
    const int y = qBound(0, int(relativeY * alphaProbe_.height()), alphaProbe_.height() - 1);
    return qAlpha(alphaProbe_.pixel(x, y)) > 12;
}

QPixmap ImageSkin::iconPixmap(int size) const
{
    if (image_.isNull()) {
        return QPixmap();
    }
    return QPixmap::fromImage(image_.scaled(size, size, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
}