#pragma once

#include "animation/pet_skin.h"

#include <QImage>
#include <QVector>

#if CUTECAT_HAS_RLOTTIE
#include <memory>
namespace rlottie {
class Animation;
}
#endif

// rlottie 皮肤：把 Lottie（.json）动画逐帧渲染成 QImage 再贴到窗口上。
// 默认构建不带 rlottie（见 CMakeLists 的 CUTECAT_WITH_RLOTTIE），
// 此时 load() 会返回失败，工厂会自动退回图片皮肤，桌宠不会启动不了。
class LottieSkin : public PetSkin
{
public:
    LottieSkin();
    ~LottieSkin() override;

    static bool available();

    bool load(const QString& asset, QString* error) override;
    QSize nativeSize() const override;
    void tick(double dtSeconds) override;
    void paint(QPainter* painter, const QRectF& target, qreal opacity) override;
    QPixmap iconPixmap(int size) const override;
    QString backendName() const override { return QStringLiteral("lottie"); }

private:
    QImage frameAt(int index) const;

    QSize size_;
#if CUTECAT_HAS_RLOTTIE
    std::shared_ptr<rlottie::Animation> animation_;
    int totalFrames_ = 0;
    double frameRate_ = 30.0;
    double elapsed_ = 0.0;
    mutable QVector<QImage> frameCache_;
#endif
};