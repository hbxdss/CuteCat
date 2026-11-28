#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

namespace {
constexpr int kMinWidth = 160;
constexpr int kMaxWidth = 640;
constexpr int kMinFrameRate = 30;
constexpr int kMaxFrameRate = 120;
constexpr qreal kMinOpacity = 0.35;
constexpr qreal kMaxOpacity = 1.0;

// 气泡区域占的高度比例（窗口比形象本身高一点，留给说话气泡）
constexpr qreal kWindowAspect = 1.22;

QJsonObject makeDefaults()
{
    QJsonObject window;
    window.insert(QStringLiteral("width"), 300);
    window.insert(QStringLiteral("height"), 366);
    window.insert(QStringLiteral("keepOnTop"), true);
    window.insert(QStringLiteral("clickThrough"), false);
    window.insert(QStringLiteral("rememberPosition"), true);

    QJsonObject appearance;
    appearance.insert(QStringLiteral("opacity"), 1.0);
    appearance.insert(QStringLiteral("animation"), QStringLiteral("image"));
    appearance.insert(QStringLiteral("image"), QString());
    appearance.insert(QStringLiteral("lottie"), QStringLiteral(":/lottie/idle.json"));

    QJsonObject behavior;
    behavior.insert(QStringLiteral("autoStart"), false);
    behavior.insert(QStringLiteral("frameRate"), 60);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("window"), window);
    root.insert(QStringLiteral("appearance"), appearance);
    root.insert(QStringLiteral("behavior"), behavior);
    return root;
}

QJsonObject subObject(const QJsonObject& root, const QString& key)
{
    return root.value(key).toObject();
}
} // namespace

Config::Config()
{
    applyDefaults();
}

void Config::applyDefaults()
{
    root_ = makeDefaults();
}

QString Config::userConfigPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.cutecat");
    }
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

void Config::mergeKnownKeys(const QJsonObject& source)
{
    // 只接受已知字段，避免配置里塞进奇怪的东西影响启动
    const QStringList groups{QStringLiteral("window"), QStringLiteral("appearance"),
                             QStringLiteral("behavior")};
    for (const QString& group : groups) {
        const QJsonObject defaults = root_.value(group).toObject();
        const QJsonObject incoming = source.value(group).toObject();
        if (incoming.isEmpty()) {
            continue;
        }
        QJsonObject merged = defaults;
        for (auto it = incoming.begin(); it != incoming.end(); ++it) {
            if (defaults.contains(it.key()) && !it.value().isUndefined()) {
                merged.insert(it.key(), it.value());
            }
        }
        root_.insert(group, merged);
    }
}

bool Config::load(QString* error)
{
    applyDefaults();

    QByteArray payload;
    bool fromUserFile = false;

    QFile userFile(userConfigPath());
    if (userFile.exists() && userFile.open(QIODevice::ReadOnly)) {
        payload = userFile.readAll();
        fromUserFile = true;
    }

    if (payload.isEmpty()) {
        QFile builtin(QStringLiteral(":/config.json"));
        if (builtin.open(QIODevice::ReadOnly)) {
            payload = builtin.readAll();
        }
    }

    if (!payload.isEmpty()) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            mergeKnownKeys(document.object());
        } else if (error) {
            *error = QStringLiteral("config.json 解析失败（%1），已使用默认配置")
                         .arg(parseError.errorString());
        }
    }

    if (!fromUserFile) {
        // 第一次运行：把默认配置落到用户目录，方便用户自己改
        save(nullptr);
    }
    return true;
}

bool Config::save(QString* error) const
{
    const QString path = userConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法写入配置：%1").arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("保存配置失败：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

QSize Config::windowSize() const
{
    const int width = qBound(kMinWidth, windowWidth(), kMaxWidth);
    const int height = qMax(windowHeight(), width + 40);
    return QSize(width, height);
}

int Config::windowWidth() const
{
    return subObject(root_, QStringLiteral("window")).value(QStringLiteral("width")).toInt(300);
}

int Config::windowHeight() const
{
    return subObject(root_, QStringLiteral("window")).value(QStringLiteral("height")).toInt(366);
}

void Config::setWindowWidth(int width)
{
    const int clamped = qBound(kMinWidth, width, kMaxWidth);
    QJsonObject window = subObject(root_, QStringLiteral("window"));
    window.insert(QStringLiteral("width"), clamped);
    window.insert(QStringLiteral("height"), qRound(clamped * kWindowAspect));
    root_.insert(QStringLiteral("window"), window);
}

bool Config::keepOnTop() const
{
    return subObject(root_, QStringLiteral("window")).value(QStringLiteral("keepOnTop")).toBool(true);
}

void Config::setKeepOnTop(bool value)
{
    QJsonObject window = subObject(root_, QStringLiteral("window"));
    window.insert(QStringLiteral("keepOnTop"), value);
    root_.insert(QStringLiteral("window"), window);
}

bool Config::clickThrough() const
{
    return subObject(root_, QStringLiteral("window"))
        .value(QStringLiteral("clickThrough"))
        .toBool(false);
}

void Config::setClickThrough(bool value)
{
    QJsonObject window = subObject(root_, QStringLiteral("window"));
    window.insert(QStringLiteral("clickThrough"), value);
    root_.insert(QStringLiteral("window"), window);
}

bool Config::rememberPosition() const
{
    return subObject(root_, QStringLiteral("window"))
        .value(QStringLiteral("rememberPosition"))
        .toBool(true);
}

void Config::setRememberPosition(bool value)
{
    QJsonObject window = subObject(root_, QStringLiteral("window"));
    window.insert(QStringLiteral("rememberPosition"), value);
    root_.insert(QStringLiteral("window"), window);
}

QPoint Config::lastPosition() const
{
    const QJsonObject window = subObject(root_, QStringLiteral("window"));
    if (!window.contains(QStringLiteral("lastX")) || !window.contains(QStringLiteral("lastY"))) {
        return QPoint(-1, -1);
    }
    return QPoint(window.value(QStringLiteral("lastX")).toInt(-1),
                  window.value(QStringLiteral("lastY")).toInt(-1));
}

void Config::setLastPosition(const QPoint& position)
{
    QJsonObject window = subObject(root_, QStringLiteral("window"));
    window.insert(QStringLiteral("lastX"), position.x());
    window.insert(QStringLiteral("lastY"), position.y());
    root_.insert(QStringLiteral("window"), window);
}

qreal Config::opacity() const
{
    const qreal value = subObject(root_, QStringLiteral("appearance"))
                            .value(QStringLiteral("opacity"))
                            .toDouble(1.0);
    return qBound(kMinOpacity, value, kMaxOpacity);
}

void Config::setOpacity(qreal value)
{
    QJsonObject appearance = subObject(root_, QStringLiteral("appearance"));
    appearance.insert(QStringLiteral("opacity"), qBound(kMinOpacity, value, kMaxOpacity));
    root_.insert(QStringLiteral("appearance"), appearance);
}

bool Config::autoStart() const
{
    return subObject(root_, QStringLiteral("behavior"))
        .value(QStringLiteral("autoStart"))
        .toBool(false);
}

void Config::setAutoStart(bool value)
{
    QJsonObject behavior = subObject(root_, QStringLiteral("behavior"));
    behavior.insert(QStringLiteral("autoStart"), value);
    root_.insert(QStringLiteral("behavior"), behavior);
}

int Config::frameRate() const
{
    const int value = subObject(root_, QStringLiteral("behavior"))
                          .value(QStringLiteral("frameRate"))
                          .toInt(60);
    return qBound(kMinFrameRate, value, kMaxFrameRate);
}

QString Config::animation() const
{
    const QString value = subObject(root_, QStringLiteral("appearance"))
                              .value(QStringLiteral("animation"))
                              .toString(QStringLiteral("image"));
    return value.trimmed().toLower();
}

void Config::setAnimation(const QString& value)
{
    QJsonObject appearance = subObject(root_, QStringLiteral("appearance"));
    appearance.insert(QStringLiteral("animation"), value);
    root_.insert(QStringLiteral("appearance"), appearance);
}

QString Config::imageFile() const
{
    return subObject(root_, QStringLiteral("appearance"))
        .value(QStringLiteral("image"))
        .toString();
}

QString Config::lottieFile() const
{
    return subObject(root_, QStringLiteral("appearance"))
        .value(QStringLiteral("lottie"))
        .toString();
}