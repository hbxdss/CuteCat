#include "platform/keyboard_watcher.h"

#include <QDateTime>
#include <QtDebug>

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
KeyboardWatcher* g_watcher = nullptr;

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_watcher) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            g_watcher->handleActivity();
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
} // namespace
#endif

KeyboardWatcher::KeyboardWatcher(QObject* parent)
    : QObject(parent)
{
}

KeyboardWatcher::~KeyboardWatcher()
{
    stop();
}

bool KeyboardWatcher::start(QString* error)
{
#ifdef Q_OS_WIN
    if (hookHandle_) {
        return true;
    }
    g_watcher = this;
    hookHandle_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, GetModuleHandleW(nullptr), 0);
    if (!hookHandle_) {
        g_watcher = nullptr;
        if (error) {
            *error = QStringLiteral("键盘钩子安装失败（错误码 %1）").arg(GetLastError());
        }
        return false;
    }
    return true;
#else
    if (error) {
        *error = QStringLiteral("当前平台暂不支持键盘监听");
    }
    return false;
#endif
}

void KeyboardWatcher::stop()
{
#ifdef Q_OS_WIN
    if (hookHandle_) {
        UnhookWindowsHookEx(static_cast<HHOOK>(hookHandle_));
        hookHandle_ = nullptr;
        g_watcher = nullptr;
    }
#endif
}

void KeyboardWatcher::handleActivity()
{
    // 打字时按键很密集，节流到 45ms 一次就够了，避免动画抖动和信号风暴
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastEmitMs_ < 45) {
        return;
    }
    lastEmitMs_ = now;
    emit keyActivity();
}