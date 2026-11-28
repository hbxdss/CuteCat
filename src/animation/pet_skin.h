#pragma once

#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QtGlobal>

class QPainter;

// 桌宠皮肤接口。
// 窗口（PetWindow）只负责几何、鼠标交互、托盘和配置；
// 怎么把形象画出来（静态图片 + 程序化呼吸，或者 rlottie 逐帧动画）由皮肤自己决定。
class PetSkin
{
public:
    virtual ~PetSkin() = default;

    // asset 可以是 qrc 路径（:/pet/cat_idle.png）或磁盘绝对路径
    virtual bool load(const QString& asset, QString* error) = 0;

    virtual QSize nativeSize() const = 0;

    // dtSeconds 为两次 tick 的间隔，皮肤内部自己维护时间轴
    virtual void tick(double dtSeconds) = 0;

    virtual void paint(QPainter* painter, const QRectF& target, qreal opacity) = 0;

    virtual void triggerClick() {}
    virtual void triggerWave() {}
    virtual void setHovered(bool hovered) { Q_UNUSED(hovered) }
    virtual void setPressed(bool pressed) { Q_UNUSED(pressed) }
    virtual void setDragTilt(qreal degrees) { Q_UNUSED(degrees) }
    // 鼠标相对窗口中心的归一化位置（-1..1，越界会被皮肤自己裁剪）
    virtual void setPointerRelative(const QPointF& relative) { Q_UNUSED(relative) }
    // 有按键活动（打字），Live2D 皮肤会把“手”抬起来
    virtual void notifyKeyActivity() {}
    virtual QString displayName() const { return backendName(); }

    // 命中测试：默认整块区域都算命中，图片皮肤会按 alpha 判断
    virtual bool hitTest(const QPointF& position, const QRectF& target) const
    {
        return target.contains(position);
    }

    virtual QPixmap iconPixmap(int size) const
    {
        Q_UNUSED(size)
        return QPixmap();
    }

    virtual QString backendName() const = 0;
};