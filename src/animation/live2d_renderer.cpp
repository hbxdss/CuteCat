#include "animation/live2d_renderer.h"

#include "animation/live2d_model.h"
#include "Live2DCubismCore.h"

#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QtDebug>

#include <algorithm>
#include <numeric>
#include <vector>

namespace {
const char* kVertexShader = R"(#version 120
attribute vec2 aPos;   // 屏幕像素坐标，y 向下
attribute vec2 aUv;
uniform vec2 uViewport;
varying vec2 vUv;
void main()
{
    vUv = aUv;
    vec2 ndc = vec2(aPos.x / uViewport.x * 2.0 - 1.0,
                    1.0 - aPos.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 120
varying vec2 vUv;
uniform sampler2D uTexture;
uniform sampler2D uMask;
uniform float uOpacity;
uniform vec4 uMultiply;
uniform vec4 uScreen;
uniform float uUseMask;
uniform float uInvertMask;
uniform vec2 uResolution;

void main()
{
    vec4 texel = texture2D(uTexture, vUv);
    texel.rgb *= uMultiply.rgb;
    texel.rgb = texel.rgb + uScreen.rgb - texel.rgb * uScreen.rgb;

    float alpha = texel.a * uOpacity;
    if (uUseMask > 0.5) {
        float coverage = texture2D(uMask, gl_FragCoord.xy / uResolution).a;
        coverage = clamp(coverage, 0.0, 1.0);
        alpha *= (uInvertMask > 0.5) ? (1.0 - coverage) : coverage;
    }
    // 预乘 alpha 输出，配合 glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
    gl_FragColor = vec4(texel.rgb * alpha, alpha);
}
)";

// 遮罩写入：只关心覆盖范围（贴图 alpha），颜色固定白色
const char* kMaskFragmentShader = R"(#version 120
varying vec2 vUv;
uniform sampler2D uTexture;
void main()
{
    float coverage = texture2D(uTexture, vUv).a;
    gl_FragColor = vec4(coverage);
}
)";
} // namespace

Live2DRenderer::Live2DRenderer() = default;

Live2DRenderer::~Live2DRenderer()
{
    shutdown();
}

bool Live2DRenderer::initialize(QString* error)
{
    if (ready_) {
        return true;
    }

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::NoProfile); // 兼容模式，无需 VAO
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setAlphaBufferSize(8);

    context_.setFormat(format);
    if (!context_.create()) {
        if (error) {
            *error = QStringLiteral("无法创建 OpenGL 上下文");
        }
        return false;
    }
    surface_.setFormat(context_.format());
    surface_.create();
    if (!surface_.isValid() || !context_.makeCurrent(&surface_)) {
        if (error) {
            *error = QStringLiteral("无法创建离屏 OpenGL 表面");
        }
        return false;
    }
    initializeOpenGLFunctions();

    const auto buildProgram = [this](QOpenGLShaderProgram& program, const char* fragment,
                                     QString* buildError) {
        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
            || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)) {
            if (buildError) {
                *buildError = QStringLiteral("着色器编译失败：%1").arg(program.log());
            }
            return false;
        }
        program.bindAttributeLocation("aPos", attributePosition_);
        program.bindAttributeLocation("aUv", attributeUv_);
        if (!program.link()) {
            if (buildError) {
                *buildError = QStringLiteral("着色器链接失败：%1").arg(program.log());
            }
            return false;
        }
        return true;
    };

    if (!buildProgram(program_, kFragmentShader, error)
        || !buildProgram(maskProgram_, kMaskFragmentShader, error)) {
        context_.doneCurrent();
        return false;
    }

    uniformViewport_ = program_.uniformLocation("uViewport");
    uniformTexture_ = program_.uniformLocation("uTexture");
    uniformOpacity_ = program_.uniformLocation("uOpacity");
    uniformMultiply_ = program_.uniformLocation("uMultiply");
    uniformScreen_ = program_.uniformLocation("uScreen");
    uniformMask_ = program_.uniformLocation("uMask");
    uniformUseMask_ = program_.uniformLocation("uUseMask");
    uniformInvertMask_ = program_.uniformLocation("uInvertMask");
    uniformResolution_ = program_.uniformLocation("uResolution");

    maskUniformViewport_ = maskProgram_.uniformLocation("uViewport");
    maskUniformTexture_ = maskProgram_.uniformLocation("uTexture");

    vertexBuffer_.create();
    indexBuffer_.create();

    context_.doneCurrent();
    ready_ = true;
    return true;
}

bool Live2DRenderer::uploadTextures(const QVector<QImage>& images, QString* error)
{
    if (!ready_) {
        if (error) {
            *error = QStringLiteral("渲染器尚未初始化");
        }
        return false;
    }
    if (!context_.makeCurrent(&surface_)) {
        if (error) {
            *error = QStringLiteral("无法激活 OpenGL 上下文");
        }
        return false;
    }

    for (QOpenGLTexture* texture : textures_) {
        delete texture;
    }
    textures_.clear();

    for (const QImage& image : images) {
        if (image.isNull()) {
            continue;
        }
        auto* texture = new QOpenGLTexture(image, QOpenGLTexture::DontGenerateMipMaps);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        textures_.append(texture);
    }

    context_.doneCurrent();
    if (textures_.isEmpty()) {
        if (error) {
            *error = QStringLiteral("没有可上传的贴图");
        }
        return false;
    }
    return true;
}

bool Live2DRenderer::ensureFramebuffers(const QSize& size, QString* error)
{
    const auto create = [&size, error](QOpenGLFramebufferObject*& target) {
        if (target && target->size() == size) {
            return true;
        }
        delete target;
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
format.setInternalTextureFormat(GL_RGBA8);
        format.setSamples(0); // 多重采样在部分驱动上会导致下方黑块，改用超高倍率超采样
        target = new QOpenGLFramebufferObject(size, format);
        if (!target->isValid()) {
            delete target;
            target = nullptr;
            if (error) {
                *error = QStringLiteral("无法创建离屏帧缓冲");
            }
            return false;
        }
        return true;
    };
    return create(framebuffer_) && create(maskFramebuffer_);
}

QImage Live2DRenderer::render(Live2DModel& model, const QSize& size, qreal margin)
{
    if (!ready_ || !model.isValid() || size.isEmpty()) {
        return QImage();
    }
    if (!context_.makeCurrent(&surface_)) {
        return QImage();
    }
    if (!ensureFramebuffers(size, nullptr)) {
        context_.doneCurrent();
        return QImage();
    }

    csmModel* core = model.core();
    const int drawableCount = csmGetDrawableCount(core);
    if (drawableCount <= 0) {
        context_.doneCurrent();
        return QImage();
    }

    QRectF bounds = model.canvasRect();
    if (bounds.isEmpty()) {
        bounds = model.poseBounds();
    }
    const qreal usable = 1.0 - margin * 2.0;
    Transform transform;
    transform.scale =
        float(qMin(size.width() / bounds.width(), size.height() / bounds.height()) * usable);
    transform.offsetX = float(size.width() * 0.5 - bounds.center().x() * transform.scale);
    transform.offsetY = float(size.height() * 0.5 + bounds.center().y() * transform.scale);
    transform.size = size;

    framebuffer_->bind();
    glViewport(0, 0, size.width(), size.height());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);

    program_.bind();
    glUniform2f(uniformViewport_, float(size.width()), float(size.height()));
    glUniform1i(uniformTexture_, 0);
    glUniform1i(uniformMask_, 1);
    glUniform2f(uniformResolution_, float(size.width()), float(size.height()));

    // 按渲染顺序从后往前画
    std::vector<int> order(static_cast<size_t>(drawableCount), 0);
    std::iota(order.begin(), order.end(), 0);
    const int* renderOrders = csmGetRenderOrders(core);
    std::stable_sort(order.begin(), order.end(),
                     [renderOrders](int a, int b) { return renderOrders[a] < renderOrders[b]; });

    const csmFlags* dynamicFlags = csmGetDrawableDynamicFlags(core);
    const csmFlags* constantFlags = csmGetDrawableConstantFlags(core);
const float* opacities = csmGetDrawableOpacities(core);
    const int* maskCounts = csmGetDrawableMaskCounts(core);
    const int* parentParts = csmGetDrawableParentPartIndices(core);
    const float* partOpacities = csmGetPartOpacities(core);
    const int partCount = csmGetPartCount(core);

    for (int drawable : order) {
        // Cubism 的最终透明度 = 网格透明度 × 所属部件的透明度
        float alpha = opacities[drawable];
        const int part = parentParts[drawable];
        if (part >= 0 && part < partCount) {
            alpha *= partOpacities[part];
        }
        if (!(dynamicFlags[drawable] & csmIsVisible) || alpha <= 0.001f) {
            continue;
        }
        const bool masked = maskCounts[drawable] > 0;
        const bool inverted = (constantFlags[drawable] & csmIsInvertedMask) != 0;
        if (masked) {
            renderMaskPass(core, drawable, transform);
            framebuffer_->bind();
            glViewport(0, 0, size.width(), size.height());
            program_.bind();
            glUniform2f(uniformViewport_, float(size.width()), float(size.height()));
            glUniform1i(uniformTexture_, 0);
            glUniform1i(uniformMask_, 1);
            glUniform2f(uniformResolution_, float(size.width()), float(size.height()));
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, maskFramebuffer_->texture());
            glActiveTexture(GL_TEXTURE0);
        }
glUniform1f(uniformOpacity_, alpha);
        glUniform1f(uniformUseMask_, masked ? 1.0f : 0.0f);
        glUniform1f(uniformInvertMask_, inverted ? 1.0f : 0.0f);
        // Cubism 三种混合模式：普通 / 叠加 / 正片叠底
        // 叠加层（闪电等发光特效用得上）；正片叠底层按普通方式画，
        // 预乘 alpha 下面板之相乘会把整块压黑
        if (constantFlags[drawable] & csmBlendAdditive) {
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        drawMesh(program_, core, drawable, transform, masked, inverted);
    }

    program_.release();
    framebuffer_->release();

    QImage image = framebuffer_->toImage();
    context_.doneCurrent();
    if (!image.isNull()) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return image;
}

void Live2DRenderer::renderMaskPass(csmModel* model, int drawable, const Transform& transform)
{
    const int* maskCounts = csmGetDrawableMaskCounts(model);
    const int* const* masks = csmGetDrawableMasks(model);

    maskFramebuffer_->bind();
    glViewport(0, 0, transform.size.width(), transform.size.height());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    // 多个 mask 取并集（覆盖度累加），采样时再 clamp
    glBlendFunc(GL_ONE, GL_ONE);
    glActiveTexture(GL_TEXTURE0);

    maskProgram_.bind();
    glUniform2f(maskUniformViewport_, float(transform.size.width()),
                float(transform.size.height()));
    glUniform1i(maskUniformTexture_, 0);

    const int count = maskCounts[drawable];
    for (int i = 0; i < count; ++i) {
        const int maskDrawable = masks[drawable][i];
        if (maskDrawable < 0 || maskDrawable >= csmGetDrawableCount(model)) {
            continue;
        }
        drawMesh(maskProgram_, model, maskDrawable, transform, false, false);
    }
    maskProgram_.release();
}

void Live2DRenderer::drawMesh(QOpenGLShaderProgram& program, csmModel* model, int drawable,
                              const Transform& transform, bool withMask, bool invertedMask)
{
    Q_UNUSED(withMask)
    Q_UNUSED(invertedMask)

    const csmVector2** positions = csmGetDrawableVertexPositions(model);
    const csmVector2** uvs = csmGetDrawableVertexUvs(model);
    const int* vertexCounts = csmGetDrawableVertexCounts(model);
    const int* indexCounts = csmGetDrawableIndexCounts(model);
    const unsigned short** indices = csmGetDrawableIndices(model);
    const int* textureIndices = csmGetDrawableTextureIndices(model);
    const csmVector4* multiplyColors = csmGetDrawableMultiplyColors(model);
    const csmVector4* screenColors = csmGetDrawableScreenColors(model);

    const int vertexCount = vertexCounts[drawable];
    const int indexCount = indexCounts[drawable];
    if (vertexCount <= 0 || indexCount <= 0 || !positions[drawable] || !uvs[drawable]) {
        return;
    }

    const int textureIndex = textureIndices[drawable];
    if (textureIndex < 0 || textureIndex >= textures_.size()) {
        return;
    }

    QVector<float> vertices;
    vertices.resize(vertexCount * 4);
    for (int i = 0; i < vertexCount; ++i) {
        vertices[i * 4 + 0] = positions[drawable][i].X * transform.scale + transform.offsetX;
        vertices[i * 4 + 1] = transform.offsetY - positions[drawable][i].Y * transform.scale;
vertices[i * 4 + 2] = uvs[drawable][i].X;
        // Cubism 的 UV 原点在贴图左上角，OpenGL 纹理原点在左下角，纵向要翻转
        vertices[i * 4 + 3] = 1.0f - uvs[drawable][i].Y;
    }

    vertexBuffer_.bind();
    vertexBuffer_.allocate(vertices.constData(), int(sizeof(float) * size_t(vertices.size())));
    indexBuffer_.bind();
    indexBuffer_.allocate(indices[drawable], int(sizeof(unsigned short) * size_t(indexCount)));

    program.enableAttributeArray(attributePosition_);
    program.enableAttributeArray(attributeUv_);
    program.setAttributeBuffer(attributePosition_, GL_FLOAT, 0, 2, 4 * sizeof(float));
    program.setAttributeBuffer(attributeUv_, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    if (&program == &program_) {
        const csmVector4 multiply = multiplyColors[drawable];
        const csmVector4 screen = screenColors[drawable];
        glUniform4f(uniformMultiply_, multiply.X, multiply.Y, multiply.Z, 1.0f);
        glUniform4f(uniformScreen_, screen.X, screen.Y, screen.Z, 0.0f);
    }

    textures_.at(textureIndex)->bind(0);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    textures_.at(textureIndex)->release(0);

    program.disableAttributeArray(attributePosition_);
    program.disableAttributeArray(attributeUv_);
    vertexBuffer_.release();
    indexBuffer_.release();
}

void Live2DRenderer::shutdown()
{
    if (context_.isValid() && surface_.isValid() && context_.makeCurrent(&surface_)) {
        for (QOpenGLTexture* texture : textures_) {
            delete texture;
        }
        textures_.clear();
        if (vertexBuffer_.isCreated()) {
            vertexBuffer_.destroy();
        }
        if (indexBuffer_.isCreated()) {
            indexBuffer_.destroy();
        }
        delete framebuffer_;
        framebuffer_ = nullptr;
        delete maskFramebuffer_;
        maskFramebuffer_ = nullptr;
        program_.removeAllShaders();
        maskProgram_.removeAllShaders();
        context_.doneCurrent();
    }
    if (surface_.isValid()) {
        surface_.destroy();
    }
    ready_ = false;
}