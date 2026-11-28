#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidget>

#include <memory>

#include "animation/pet_skin.h"

class Config;
class QAction;
class QMenu;
class KeyboardWatcher;

// 桌宠窗口：透明、无边框、置顶，负责鼠标交互、右键菜单、托盘和配置持久化。
// 形象与动画全部交给 PetSkin，窗口本身不关心用的是图片还是 Lottie。
class PetWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PetWindow(Config* config, QWidget* parent = nullptr);
    ~PetWindow() override;

    bool initialize(QString* error);

    void showBubble(const QString& text);
    void quit();

public slots:
    void toggleVisible();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QRectF petRect() const;
    bool hitPet(const QPointF& position) const;
    void paintBubble(QPainter* painter);

    bool loadSkin(QString* error);
    void applyWindowFlags();
    void applyAppearance();
    void restorePosition();
    void savePosition();

    void setupTray();
    QMenu* buildMenu();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showAbout();
    void resizePet(int width);
    QString randomBubbleLine() const;

    bool isAutoStartEnabled() const;
    void setAutoStart(bool enabled);

    void onFrame();
    void switchSkin(const QString& animation, const QString& modelPath);

    Config* config_ = nullptr;
    std::unique_ptr<PetSkin> skin_;
    std::unique_ptr<KeyboardWatcher> keyboard_;
    QPixmap iconPixmap_;

    QTimer frameTimer_;
    QTimer bubbleTimer_;
    QElapsedTimer frameClock_;
    qint64 lastTickMs_ = 0;

    bool dragging_ = false;
    bool moved_ = false;
    QPoint dragOffset_;
    QPoint pressStartGlobal_;
    QPoint lastGlobal_;
    qint64 lastMoveMs_ = 0;

    QMenu* menu_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* toggleAction_ = nullptr;

    QString bubbleText_;
    qint64 bubbleStartedMs_ = -1;

    bool quitting_ = false;
};