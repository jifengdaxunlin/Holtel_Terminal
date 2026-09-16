#ifndef SOFTKEYBOARD_H
#define SOFTKEYBOARD_H

#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QGridLayout;

// ---------------------------------------------------------------------------
// 简易屏幕软键盘
// 用途：ARM 板是触摸屏设备、没有物理键盘，QLineEdit 收不到按键事件，
//       靠这个软键盘把字符 insert() 进目标输入框。
// ---------------------------------------------------------------------------
class SoftKeyboard : public QWidget
{
    Q_OBJECT

public:
    explicit SoftKeyboard(QWidget *parent = 0);

    void setTarget(QLineEdit *edit);
    QLineEdit *target() const { return m_target; }

signals:
    void done();   // 用户点了「完成」/「收起」

private slots:
    void onKeyClicked();
    void onBackspace();
    void onClear();
    void onSpace();
    void onShift();
    void onSwitchPage();
    void onDone();

private:
    void buildUi();
    void rebuildKeys();
    void updatePreview();
    void insertText(const QString &t);

    QLineEdit   *m_target;
    QLabel      *m_preview;
    QGridLayout *m_grid;
    bool         m_shift;     // 大写锁定
    bool         m_symbols;   // 当前是否符号页
};

#endif // SOFTKEYBOARD_H
