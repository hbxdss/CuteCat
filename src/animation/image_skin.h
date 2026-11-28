#pragma once

#include "animation/pet_skin.h"

#include <QImage>
#include <QVector>

// 图片皮肤：把一张带透明通道的立绘当作桌宠本体，
// 用程序化的呼吸、轻摆、按下挤压、点击弹跳和飘心粒子制造“活着”的感觉。
// 美术资源换新图只要替换 resources/pet/cat_idle.png，无需改代码。
class ImageSkin : public PetSkin
{
public:
    ImageSkin() = default;
    ~ImageSkin() override = default;

    bool load(const QString& asset, QString* error) override;
    QSize nativeSize() const override;
    void tick(double dtSeconds) override;
    void paint(QPainter* painter, const QRectF& target, qreal opacity) override;

    void triggerClick() override;
    void setHovered(bool hovered) override;
    void setPressed(bool pressed) override;
    void setDragTilt(qreal degrees) override;

    bool hitTest(const QPointF& position, const QRectF& target) const override;
    QPixmap iconPixmap(int size) const override;
    QString backendName() const override { return QStringLiteral("image"); }

private:
    enum ParticleKind { Heart = 0, Sparkle = 1 };

    struct Particle
    {
        QPointF position;
        QPointF velocity;
        qreal size = 6.0;
        qreal life = 0.0;
        qreal maxLife = 1.0;
        qreal rotation = 0.0;
        ParticleKind kind = Heart;
        QColor color;
    };

    void spawnBurst();

    QImage image_;
    QImage alphaProbe_; // 小尺寸副本，只用于 alpha 命中测试
    QRectF lastTarget_;

    double elapsed_ = 0.0;
    qreal hoverMix_ = 0.0;
    qreal pressedMix_ = 0.0;
    qreal dragTilt_ = 0.0;
    qreal dragTiltTarget_ = 0.0;
    double popTime_ = -1.0;
    bool hovered_ = false;
    bool pressed_ = false;
    QVector<Particle> particles_;
};