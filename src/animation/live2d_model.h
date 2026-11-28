#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

// PurismCore（MIT）提供的是 Cubism Core 的 C 接口，这里只做类型前置声明，
// 具体头文件只在 .cpp 里引入，避免把第三方头文件带进整个工程。
struct csmMoc;
struct csmModel;

// 一个 Live2D（Cubism 3/4/5）模型包。
// 负责：解析 .model3.json → 载入 moc3 → 载入贴图 → 暴露参数与网格数据。
//
// 说明：BongoCat 的“模型”就是标准的 Live2D Cubism 目录（.model3.json + .moc3 + 贴图），
// 所以能加载它的模型目录，也能加载任何同格式的模型。
class Live2DModel
{
public:
    Live2DModel();
    ~Live2DModel();

    Live2DModel(const Live2DModel&) = delete;
    Live2DModel& operator=(const Live2DModel&) = delete;

    // modelJson 指向某个 *.model3.json
    bool load(const QString& modelJson, QString* error);
    void unload();

    // 参数改动后调用，重新计算顶点
    void update();

    int parameterCount() const;
    QString parameterId(int index) const;
    int findParameter(const QString& id) const;
    bool setParameter(int index, float value);
    float parameterValue(int index) const;
    float parameterDefault(int index) const;
    float parameterMin(int index) const;
    float parameterMax(int index) const;

    // 当前姿态的包围盒（模型坐标，y 轴向上）
    QRectF poseBounds() const;
    QSizeF canvasSize() const;
    // 画布在模型坐标系里的矩形（Live2D 官方推荐的取景范围）
    QRectF canvasRect() const;

    csmModel* core() const { return model_; }
    const QVector<QImage>& textures() const { return textures_; }

    QString displayName() const { return displayName_; }
    QString modelFile() const { return modelFile_; }

    bool isValid() const { return model_ != nullptr; }

private:
    void release();

    csmMoc* moc_ = nullptr;
    csmModel* model_ = nullptr;
    void* mocMemory_ = nullptr;   // 64 字节对齐，moc 生命周期要长于 model
    void* modelMemory_ = nullptr; // 16 字节对齐
    QVector<QImage> textures_;
    QString modelFile_;
    QString displayName_;
    QSizeF canvasSize_;
    QPointF canvasOrigin_;
    float pixelsPerUnit_ = 1.0f;
};