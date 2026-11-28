#pragma once

#include <QObject>
#include <QString>

#include <QtGlobal>

// 键盘活动监听：给桌宠提供“正在打字”的信号（BongoCat 的核心手感）。
// Windows 下用 WH_KEYBOARD_LL 低级钩子；其它平台暂时不启用。
class KeyboardWatcher : public QObject
{
    Q_OBJECT

public:
    explicit KeyboardWatcher(QObject* parent = nullptr);
    ~KeyboardWatcher() override;

    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const { return hookHandle_ != nullptr; }

    // 由键盘钩子回调触发（同一线程，Qt 事件循环里执行）
    void handleActivity();

signals:
    void keyActivity();

private:
    void* hookHandle_ = nullptr;
    qint64 lastEmitMs_ = 0;
};