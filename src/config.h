#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QSize>
#include <QString>

// 用户配置（config.json），字段全部带默认值和范围校验：
// 就算手工把配置改坏了，桌宠也能正常启动。
//
// 读取顺序：用户配置（%APPDATA%/CuteCat/config.json）→ 内置默认（qrc:/config.json）。
class Config
{
public:
    Config();

    static QString userConfigPath();

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    QSize windowSize() const;
    void setWindowWidth(int width);
    int windowWidth() const;
    int windowHeight() const;

    bool keepOnTop() const;
    void setKeepOnTop(bool value);

    bool clickThrough() const;
    void setClickThrough(bool value);

    bool rememberPosition() const;
    void setRememberPosition(bool value);

    QPoint lastPosition() const;
    void setLastPosition(const QPoint& position);

    qreal opacity() const;
    void setOpacity(qreal value);

    bool autoStart() const;
    void setAutoStart(bool value);

    int frameRate() const;

    // "image"：内置图片皮肤；"lottie"：rlottie 动画皮肤
    QString animation() const;
    void setAnimation(const QString& value);

    // 空字符串表示使用内置默认形象 qrc:/pet/cat_idle.png
    QString imageFile() const;
    QString lottieFile() const;

private:
    void applyDefaults();
    void mergeKnownKeys(const QJsonObject& source);

    QJsonObject root_;
};