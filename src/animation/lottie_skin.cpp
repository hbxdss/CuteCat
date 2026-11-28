#include "animation/lottie_skin.h"

#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QtDebug>

#if CUTECAT_HAS_RLOTTIE
#include <rlottie.h>
#include <QPainter>
#endif

LottieSkin::LottieSkin() = default;
LottieSkin::~LottieSkin() = default;

bool LottieSkin::available()
{
#if CUTECAT_HAS_RLOTTIE
    return true;
#else
    return false;
#endif
}

bool LottieSkin::load(const QString& asset, QString* error)
{
#if CUTECAT_HAS_RLOTTIE
    if (asset.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有指定 Lottie 动画文件");
        }
        return false;
    }
    if (!asset.startsWith(QLatin1String(":/")) && !QFileInfo::exists(asset)) {
        if (error) {
            *error = QStringLiteral("找不到 Lottie 文件：%1").arg(asset);
        }
        return false;
    }

    // rlottie 需要真实文件路径，qrc 资源要先导出到临时文件
    QString path = asset;
    if (asset.startsWith(QLatin1String(":/"))) {
        path = QFileInfo(asset).absoluteFilePath();
    }

    animation_ = rlottie::Animation::loadFromFile(path.toStdString());
    if (!animation_) {
        if (error) {
            *error = QStringLiteral("rlottie 无法解析动画：%1").arg(asset);
        }
        return false;
    }

    totalFrames_ = int(animation_->totalFrame());
    frameRate_ = double(animation_->frameRate());
    if (totalFrames_ <= 0 || frameRate_ <= 0.0) {
        if (error) {
            *error = QStringLiteral("Lottie 动画没有可用帧：%1").arg(asset);
        }
        return false;
    }

    size_ = QSize(int(animation_->width()), int(animation_->height()));
    frameCache_.clear();
    return true;
#else
    Q_UNUSED(asset)
    if (error) {
        *error = QStringLiteral("当前构建未启用 rlottie，无法播放 Lottie 动画"
                                "（见 README 的“接入 rlottie”一节）");
    }
    return false;
#endif
}

QSize LottieSkin::nativeSize() const
{
    return size_;
}

void LottieSkin::tick(double dtSeconds)
{
#if CUTECAT_HAS_RLOTTIE
    elapsed_ += dtSeconds;
#endif
    Q_UNUSED(dtSeconds)
}

QImage LottieSkin::frameAt(int index) const
{
#if CUTECAT_HAS_RLOTTIE
    if (!animation_ || index < 0 || index >= totalFrames_) {
        return QImage();
    }
    if (frameCache_.size() != totalFrames_) {
        frameCache_.resize(totalFrames_);
    }
    if (!frameCache_.at(index).isNull()) {
        return frameCache_.at(index);
    }

    const int width = qMax(1, size_.width());
    const int height = qMax(1, size_.height());
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    // rlottie 的 Surface 直接写 ARGB32 预乘像素，和 Qt 的格式一致
    rlottie::Surface surface(reinterpret_cast<uint32_t*>(image.bits()),
                             uint32_t(width), uint32_t(height),
                             uint32_t(image.bytesPerLine()));
    animation_->render(static_cast<size_t>(index), surface);
    frameCache_[index] = image;
    return image;
#else
    Q_UNUSED(index)
    return QImage();
#endif
}

void LottieSkin::paint(QPainter* painter, const QRectF& target, qreal opacity)
{
#if CUTECAT_HAS_RLOTTIE
    if (!animation_) {
        return;
    }
    const double frame = std::fmod(elapsed_ * frameRate_, double(totalFrames_));
    const int index = qBound(0, int(frame), totalFrames_ - 1);
    const QImage image = frameAt(index);
    if (image.isNull()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setOpacity(opacity);
    painter->drawImage(target, image);
    painter->restore();
#else
    Q_UNUSED(painter)
    Q_UNUSED(target)
    Q_UNUSED(opacity)
#endif
}

QPixmap LottieSkin::iconPixmap(int size) const
{
    const QImage image = frameAt(0);
    if (image.isNull()) {
        return QPixmap();
    }
    return QPixmap::fromImage(image.scaled(size, size, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
}