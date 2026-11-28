#pragma once

#include "animation/pet_skin.h"

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QVector>

#include "animation/live2d_model.h"
#include "animation/live2d_renderer.h"

// Live2D 皮肤：直接吃 BongoCat（以及任何 Cubism 3/4/5）的模型目录。
//
// 做法：模型参数由我们自己驱动（呼吸、眨眼、鼠标跟随、点击、按键），
// 每帧用离屏 OpenGL 把网格画成 QImage，再由窗口贴到桌面上。
class Live2DSkin : public PetSkin
{
public:
    Live2DSkin();
    ~Live2DSkin() override;

    bool load(const QString& asset, QString* error) override;
    QSize nativeSize() const override;
    void tick(double dtSeconds) override;
    void paint(QPainter* painter, const QRectF& target, qreal opacity) override;

    void triggerClick() override;
    void setHovered(bool hovered) override;
    void setPressed(bool pressed) override;
    void setDragTilt(qreal degrees) override;
    void setPointerRelative(const QPointF& relative) override;
    void notifyKeyActivity() override;
    void triggerWave();

    bool hitTest(const QPointF& position, const QRectF& target) const override;
    QPixmap iconPixmap(int size) const override;
    QString backendName() const override { return QStringLiteral("live2d"); }
    QString displayName() const override;

private:
    void applyParameters();
    void renderFrame(bool force);
    void refreshHitMask();

    Live2DModel model_;
    Live2DRenderer renderer_;
    QImage frame_;
    QImage hitMask_;
    QRectF lastTarget_;
    QSize renderSize_{320, 380};

    double elapsed_ = 0.0;
    double sinceRender_ = 0.0;
    double blinkTimer_ = 2.4;
    double blinkPhase_ = -1.0;
    double clickPulse_ = -1.0;
    double wavePulse_ = -1.0;
    double typingEnergy_ = 0.0;
    double hoverMix_ = 0.0;
    double pressedMix_ = 0.0;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal dragTilt_ = 0.0;
    qreal dragTiltTarget_ = 0.0;
    QPointF pointer_;

    // 参数下标缓存（-1 表示模型没有这个参数）
    int paramBreath_ = -1;
    int paramEyeLeft_ = -1;
    int paramEyeRight_ = -1;
    int paramAngleX_ = -1;
    int paramAngleY_ = -1;
    int paramAngleZ_ = -1;
    int paramBodyX_ = -1;
    int paramBodyZ_ = -1;
    int paramEyeBallX_ = -1;
    int paramEyeBallY_ = -1;
    int paramMouthOpen_ = -1;
    int paramMouthForm_ = -1;
    int paramSmileLeft_ = -1;
    int paramSmileRight_ = -1;
    int paramMouseLeft_ = -1;
    QVector<int> paramHands_;
};