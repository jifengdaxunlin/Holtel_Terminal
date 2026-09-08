#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QDateTime>
#include <QIcon>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onTimeUpdate();
    void onNavHome();
    void onNavRoom();
    void onNavCheckin();
    void onNavService();
    void onNavSettings();
    void onAcSliderChanged(int value);
    void onAcMinus();
    void onAcPlus();
    void onDndToggled(bool checked);
    void onCleanToggled(bool checked);
    void onDoorPressed();
    void onDoorReleased();
    void onLightToggled(bool checked);
    void onSceneToggled(bool checked);
    void onCallToggled(bool checked);
    void onServiceClicked();
    void onInfoClicked();
    void onPlayClicked();
    void onReadIdClicked();
    void onStartFaceClicked();
    void onConfirmCheckinClicked();
    void onPrintClicked();
    void onCardAuthClicked();

    void onThemeDark();
    void onThemeLight();

private:
    enum class Theme { Dark, Light };

    Ui::MainWindow *ui;
    QTimer *m_clockTimer;
    bool m_isPlaying = false;
    Theme m_theme = Theme::Dark;

    void setupConnections();
    void setupIcons();
    void applyButtonStyles();
    void applyTheme();
    QString darkStyleSheet() const;
    QString lightStyleSheet() const;
    QString currentDateTimeString() const;

    void showToast(const QString &title, const QString &message);
};
#endif // MAINWINDOW_H
