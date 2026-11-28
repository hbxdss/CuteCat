#include "animation/live2d_model.h"

#include "Live2DCubismCore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtDebug>

#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace {
// moc3 要求 64 字节对齐，模型实例要求 16 字节对齐
void* alignedAllocate(size_t size, size_t alignment)
{
#ifdef _MSC_VER
    return _aligned_malloc(size, alignment);
#else
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, size) != 0) {
        return nullptr;
    }
    return pointer;
#endif
}

void alignedFree(void* pointer)
{
#ifdef _MSC_VER
    _aligned_free(pointer);
#else
    free(pointer);
#endif
}
} // namespace

Live2DModel::Live2DModel() = default;

Live2DModel::~Live2DModel()
{
    unload();
}

void Live2DModel::release()
{
    // 先释放模型，再释放 moc（model 内部引用 moc 内存）
    if (modelMemory_) {
        alignedFree(modelMemory_);
        modelMemory_ = nullptr;
    }
    model_ = nullptr;
    moc_ = nullptr;
    if (mocMemory_) {
        alignedFree(mocMemory_);
        mocMemory_ = nullptr;
    }
}

void Live2DModel::unload()
{
    release();
    textures_.clear();
    modelFile_.clear();
    displayName_.clear();
    canvasSize_ = QSizeF();
}

bool Live2DModel::load(const QString& modelJson, QString* error)
{
    unload();

    const QFileInfo modelInfo(modelJson);
    if (!modelInfo.exists()) {
        if (error) {
            *error = QStringLiteral("找不到模型描述文件：%1").arg(modelJson);
        }
        return false;
    }

    QFile jsonFile(modelJson);
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("模型描述文件打不开：%1").arg(modelJson);
        }
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("模型描述文件解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject references = root.value(QStringLiteral("FileReferences")).toObject();
    const QString mocName = references.value(QStringLiteral("Moc")).toString();
    if (mocName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("模型描述里没有 Moc 字段");
        }
        return false;
    }

    const QDir baseDir = modelInfo.absoluteDir();
    const QString mocPath = baseDir.filePath(mocName);

    QFile mocFile(mocPath);
    if (!mocFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("moc3 打不开：%1").arg(mocPath);
        }
        return false;
    }
    const QByteArray mocData = mocFile.readAll();
    mocFile.close();
    if (mocData.isEmpty()) {
        if (error) {
            *error = QStringLiteral("moc3 是空文件：%1").arg(mocPath);
        }
        return false;
    }

    mocMemory_ = alignedAllocate(size_t(mocData.size()), csmAlignofMoc);
    if (!mocMemory_) {
        if (error) {
            *error = QStringLiteral("moc3 内存分配失败");
        }
        return false;
    }
    std::memcpy(mocMemory_, mocData.constData(), size_t(mocData.size()));

    moc_ = csmReviveMocInPlace(mocMemory_, unsigned(mocData.size()));
    if (!moc_) {
        if (error) {
            *error = QStringLiteral("moc3 解析失败：%1")
                         .arg(QString::fromLatin1(csmGetErrorString(csmGetMocError(moc_))));
        }
        release();
        return false;
    }

    const unsigned modelSize = csmGetSizeofModel(moc_);
    if (modelSize == 0) {
        if (error) {
            *error = QStringLiteral("无法计算模型内存大小");
        }
        release();
        return false;
    }
    modelMemory_ = alignedAllocate(modelSize, csmAlignofModel);
    if (!modelMemory_) {
        if (error) {
            *error = QStringLiteral("模型内存分配失败");
        }
        release();
        return false;
    }

    model_ = csmInitializeModelInPlace(moc_, modelMemory_, modelSize);
    if (!model_) {
        if (error) {
            *error = QStringLiteral("模型初始化失败");
        }
        release();
        return false;
    }
    csmUpdateModel(model_);

    csmVector2 canvasPixels{};
    csmVector2 canvasOrigin{};
    float pixelsPerUnit = 1.0f;
    csmReadCanvasInfo(model_, &canvasPixels, &canvasOrigin, &pixelsPerUnit);
    canvasSize_ = QSizeF(canvasPixels.X, canvasPixels.Y);
    canvasOrigin_ = QPointF(canvasOrigin.X, canvasOrigin.Y);
    pixelsPerUnit_ = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;

    // 贴图
    const QJsonArray textureArray = references.value(QStringLiteral("Textures")).toArray();
    for (const QJsonValue& value : textureArray) {
        const QString relative = value.toString();
        const QString path = baseDir.filePath(relative);
        QImage image(path);
        if (image.isNull()) {
            qInfo().noquote() << "Live2D 贴图读取失败：" << path;
            continue;
        }
        textures_.append(image.convertToFormat(QImage::Format_RGBA8888));
    }
    if (textures_.isEmpty()) {
        if (error) {
            *error = QStringLiteral("模型没有可用的贴图");
        }
        release();
        return false;
    }

    modelFile_ = modelJson;
    displayName_ = baseDir.dirName();
    if (displayName_.isEmpty()) {
        displayName_ = modelInfo.completeBaseName();
    }
    qInfo().noquote() << "Live2D 模型已载入：" << displayName_ << "参数" << parameterCount()
                      << "贴图" << textures_.size();
    return true;
}

void Live2DModel::update()
{
    if (model_) {
        csmUpdateModel(model_);
    }
}

int Live2DModel::parameterCount() const
{
    return model_ ? csmGetParameterCount(model_) : 0;
}

QString Live2DModel::parameterId(int index) const
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return QString();
    }
    const char** ids = csmGetParameterIds(model_);
    return QString::fromLatin1(ids[index]);
}

int Live2DModel::findParameter(const QString& id) const
{
    const QByteArray key = id.toLatin1();
    for (int i = 0; i < parameterCount(); ++i) {
        if (key == parameterId(i).toLatin1()) {
            return i;
        }
    }
    return -1;
}

bool Live2DModel::setParameter(int index, float value)
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return false;
    }
    float* values = csmGetParameterValues(model_);
    const float* minimum = csmGetParameterMinimumValues(model_);
    const float* maximum = csmGetParameterMaximumValues(model_);
    values[index] = qBound(minimum[index], value, maximum[index]);
    return true;
}

float Live2DModel::parameterValue(int index) const
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return 0.0f;
    }
    return csmGetParameterValues(model_)[index];
}

float Live2DModel::parameterDefault(int index) const
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return 0.0f;
    }
    return csmGetParameterDefaultValues(model_)[index];
}

float Live2DModel::parameterMin(int index) const
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return 0.0f;
    }
    return csmGetParameterMinimumValues(model_)[index];
}

float Live2DModel::parameterMax(int index) const
{
    if (!model_ || index < 0 || index >= parameterCount()) {
        return 0.0f;
    }
    return csmGetParameterMaximumValues(model_)[index];
}

QSizeF Live2DModel::canvasSize() const
{
    return canvasSize_;
}

QRectF Live2DModel::canvasRect() const
{
    if (canvasSize_.isEmpty() || pixelsPerUnit_ <= 0.0f) {
        return poseBounds();
    }
    // 像素 = 原点 + 模型坐标 * ppu，所以模型坐标 = (像素 - 原点) / ppu
    const qreal width = canvasSize_.width() / pixelsPerUnit_;
    const qreal height = canvasSize_.height() / pixelsPerUnit_;
    return QRectF(-canvasOrigin_.x() / pixelsPerUnit_,
                  -canvasOrigin_.y() / pixelsPerUnit_, width, height);
}

QRectF Live2DModel::poseBounds() const
{
    if (!model_) {
        return QRectF();
    }
    const int count = csmGetDrawableCount(model_);
    const csmVector2** positions = csmGetDrawableVertexPositions(model_);
    const int* vertexCounts = csmGetDrawableVertexCounts(model_);
    const csmFlags* dynamicFlags = csmGetDrawableDynamicFlags(model_);
    const float* opacities = csmGetDrawableOpacities(model_);

    qreal minX = 1e30;
    qreal minY = 1e30;
    qreal maxX = -1e30;
    qreal maxY = -1e30;
    for (int d = 0; d < count; ++d) {
        // 只把真正看得见的部件算进包围盒：隐藏的特效网格会把画布撑大，
        // 导致小猫只占画面一小块
        if (!(dynamicFlags[d] & csmIsVisible) || !positions[d] || opacities[d] <= 0.01f) {
            continue;
        }
        for (int i = 0; i < vertexCounts[d]; ++i) {
            const qreal x = positions[d][i].X;
            const qreal y = positions[d][i].Y;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX <= minX || maxY <= minY) {
        return QRectF(-1.0, -1.0, 2.0, 2.0);
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}