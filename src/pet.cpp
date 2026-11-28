#include "pet.h"

#include "animation/skin_factory.h"
#include "config.h"
#include "platform/keyboard_watcher.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>
#include <cstdio>

namespace {
constexpr const char* kAutoStartKey = "CuteCat";
constexpr const char* kRunKeyPath =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 扫描放模型的两个位置：exe 旁边的 models 目录，以及用户配置目录下的 models
QStringList discoverLive2DModels()
{
    QStringList roots;
    roots << QCoreApplication::applicationDirPath() + QStringLiteral("/models")
          << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                 + QStringLiteral("/models");
    QStringList found;
    for (const QString& rootPath : roots) {
        QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }
        const auto addFrom = [&found](const QDir& dir) {
            const QStringList names =
                dir.entryList({QStringLiteral("*.model3.json")}, QDir::Files, QDir::Name);
            for (const QString& name : names) {
                found.append(dir.filePath(name));
            }
        };
        addFrom(root);
        const QStringList subDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sub : subDirs) {
            addFrom(QDir(root.filePath(sub)));
        }
    }
    found.removeDuplicates();
    if (qEnvironmentVariableIsSet("CUTECAT_DEBUG_LIVE2D")) {
        for (const QString& path : found) {
            std::puts(qUtf8Printable(QStringLiteral("discovered model: ") + path));
        }
    }
    return found;
}

QStringList bubbleLines()
{
    return {
        QStringLiteral("喵~"),
        QStringLiteral("摸摸头"),
        QStringLiteral("今天也要加油鸭"),
        QStringLiteral("抓到一只小虫子！"),
        QStringLiteral("代码又跑通了？"),
        QStringLiteral("困了…"),
        QStringLiteral("陪我玩一会儿嘛"),
        QStringLiteral("记得按 Ctrl+S"),
    };
}
} // namespace

PetWindow::PetWindow(Config* config, QWidget* parent)
    : QWidget(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("CuteCat"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::OpenHandCursor);

    frameClock_.start();
    lastTickMs_ = frameClock_.elapsed();

    connect(&frameTimer_, &QTimer::timeout, this, &PetWindow::onFrame);

    bubbleTimer_.setSingleShot(true);
    bubbleTimer_.setInterval(2200);
    connect(&bubbleTimer_, &QTimer::timeout, this, [this] {
        bubbleText_.clear();
        bubbleStartedMs_ = -1;
        update();
    });
}

PetWindow::~PetWindow() = default;

bool PetWindow::initialize(QString* error)
{
    if (!loadSkin(error)) {
        return false;
    }
    applyAppearance();
    restorePosition();
    setupTray();

    // 键盘监听：给 Live2D 皮肤提供“正在打字”的信号
    keyboard_ = std::make_unique<KeyboardWatcher>(this);
    connect(keyboard_.get(), &KeyboardWatcher::keyActivity, this, [this] {
        if (skin_) {
            skin_->notifyKeyActivity();
        }
    });
    QString keyboardError;
    if (!keyboard_->start(&keyboardError)) {
        qInfo().noquote() << "键盘监听未启用：" << keyboardError;
    }

    frameTimer_.start(qMax(8, 1000 / config_->frameRate()));
    return true;
}

bool PetWindow::loadSkin(QString* error)
{
    auto skin = createSkin(*config_, error);
    if (!skin) {
        return false;
    }
    skin_ = std::move(skin);

    iconPixmap_ = skin_->iconPixmap(64);
    if (!iconPixmap_.isNull()) {
        setWindowIcon(QIcon(iconPixmap_));
        if (tray_) {
            tray_->setIcon(QIcon(iconPixmap_));
        }
    }
    update();
    return true;
}

QSize PetWindow::desiredWindowSize() const
{
    const int width = qBound(160, config_->windowWidth(), 640);
    if (!skin_) {
        return config_->windowSize();
    }
    const QSize native = skin_->nativeSize();
    if (native.isEmpty() || native.width() <= 0) {
        return config_->windowSize();
    }
    // 形象是什么比例窗口就是什么比例（Live2D 画布是横向的），上方再留一截给气泡
    constexpr int kBubbleBand = 44;
    const int height = qRound(qreal(width) * native.height() / native.width());
    return QSize(width, height + kBubbleBand);
}

void PetWindow::applyAppearance()
{
    resize(desiredWindowSize());
    setWindowOpacity(config_->opacity());
    applyWindowFlags();

    if (config_->clickThrough()) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        unsetCursor();
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setCursor(Qt::OpenHandCursor);
    }
}

void PetWindow::applyWindowFlags()
{
    const bool wasVisible = isVisible();
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (config_->keepOnTop()) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    if (windowFlags() != flags) {
        setWindowFlags(flags);
        if (wasVisible) {
            show();
        }
    }
}

void PetWindow::restorePosition()
{
    QPoint position(-1, -1);
    if (config_->rememberPosition()) {
        position = config_->lastPosition();
    }

    QScreen* screen = nullptr;
    if (position.x() >= 0 && position.y() >= 0) {
        screen = QGuiApplication::screenAt(position + QPoint(width() / 2, height() / 2));
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    QRect frame(position, size());
    if (position.x() < 0 || position.y() < 0) {
        // 第一次运行：默认蹲在屏幕右下角，离任务栏留一点空隙
        frame.moveBottomRight(available.bottomRight() - QPoint(32, 24));
    } else {
        if (frame.right() > available.right()) {
            frame.moveRight(available.right());
        }
        if (frame.bottom() > available.bottom()) {
            frame.moveBottom(available.bottom());
        }
        if (frame.left() < available.left()) {
            frame.moveLeft(available.left());
        }
        if (frame.top() < available.top()) {
            frame.moveTop(available.top());
        }
    }
    move(frame.topLeft());
}

void PetWindow::savePosition()
{
    if (!config_->rememberPosition()) {
        return;
    }
    config_->setLastPosition(pos());
    config_->save();
}

QRectF PetWindow::petRect() const
{
    // 形象是横向画布（Live2D）时，绘制区按它的宽高比铺满宽度，底部对齐窗底
    if (skin_) {
        const QSize native = skin_->nativeSize();
        if (!native.isEmpty() && native.width() > 0) {
            qreal w = width();
            qreal h = w * native.height() / native.width();
            if (h > height()) {
                h = height();
                w = h * native.width() / native.height();
            }
            return QRectF((width() - w) / 2.0, height() - h, w, h);
        }
    }
    const qreal side = qMin<qreal>(width(), height());
    return QRectF((width() - side) / 2.0, height() - side, side, side);
}

bool PetWindow::hitPet(const QPointF& position) const
{
    return skin_ && skin_->hitTest(position, petRect());
}

void PetWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (skin_) {
        skin_->paint(&painter, petRect(), 1.0);
    }
    if (!bubbleText_.isEmpty()) {
        paintBubble(&painter);
    }
}

void PetWindow::paintBubble(QPainter* painter)
{
    QFont font = this->font();
    font.setPointSizeF(qMax(9.0, width() / 26.0));
    const QFontMetricsF metrics(font);
    painter->setFont(font);

    const qreal maxTextWidth = width() * 0.72;
    const QRectF textBounds =
        metrics.boundingRect(QRectF(0, 0, maxTextWidth, 400), Qt::AlignCenter | Qt::TextWordWrap,
                             bubbleText_);
    const qreal padX = 14.0;
    const qreal padY = 8.0;
    const QSizeF boxSize(textBounds.width() + padX * 2.0, textBounds.height() + padY * 2.0);
    const QRectF box(QPointF((width() - boxSize.width()) / 2.0, 6.0), boxSize);

    QPainterPath path;
    path.addRoundedRect(box, 14.0, 14.0);
    QPainterPath tail;
    tail.moveTo(box.center().x() - 9.0, box.bottom() - 1.0);
    tail.lineTo(box.center().x() + 1.0, box.bottom() + 11.0);
    tail.lineTo(box.center().x() + 10.0, box.bottom() - 1.0);
    tail.closeSubpath();
    path = path.united(tail);

    qreal alpha = 1.0;
    if (bubbleStartedMs_ >= 0) {
        const qint64 shownMs = QDateTime::currentMSecsSinceEpoch() - bubbleStartedMs_;
        if (shownMs < 150) {
            alpha = shownMs / 150.0;
        } else if (bubbleTimer_.remainingTime() >= 0 && bubbleTimer_.remainingTime() < 220) {
            alpha = bubbleTimer_.remainingTime() / 220.0;
        }
    }

    painter->save();
    painter->setOpacity(qBound<qreal>(0.0, alpha, 1.0));
    painter->setPen(QPen(QColor(255, 255, 255, 200), 2.0));
    painter->setBrush(QColor(255, 255, 255, 236));
    painter->drawPath(path);
    painter->setPen(QColor(78, 58, 48));
    painter->drawText(box, Qt::AlignCenter | Qt::TextWordWrap, bubbleText_);
    painter->restore();
}

void PetWindow::onFrame()
{
    const qint64 now = frameClock_.elapsed();
    double dt = double(now - lastTickMs_) / 1000.0;
    lastTickMs_ = now;

    // 收进托盘时不重绘也不推进动画，别白烧 CPU
    if (!isVisible()) {
        return;
    }

    dt = qBound(0.0, dt, 0.1);
    if (skin_) {
        // 鼠标相对窗口中心的位置，Live2D 模型靠它转头/转眼球
        const QPointF local = mapFromGlobal(QCursor::pos());
        const qreal rx = (local.x() - width() * 0.5) / qMax<qreal>(1.0, width() * 0.5);
        const qreal ry = (local.y() - height() * 0.5) / qMax<qreal>(1.0, height() * 0.5);
        skin_->setPointerRelative(QPointF(rx, ry));
        skin_->tick(dt);
    }
    update();
}

void PetWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (!hitPet(event->position())) {
        event->ignore();
        return;
    }

    dragging_ = true;
    moved_ = false;
    dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    pressStartGlobal_ = event->globalPosition().toPoint();
    lastGlobal_ = pressStartGlobal_;
    lastMoveMs_ = frameClock_.elapsed();

    if (skin_) {
        skin_->setPressed(true);
    }
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PetWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint global = event->globalPosition().toPoint();
    if ((global - pressStartGlobal_).manhattanLength() > 3) {
        moved_ = true;
    }

    const qint64 now = frameClock_.elapsed();
    const double dt = qMax<qint64>(8, now - lastMoveMs_) / 1000.0;
    const qreal velocityX = (global.x() - lastGlobal_.x()) / dt;
    lastGlobal_ = global;
    lastMoveMs_ = now;

    if (skin_) {
        skin_->setDragTilt(velocityX * 0.02);
    }

    move(global - dragOffset_);
    event->accept();
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;
    if (skin_) {
        skin_->setPressed(false);
        skin_->setDragTilt(0.0);
    }
    setCursor(Qt::OpenHandCursor);

    if (moved_) {
        savePosition();
    } else {
        if (skin_) {
            skin_->triggerClick();
        }
        showBubble(randomBubbleLine());
    }
    event->accept();
}

void PetWindow::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        event->ignore();
        return;
    }
    resizePet(config_->windowWidth() + steps * 30);
    event->accept();
}

void PetWindow::enterEvent(QEnterEvent* event)
{
    if (skin_) {
        skin_->setHovered(true);
    }
    QWidget::enterEvent(event);
}

void PetWindow::leaveEvent(QEvent* event)
{
    if (skin_) {
        skin_->setHovered(false);
    }
    QWidget::leaveEvent(event);
}

void PetWindow::contextMenuEvent(QContextMenuEvent* event)
{
    if (!menu_) {
        return;
    }
    menu_->exec(event->globalPos());
    event->accept();
}

void PetWindow::closeEvent(QCloseEvent* event)
{
    if (quitting_) {
        event->accept();
        return;
    }
    // 点关闭只是躲起来，桌宠本身继续挂在托盘上
    hide();
    event->ignore();
}

void PetWindow::showEvent(QShowEvent* event)
{
    if (toggleAction_) {
        toggleAction_->setText(QStringLiteral("藏起来休息一下"));
    }
    QWidget::showEvent(event);
}

void PetWindow::hideEvent(QHideEvent* event)
{
    if (toggleAction_) {
        toggleAction_->setText(QStringLiteral("叫它出来"));
    }
    QWidget::hideEvent(event);
}

void PetWindow::showBubble(const QString& text)
{
    bubbleText_ = text;
    bubbleStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    bubbleTimer_.start();
    update();
}

QString PetWindow::randomBubbleLine() const
{
    const QStringList lines = bubbleLines();
    const int index = QRandomGenerator::global()->bounded(lines.size());
    return lines.at(index);
}

void PetWindow::resizePet(int width)
{
    const int target = qBound(160, width, 640);
    const QPoint bottomCenter(geometry().center().x(), geometry().bottom());

config_->setWindowWidth(target);
    resize(desiredWindowSize());

    QRect frame = geometry();
    frame.moveBottom(bottomCenter.y());
    frame.moveLeft(bottomCenter.x() - width / 2);
    move(frame.topLeft());
    savePosition();
}

void PetWindow::toggleVisible()
{
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
    }
}

void PetWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(iconPixmap_.isNull() ? windowIcon() : QIcon(iconPixmap_));
    tray_->setToolTip(QStringLiteral("CuteCat 桌宠"));
    menu_ = buildMenu();
    tray_->setContextMenu(menu_);
    connect(tray_, &QSystemTrayIcon::activated, this, &PetWindow::onTrayActivated);
    tray_->show();
}

QMenu* PetWindow::buildMenu()
{
    auto* menu = new QMenu(this);

    toggleAction_ = menu->addAction(QStringLiteral("藏起来休息一下"));
    connect(toggleAction_, &QAction::triggered, this, &PetWindow::toggleVisible);

    menu->addSeparator();

    auto* stayOnTop = menu->addAction(QStringLiteral("总在最前"));
    stayOnTop->setCheckable(true);
    stayOnTop->setChecked(config_->keepOnTop());
    connect(stayOnTop, &QAction::toggled, this, [this](bool checked) {
        if (config_->keepOnTop() == checked) {
            return;
        }
        config_->setKeepOnTop(checked);
        applyWindowFlags();
        config_->save();
    });

    auto* clickThrough = menu->addAction(QStringLiteral("鼠标穿透（从托盘菜单关闭）"));
    clickThrough->setCheckable(true);
    clickThrough->setChecked(config_->clickThrough());
    connect(clickThrough, &QAction::toggled, this, [this](bool checked) {
        if (config_->clickThrough() == checked) {
            return;
        }
        config_->setClickThrough(checked);
        setAttribute(Qt::WA_TransparentForMouseEvents, checked);
        if (checked) {
            unsetCursor();
        } else {
            setCursor(Qt::OpenHandCursor);
        }
        config_->save();
    });

    auto* sizeMenu = menu->addMenu(QStringLiteral("大小"));
    auto* sizeGroup = new QActionGroup(sizeMenu);
    sizeGroup->setExclusive(true);
    struct SizePreset
    {
        const char* label;
        int width;
    };
    const SizePreset presets[] = {{"小", 220}, {"中", 300}, {"大", 400}};
    for (const SizePreset& preset : presets) {
        auto* action = sizeMenu->addAction(QString::fromUtf8(preset.label));
        action->setCheckable(true);
        action->setChecked(qAbs(config_->windowSize().width() - preset.width) <= 10);
        sizeGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, preset] { resizePet(preset.width); });
    }

    auto* skinMenu = menu->addMenu(QStringLiteral("形象"));
    auto* skinGroup = new QActionGroup(skinMenu);
    skinGroup->setExclusive(true);

    auto* imageAction = skinMenu->addAction(QStringLiteral("橘猫（默认）"));
    imageAction->setCheckable(true);
    imageAction->setChecked(config_->animation() != QLatin1String("live2d"));
    skinGroup->addAction(imageAction);
    connect(imageAction, &QAction::triggered, this,
            [this] { switchSkin(QStringLiteral("image"), QString()); });

    // BongoCat 的模型目录扫描出来直接列进菜单
    const QStringList models = discoverLive2DModels();
    for (const QString& path : models) {
        const QString label = QFileInfo(path).dir().dirName();
        auto* action = skinMenu->addAction(QStringLiteral("BongoCat · %1").arg(label));
        action->setCheckable(true);
        action->setChecked(config_->animation() == QLatin1String("live2d")
                           && config_->live2DModel() == path);
        skinGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, path] { switchSkin(QStringLiteral("live2d"), path); });
    }

    auto* openModels = skinMenu->addAction(QStringLiteral("打开模型文件夹…"));
    connect(openModels, &QAction::triggered, this, [this] {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/models");
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    auto* remember = menu->addAction(QStringLiteral("记住位置"));
    remember->setCheckable(true);
    remember->setChecked(config_->rememberPosition());
    connect(remember, &QAction::toggled, this, [this](bool checked) {
        if (config_->rememberPosition() == checked) {
            return;
        }
        config_->setRememberPosition(checked);
        if (checked) {
            config_->setLastPosition(pos());
        }
        config_->save();
    });

    auto* autoStart = menu->addAction(QStringLiteral("开机自启"));
    autoStart->setCheckable(true);
    autoStart->setChecked(isAutoStartEnabled());
    connect(autoStart, &QAction::toggled, this, [this](bool checked) {
        setAutoStart(checked);
        config_->setAutoStart(checked);
        config_->save();
    });

    auto* reload = menu->addAction(QStringLiteral("重新加载形象"));
    connect(reload, &QAction::triggered, this, [this] {
        QString error;
        if (!loadSkin(&error)) {
            QMessageBox::warning(this, QStringLiteral("CuteCat"),
                                 QStringLiteral("形象加载失败：%1").arg(error));
        } else {
            update();
        }
    });

    auto* about = menu->addAction(QStringLiteral("关于 CuteCat"));
    connect(about, &QAction::triggered, this, &PetWindow::showAbout);

    menu->addSeparator();
    auto* quitAction = menu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, &PetWindow::quit);

    return menu;
}

void PetWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        toggleVisible();
    }
}

void PetWindow::showAbout()
{
    const QString backend = skin_ ? skin_->backendName() : QStringLiteral("unknown");
    QMessageBox::about(
        this, QStringLiteral("关于 CuteCat"),
        QStringLiteral("<h3>CuteCat 桌宠 %1</h3>"
                       "<p>Qt %2 · C++17 · CMake · QJson<br/>"
                       "形象后端：%3<br/>"
                       "配置文件：%4</p>"
                       "<p style='color:#888'>桌面上的小橘猫，点一下会有反应，按住可以拖来拖去。</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion()), backend,
                 QDir::toNativeSeparators(Config::userConfigPath())));
}

bool PetWindow::isAutoStartEnabled() const
{
    QSettings runKey(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    return runKey.contains(QString::fromLatin1(kAutoStartKey));
}

void PetWindow::setAutoStart(bool enabled)
{
    QSettings runKey(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    if (enabled) {
        const QString exePath =
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        runKey.setValue(QString::fromLatin1(kAutoStartKey),
                        QStringLiteral("\"%1\"").arg(exePath));
    } else {
        runKey.remove(QString::fromLatin1(kAutoStartKey));
    }
    runKey.sync();
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !hitPet(event->position())) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    if (skin_) {
        skin_->triggerWave();
    }
    showBubble(QStringLiteral("看我挥手~"));
    event->accept();
}

void PetWindow::switchSkin(const QString& animation, const QString& modelPath)
{
    config_->setAnimation(animation);
    config_->setLive2DModel(modelPath);
    config_->save();

    QString error;
    if (!loadSkin(&error)) {
        QMessageBox::warning(this, QStringLiteral("CuteCat"),
                             QStringLiteral("形象切换失败：%1").arg(error));
        return;
    }
    if (skin_) {
        showBubble(QStringLiteral("嘿，换了个样子~"));
    }
}

void PetWindow::quit()
{
    quitting_ = true;
    savePosition();
    config_->save();
    if (tray_) {
        tray_->hide();
    }
    QApplication::quit();
}