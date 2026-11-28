#include "config.h"
#include "pet.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

#ifndef CUTECAT_VERSION
#define CUTECAT_VERSION "0.0.0"
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CuteCat"));
    QApplication::setApplicationDisplayName(QStringLiteral("CuteCat 桌宠"));
    QApplication::setApplicationVersion(QStringLiteral(CUTECAT_VERSION));
    QApplication::setOrganizationName(QStringLiteral("hbxdss"));
    QApplication::setOrganizationDomain(QStringLiteral("github.com/hbxdss"));
    // 关掉窗口（躲进托盘）不代表退出程序
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CuteCat 桌面宠物"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption snapshotOption(
        QStringList{QStringLiteral("snapshot")},
        QStringLiteral("渲染当前画面到 PNG 后退出（用于自检/截图）"), QStringLiteral("file"));
    parser.addOption(snapshotOption);
    parser.process(app);

    const bool snapshotMode = parser.isSet(snapshotOption);

    // 单实例：重复启动时只提醒一句，不叠加第二只猫
    QLockFile lockFile(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("cutecat-desktop-pet.lock")));
    lockFile.setStaleLockTime(0);
    if (!lockFile.tryLock(120)) {
        if (!snapshotMode) {
            QMessageBox::information(nullptr, QStringLiteral("CuteCat"),
                                     QStringLiteral("CuteCat 已经在桌面上啦，看看屏幕右下角~"));
        }
        return 0;
    }

    Config config;
    QString error;
    config.load(&error);
    if (!error.isEmpty()) {
        std::fprintf(stderr, "配置提示：%s\n", qUtf8Printable(error));
    }

    PetWindow pet(&config);
    if (!pet.initialize(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("CuteCat"),
                              QStringLiteral("桌宠启动失败：%1").arg(error));
        return 1;
    }

    pet.show();

    // 第一次运行（还没有记录过位置）给一句提示，免得用户找不到猫在哪
    if (!snapshotMode && config.lastPosition().x() < 0) {
        pet.showBubble(QStringLiteral("喵~ 我在这儿，点我摸摸头"));
    }

    if (snapshotMode) {
        pet.showBubble(QStringLiteral("喵~ 抓到一只小虫子！"));
        const QString path = parser.value(snapshotOption);
        QTimer::singleShot(700, &app, [&pet, path] {
            const QPixmap pixmap = pet.grab();
            if (pixmap.isNull() || !pixmap.save(path)) {
                std::fprintf(stderr, "快照保存失败：%s\n", qUtf8Printable(path));
            } else {
                std::printf("快照已保存：%s (%dx%d)\n", qUtf8Printable(path), pixmap.width(),
                            pixmap.height());
            }
            QApplication::quit();
        });
    }

    return app.exec();
}