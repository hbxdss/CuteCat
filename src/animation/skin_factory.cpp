#include "animation/skin_factory.h"

#include "animation/image_skin.h"
#include "animation/live2d_skin.h"
#include "animation/lottie_skin.h"
#include "config.h"

#include <QDebug>
#include <QFileInfo>

namespace {
const QString kDefaultImage = QStringLiteral(":/pet/cat_idle.png");
}

std::unique_ptr<PetSkin> createSkin(const Config& config, QString* error)
{
    // BongoCat 的模型就是标准 Live2D 模型目录，这里优先处理
    if (config.animation() == QLatin1String("live2d")) {
        const QString modelPath = config.live2DModel();
        if (!modelPath.isEmpty() && QFileInfo::exists(modelPath)) {
            auto live2d = std::make_unique<Live2DSkin>();
            QString live2dError;
            if (live2d->load(modelPath, &live2dError)) {
                return live2d;
            }
            qInfo().noquote() << "Live2D 模型加载失败，改用内置图片皮肤：" << live2dError;
        } else {
            qInfo().noquote() << "配置里的 Live2D 模型不存在：" << modelPath;
        }
    }

    if (config.animation() == QLatin1String("lottie")) {
        auto lottie = std::make_unique<LottieSkin>();
        QString lottieError;
        if (lottie->load(config.lottieFile(), &lottieError)) {
            return lottie;
        }
        qInfo().noquote() << "Lottie 皮肤不可用，改用内置图片皮肤：" << lottieError;
    }

    QString imagePath = config.imageFile();
    if (imagePath.trimmed().isEmpty()) {
        imagePath = kDefaultImage;
    } else if (!imagePath.startsWith(QLatin1String(":/")) && !QFileInfo::exists(imagePath)) {
        qInfo().noquote() << "配置里的形象图片不存在，改用内置默认形象：" << imagePath;
        imagePath = kDefaultImage;
    }

    auto image = std::make_unique<ImageSkin>();
    QString imageError;
    if (!image->load(imagePath, &imageError)) {
        if (error) {
            *error = imageError;
        }
        return nullptr;
    }
    return image;
}