#include "animation/live2d_model.h"
#include "animation/live2d_renderer.h"
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
    const QCommandLineOption probeOption(
        QStringList{QStringLiteral("probe-model")},
        QStringLiteral("逐个参数试探受影响的网格透明度（排查用）"),
        QStringLiteral("model3.json"));
    parser.addOption(probeOption);
    const QCommandLineOption renderOption(
        QStringList{QStringLiteral("render-model")},
        QStringLiteral("把模型按画布取景渲染成 PNG 后退出（排查用）"),
        QStringLiteral("model3.json"));
    parser.addOption(renderOption);
    const QCommandLineOption outOption(
        QStringList{QStringLiteral("out")}, QStringLiteral("PNG 输出路径"), QStringLiteral("png"));
    parser.addOption(outOption);
    const QCommandLineOption sizeOption(
        QStringList{QStringLiteral("size")}, QStringLiteral("输出尺寸，形如 612x354"),
        QStringLiteral("WxH"));
    parser.addOption(sizeOption);
    const QCommandLineOption setOption(
        QStringList{QStringLiteral("set")},
        QStringLiteral("渲染前设置模型参数，形如 Param2=1，可重复"), QStringLiteral("id=value"));
    parser.addOption(setOption);
    const QCommandLineOption partOption(
        QStringList{QStringLiteral("part-opacity")},
        QStringLiteral("渲染前设置部件透明度，形如 7=0，可重复"), QStringLiteral("index=value"));
    parser.addOption(partOption);
    parser.process(app);

    if (parser.isSet(renderOption)) {
        Live2DModel model;
        QString renderError;
        if (!model.load(parser.value(renderOption), &renderError)) {
            std::puts(qUtf8Printable(QStringLiteral("加载失败: ") + renderError));
            return 1;
        }
        QSize size(qRound(model.canvasSize().width()), qRound(model.canvasSize().height()));
        const QString sizeText = parser.value(sizeOption);
        if (!sizeText.isEmpty()) {
            const QStringList wh = sizeText.split(QLatin1Char('x'));
            if (wh.size() == 2) {
                size = QSize(wh.at(0).toInt(), wh.at(1).toInt());
            }
        }
        // 先 update 一次：模型更新会把部件透明度重置回默认值
        model.update();
        for (const QString& pair : parser.values(setOption)) {
            const QStringList kv = pair.split(QLatin1Char('='));
            if (kv.size() != 2) {
                continue;
            }
            const int index = model.findParameter(kv.at(0));
            if (index >= 0) {
                model.setParameter(index, kv.at(1).toFloat());
            }
        }
        {
            float* partOpacities = csmGetPartOpacities(model.core());
            const int partCount = csmGetPartCount(model.core());
            for (const QString& pair : parser.values(partOption)) {
                const QStringList kv = pair.split(QLatin1Char('='));
                if (kv.size() != 2) {
                    continue;
                }
                const int index = kv.at(0).toInt();
                if (index >= 0 && index < partCount) {
                    partOpacities[index] = kv.at(1).toFloat();
                }
            }
        }
        Live2DRenderer renderer;
        if (!renderer.initialize(&renderError)
            || !renderer.uploadTextures(model.textures(), &renderError)) {
            std::puts(qUtf8Printable(QStringLiteral("渲染器失败: ") + renderError));
            return 1;
        }
        const QImage image = renderer.render(model, size, 0.0);
        const QString outPath = parser.value(outOption);
        if (image.isNull() || outPath.isEmpty() || !image.save(outPath)) {
            std::puts("渲染或保存失败");
            return 1;
        }
        std::puts(qUtf8Printable(QStringLiteral("已写出 %1 (%2x%3)")
                                     .arg(outPath)
                                     .arg(image.width())
                                     .arg(image.height())));
        return 0;
    }

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

    if (parser.isSet(probeOption)) {
        Live2DModel model;
        QString probeError;
        if (!model.load(parser.value(probeOption), &probeError)) {
            std::puts(qUtf8Printable(QStringLiteral("加载失败: ") + probeError));
            return 1;
        }
        csmModel* core = model.core();
        const int drawableCount = csmGetDrawableCount(core);
        const char** drawableIds = csmGetDrawableIds(core);
        const float* opacities = csmGetDrawableOpacities(core);
        const csmFlags* dynamicFlags = csmGetDrawableDynamicFlags(core);

        model.update();
        QVector<float> baseOpacity(drawableCount, 0.0f);
        QVector<int> baseVisible(drawableCount, 0);
        for (int d = 0; d < drawableCount; ++d) {
            baseOpacity[d] = opacities[d];
            baseVisible[d] = int(dynamicFlags[d] & csmIsVisible);
        }

        std::puts("== param -> drawable opacity/visibility ==");
        for (int p = 0; p < model.parameterCount(); ++p) {
            const QString id = model.parameterId(p);
            const float def = model.parameterDefault(p);
            const float trials[2] = {model.parameterMin(p), model.parameterMax(p)};
            QStringList hits;
            for (float v : trials) {
                if (qAbs(v - def) < 0.001f) {
                    continue;
                }
                model.setParameter(p, v);
                model.update();
                for (int d = 0; d < drawableCount; ++d) {
                    const int vis = int(dynamicFlags[d] & csmIsVisible);
                    if (qAbs(opacities[d] - baseOpacity[d]) > 0.01f || vis != baseVisible[d]) {
                        hits << QStringLiteral("%1 %2->%3(vis %4)")
                                    .arg(QString::fromLatin1(drawableIds[d]))
                                    .arg(double(baseOpacity[d]), 0, 'f', 2)
                                    .arg(double(opacities[d]), 0, 'f', 2)
                                    .arg(vis);
                    }
                }
                model.setParameter(p, def);
                model.update();
            }
            if (!hits.isEmpty()) {
                std::puts(qUtf8Printable(
                    QStringLiteral("%1 | %2").arg(id.leftJustified(20), hits.join(QStringLiteral(" ; ")))));
            }
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