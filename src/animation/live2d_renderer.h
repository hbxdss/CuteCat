#pragma once

#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QSize>
#include <QString>
#include <QVector>

class QOpenGLFramebufferObject;
class QOpenGLTexture;
class Live2DModel;
struct csmModel;

// 离屏 OpenGL 渲染器：把 Live2D 模型的三角形网格画进带 alpha 的 FBO，
// 再读回 QImage 交给窗口合成。
//
// 支持 Cubism 的裁剪遮罩（clipping mask）：被遮罩的部件先把它引用的 mask
// 网格画进一张单独的遮罩缓冲，再在主渲染里按遮罩采样裁剪 alpha。
class Live2DRenderer : protected QOpenGLFunctions
{
public:
    Live2DRenderer();
    ~Live2DRenderer();

    bool initialize(QString* error);
    bool uploadTextures(const QVector<QImage>& images, QString* error);
    QImage render(Live2DModel& model, const QSize& size, qreal margin = 0.03);
    void shutdown();
    bool isReady() const { return ready_; }

private:
    struct Transform
    {
        float scale = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        QSize size;
    };

    bool ensureFramebuffers(const QSize& size, QString* error);
    void renderMaskPass(csmModel* model, int drawable, const Transform& transform);
    void drawMesh(QOpenGLShaderProgram& program, csmModel* model, int drawable,
                  const Transform& transform, bool withMask, bool invertedMask);

    QOpenGLContext context_;
    QOffscreenSurface surface_;
    QOpenGLShaderProgram program_;      // 主渲染（带遮罩采样）
    QOpenGLShaderProgram maskProgram_;  // 遮罩写入
    QOpenGLBuffer vertexBuffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer indexBuffer_{QOpenGLBuffer::IndexBuffer};
    QVector<QOpenGLTexture*> textures_;
    QOpenGLFramebufferObject* framebuffer_ = nullptr;
    QOpenGLFramebufferObject* maskFramebuffer_ = nullptr;
    bool ready_ = false;

    int attributePosition_ = 0;
    int attributeUv_ = 1;

    int uniformViewport_ = -1;
    int uniformTexture_ = -1;
    int uniformOpacity_ = -1;
    int uniformMultiply_ = -1;
    int uniformScreen_ = -1;
    int uniformMask_ = -1;
    int uniformUseMask_ = -1;
    int uniformInvertMask_ = -1;
    int uniformResolution_ = -1;

    int maskUniformViewport_ = -1;
    int maskUniformTexture_ = -1;
};