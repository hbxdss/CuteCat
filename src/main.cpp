#include "animation/live2d_model.h"
#include "config.h"
#include "pet.h"

#include "Live2DCubismCore.h"

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
    const QCommandLineOption modelOption(
        QStringList{QStringLiteral("model")},
        QStringLiteral("临时用某个 Live2D 模型启动（*.model3.json，仅本次生效）"),
        QStringLiteral("model3.json"));
    parser.addOption(modelOption);
    const QCommandLineOption dumpOption(
        QStringList{QStringLiteral("dump-model")},
        QStringLiteral("打印 Live2D 模型的参数/部件/网格信息后退出（排查用）"),
        QStringLiteral("model3.json"));
    parser.addOption(dumpOption);
    parser.process(app);

    const bool snapshotMode = parser.isSet(snapshotOption);

    if (parser.isSet(dumpOption)) {
        Live2DModel model;
        QString dumpError;
        if (!model.load(parser.value(dumpOption), &dumpError)) {
            std::puts(qUtf8Printable(QStringLiteral("加载失败: ") + dumpError));
            return 1;
        }
        csmModel* core = model.core();
        std::puts(qUtf8Printable(QStringLiteral("canvas: %1 x %2")
                                     .arg(model.canvasSize().width())
                                     .arg(model.canvasSize().height())));
        std::puts(qUtf8Printable(QStringLiteral("pose bounds: %1 %2 %3 %4")
                                     .arg(model.poseBounds().left(), 0, 'f', 2)
                                     .arg(model.poseBounds().top(), 0, 'f', 2)
                                     .arg(model.poseBounds().width(), 0, 'f', 2)
                                     .arg(model.poseBounds().height(), 0, 'f', 2)));
        std::puts(qUtf8Printable(QStringLiteral("parameters: %1").arg(model.parameterCount())));
        for (int i = 0; i < model.parameterCount(); ++i) {
            std::puts(qUtf8Printable(
                QStringLiteral("  param %1 default=%2 min=%3 max=%4 value=%5")
                    .arg(model.parameterId(i).leftJustified(24))
                    .arg(model.parameterDefault(i), 0, 'f', 3)
                    .arg(model.parameterMin(i), 0, 'f', 3)
                    .arg(model.parameterMax(i), 0, 'f', 3)
                    .arg(model.parameterValue(i), 0, 'f', 3)));
        }
        const int drawableCount = csmGetDrawableCount(core);
        const char** ids = csmGetDrawableIds(core);
        const csmFlags* dflags = csmGetDrawableDynamicFlags(core);
        const float* ops = csmGetDrawableOpacities(core);
        const int* parents = csmGetDrawableParentPartIndices(core);
        const int* vcounts = csmGetDrawableVertexCounts(core);
        const int* texidx = csmGetDrawableTextureIndices(core);
        const csmVector2** pos = csmGetDrawableVertexPositions(core);
        std::puts(qUtf8Printable(QStringLiteral("drawables: %1").arg(drawableCount)));
        for (int i = 0; i < drawableCount; ++i) {
            float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
            for (int v = 0; v < vcounts[i]; ++v) {
                minx = qMin(minx, pos[i][v].X);
                maxx = qMax(maxx, pos[i][v].X);
                miny = qMin(miny, pos[i][v].Y);
                maxy = qMax(maxy, pos[i][v].Y);
            }
            std::puts(qUtf8Printable(
                QStringLiteral("  draw %1 %2 vis=%3 op=%4 part=%5 tex=%6 verts=%7 box=[%8,%9]-[%10,%11]")
                    .arg(i)
                    .arg(QString::fromLatin1(ids[i]).leftJustified(22))
                    .arg(int(dflags[i] & csmIsVisible))
                    .arg(double(ops[i]), 0, 'f', 2)
                    .arg(parents[i])
                    .arg(texidx[i])
                    .arg(vcounts[i])
                    .arg(double(minx), 0, 'f', 1)
                    .arg(double(miny), 0, 'f', 1)
                    .arg(double(maxx), 0, 'f', 1)
                    .arg(double(maxy), 0, 'f', 1)));
        }
        const int partCount = csmGetPartCount(core);
        const char** partIds = csmGetPartIds(core);
        const float* partOps = csmGetPartOpacities(core);
        std::puts(qUtf8Printable(QStringLiteral("parts: %1").arg(partCount)));
        for (int i = 0; i < partCount; ++i) {
            std::puts(qUtf8Printable(QStringLiteral("  part %1 %2 opacity=%3")
                                         .arg(i)
                                         .arg(QString::fromLatin1(partIds[i]).leftJustified(24))
                                         .arg(double(partOps[i]), 0, 'f', 3)));
        }
        return 0;
    }

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
    if (parser.isSet(modelOption)) {
        config.setAnimation(QStringLiteral("live2d"));
        config.setLive2DModel(parser.value(modelOption));
    }
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