#include "animation/skin_factory.h"

#include "animation/image_skin.h"
#include "animation/lottie_skin.h"
#include "config.h"

#include <QDebug>
#include <QFileInfo>

namespace {
const QString kDefaultImage = QStringLiteral(":/pet/cat_idle.png");
}

std::unique_ptr<PetSkin> createSkin(const Config& config, QString* error)
{
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