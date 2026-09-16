#include "softkeyboard.h"

#include <QLineEdit>
#include <QLabel>
#include <QFontMetrics>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayoutItem>

SoftKeyboard::SoftKeyboard(QWidget *parent)
    : QWidget(parent)
    , m_target(0)
    , m_preview(0)
    , m_grid(0)
    , m_shift(false)
    , m_symbols(false)
{
    setObjectName(QStringLiteral("softKbd"));
    // 自定义 QWidget 子类必须开启这个属性，样式表里的 background-color 才会生效，
    // 否则键盘是透明的，底下的列表文字会透出来干扰视线
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    rebuildKeys();
    updatePreview();

    // 键盘自身样式（自包含：挂到顶层窗口后仍生效）
    // 背景用明显渐变与页面区分；普通键中蓝、功能键暗蓝、完成/收起强调青、退格红
    setStyleSheet(QStringLiteral(
        "QWidget#softKbd {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1, stop:0 #16325c, stop:1 #0a1a33);"
        "  border: 2px solid #2a5a8a;"
        "  border-radius: 14px;"
        "  font-family: \"Microsoft YaHei\", \"PingFang SC\", sans-serif;"
        "}"
        "QLabel#kbdPreview {"
        "  background: rgba(0, 0, 0, 0.45); color: #d8e6ff; font-size: 15px;"
        "  border: 1px solid #2a5a8a; border-radius: 8px; padding: 4px 10px;"
        "  min-height: 38px; }"
        "QPushButton#kbdKey {"
        "  background: #1e4068; color: #ffffff; border: 1px solid #2a5a8a; border-radius: 8px;"
        "  font-size: 17px; min-height: 44px;"
        "}"
        "QPushButton#kbdClear, QPushButton#kbdShift, QPushButton#kbdPage, QPushButton#kbdSpace {"
        "  background: #0d2b52; color: #cfe3ff; border: 1px solid #2a5a8a; border-radius: 8px;"
        "  font-size: 15px; min-height: 44px;"
        "}"
        "QPushButton#kbdHide, QPushButton#kbdDone {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #00d4ff, stop:1 #0077be);"
        "  color: #ffffff; font-weight: bold; border: none; border-radius: 8px;"
        "  font-size: 16px; min-height: 44px;"
        "}"
        "QPushButton#kbdBackspace {"
        "  background: #c0392b; color: #ffffff; border: none; border-radius: 8px;"
        "  font-size: 15px; min-height: 44px;"
        "}"
        "QPushButton#kbdShiftOn {"
        "  background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #00d4ff, stop:1 #0077be);"
        "  color: #ffffff; font-weight: bold; border: none; border-radius: 8px;"
        "  font-size: 15px; min-height: 44px;"
        "}"
        "QPushButton#kbdKey:pressed, QPushButton#kbdClear:pressed,"
        "QPushButton#kbdHide:pressed, QPushButton#kbdShift:pressed,"
        "QPushButton#kbdShiftOn:pressed, QPushButton#kbdBackspace:pressed,"
        "QPushButton#kbdPage:pressed, QPushButton#kbdSpace:pressed,"
        "QPushButton#kbdDone:pressed { opacity: 0.85; }"));
}

void SoftKeyboard::setTarget(QLineEdit *edit)
{
    if (m_target == edit)
        return;
    if (m_target)
        disconnect(m_target, &QLineEdit::textChanged, this, &SoftKeyboard::updatePreview);
    m_target = edit;
    if (m_target)
        connect(m_target, &QLineEdit::textChanged, this, &SoftKeyboard::updatePreview);
    updatePreview();
}

void SoftKeyboard::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 3, 4, 4);   // 边距收窄，把空间尽量留给按键
    root->setSpacing(4);

    // ---- 顶部：输入显示条（独占一整行、占满键盘宽度，不再用密码打码格式）----
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("kbdPreview"));
    m_preview->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_preview->setMinimumHeight(38);
    root->addWidget(m_preview);

    // ---- 按键区（清空/收起移到底部按键行）----
    m_grid = new QGridLayout();
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(4);
    root->addLayout(m_grid, 1);
}

// 清空并重建所有按键（切页 / 切换大小写时调用）
void SoftKeyboard::rebuildKeys()
{
    while (QLayoutItem *it = m_grid->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();          // 先隐藏，避免 deleteLater 前残留在界面上
            w->deleteLater();
        }
        delete it;
    }

    for (int c = 0; c < 10; ++c)
        m_grid->setColumnStretch(c, 1);
    for (int r = 0; r < 5; ++r)
        m_grid->setRowStretch(r, 1);

    // ---- 第 0 行：数字（两页共用）----
    const QString digits = QStringLiteral("1234567890");
    for (int i = 0; i < digits.size(); ++i) {
        QPushButton *b = new QPushButton(QString(digits.at(i)), this);
        b->setObjectName(QStringLiteral("kbdKey"));
        b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("kchar", QString(digits.at(i)));
        connect(b, &QPushButton::clicked, this, &SoftKeyboard::onKeyClicked);
        m_grid->addWidget(b, 0, i, 1, 1);
    }

    if (!m_symbols) {
        // ---- 字母页 ----
        const QString r1 = QStringLiteral("qwertyuiop");
        const QString r2 = QStringLiteral("asdfghjkl");
        const QString r3 = QStringLiteral("zxcvbnm");

        for (int i = 0; i < r1.size(); ++i) {
            const QString ch = m_shift ? r1.mid(i, 1).toUpper() : r1.mid(i, 1);
            QPushButton *b = new QPushButton(ch, this);
            b->setObjectName(QStringLiteral("kbdKey"));
            b->setFocusPolicy(Qt::NoFocus);
            b->setProperty("kchar", ch);
            connect(b, &QPushButton::clicked, this, &SoftKeyboard::onKeyClicked);
            m_grid->addWidget(b, 1, i, 1, 1);
        }
        // a..k 放在 0..7，l 跨 2 列填满
        for (int i = 0; i < r2.size(); ++i) {
            const QString ch = m_shift ? r2.mid(i, 1).toUpper() : r2.mid(i, 1);
            QPushButton *b = new QPushButton(ch, this);
            b->setObjectName(QStringLiteral("kbdKey"));
            b->setFocusPolicy(Qt::NoFocus);
            b->setProperty("kchar", ch);
            connect(b, &QPushButton::clicked, this, &SoftKeyboard::onKeyClicked);
            const int span = (i == r2.size() - 1) ? 2 : 1;   // 最后一个 l 跨两列
            m_grid->addWidget(b, 2, i, 1, span);
        }

        // 第 3 行：[大小写 span2] z x c v b n m [退格 span2]
        QPushButton *shift = new QPushButton(m_shift ? QStringLiteral("小写")
                                                     : QStringLiteral("大写"), this);
        shift->setObjectName(m_shift ? QStringLiteral("kbdShiftOn") : QStringLiteral("kbdShift"));
        shift->setFocusPolicy(Qt::NoFocus);
        connect(shift, &QPushButton::clicked, this, &SoftKeyboard::onShift);
        m_grid->addWidget(shift, 3, 0, 1, 2);

        for (int i = 0; i < r3.size(); ++i) {
            const QString ch = m_shift ? r3.mid(i, 1).toUpper() : r3.mid(i, 1);
            QPushButton *b = new QPushButton(ch, this);
            b->setObjectName(QStringLiteral("kbdKey"));
            b->setFocusPolicy(Qt::NoFocus);
            b->setProperty("kchar", ch);
            connect(b, &QPushButton::clicked, this, &SoftKeyboard::onKeyClicked);
            m_grid->addWidget(b, 3, 2 + i, 1, 1);
        }

        QPushButton *bs = new QPushButton(QStringLiteral("退格"), this);
        bs->setObjectName(QStringLiteral("kbdBackspace"));
        bs->setFocusPolicy(Qt::NoFocus);
        connect(bs, &QPushButton::clicked, this, &SoftKeyboard::onBackspace);
        m_grid->addWidget(bs, 3, 8, 1, 2);

        // 第 4 行：[清空 2][符号 2][空格 2][完成 2][收起 2]（清空/收起移入按键行，顶行整体留给输入显示条）
        QPushButton *clearBtn = new QPushButton(QStringLiteral("清空"), this);
        clearBtn->setObjectName(QStringLiteral("kbdClear"));
        clearBtn->setFocusPolicy(Qt::NoFocus);
        connect(clearBtn, &QPushButton::clicked, this, &SoftKeyboard::onClear);
        m_grid->addWidget(clearBtn, 4, 0, 1, 2);

        QPushButton *page = new QPushButton(QStringLiteral("符号"), this);
        page->setObjectName(QStringLiteral("kbdPage"));
        page->setFocusPolicy(Qt::NoFocus);
        connect(page, &QPushButton::clicked, this, &SoftKeyboard::onSwitchPage);
        m_grid->addWidget(page, 4, 2, 1, 2);

        QPushButton *sp = new QPushButton(QStringLiteral("空格"), this);
        sp->setObjectName(QStringLiteral("kbdSpace"));
        sp->setFocusPolicy(Qt::NoFocus);
        connect(sp, &QPushButton::clicked, this, &SoftKeyboard::onSpace);
        m_grid->addWidget(sp, 4, 4, 1, 2);

        QPushButton *ok = new QPushButton(QStringLiteral("完成"), this);
        ok->setObjectName(QStringLiteral("kbdDone"));
        ok->setFocusPolicy(Qt::NoFocus);
        connect(ok, &QPushButton::clicked, this, &SoftKeyboard::onDone);
        m_grid->addWidget(ok, 4, 6, 1, 2);

        QPushButton *hideBtn = new QPushButton(QStringLiteral("收起"), this);
        hideBtn->setObjectName(QStringLiteral("kbdHide"));
        hideBtn->setFocusPolicy(Qt::NoFocus);
        connect(hideBtn, &QPushButton::clicked, this, &SoftKeyboard::onDone);
        m_grid->addWidget(hideBtn, 4, 8, 1, 2);
    } else {
        // ---- 符号页 ----
        const char *rows[3] = { "!@#$%^&*()",
                                "-_=+[]{}\\|",
                                ";:'\",./?<>" };
        for (int r = 0; r < 3; ++r) {
            const QString row = QString::fromLatin1(rows[r]);
            for (int i = 0; i < row.size(); ++i) {
                const QString ch = QString(row.at(i));
                QPushButton *b = new QPushButton(ch, this);
                b->setObjectName(QStringLiteral("kbdKey"));
                b->setFocusPolicy(Qt::NoFocus);
                b->setProperty("kchar", ch);
                connect(b, &QPushButton::clicked, this, &SoftKeyboard::onKeyClicked);
                m_grid->addWidget(b, r + 1, i, 1, 1);
            }
        }

        // 第 4 行：[清空 2][字母 2][空格 2][完成 2][收起 2]（清空/收起移入按键行，顶行整体留给输入显示条）
        QPushButton *clearBtn = new QPushButton(QStringLiteral("清空"), this);
        clearBtn->setObjectName(QStringLiteral("kbdClear"));
        clearBtn->setFocusPolicy(Qt::NoFocus);
        connect(clearBtn, &QPushButton::clicked, this, &SoftKeyboard::onClear);
        m_grid->addWidget(clearBtn, 4, 0, 1, 2);

        QPushButton *page = new QPushButton(QStringLiteral("字母"), this);
        page->setObjectName(QStringLiteral("kbdPage"));
        page->setFocusPolicy(Qt::NoFocus);
        connect(page, &QPushButton::clicked, this, &SoftKeyboard::onSwitchPage);
        m_grid->addWidget(page, 4, 2, 1, 2);

        QPushButton *sp = new QPushButton(QStringLiteral("空格"), this);
        sp->setObjectName(QStringLiteral("kbdSpace"));
        sp->setFocusPolicy(Qt::NoFocus);
        connect(sp, &QPushButton::clicked, this, &SoftKeyboard::onSpace);
        m_grid->addWidget(sp, 4, 4, 1, 2);

        QPushButton *ok = new QPushButton(QStringLiteral("完成"), this);
        ok->setObjectName(QStringLiteral("kbdDone"));
        ok->setFocusPolicy(Qt::NoFocus);
        connect(ok, &QPushButton::clicked, this, &SoftKeyboard::onDone);
        m_grid->addWidget(ok, 4, 6, 1, 2);

        QPushButton *hideBtn = new QPushButton(QStringLiteral("收起"), this);
        hideBtn->setObjectName(QStringLiteral("kbdHide"));
        hideBtn->setFocusPolicy(Qt::NoFocus);
        connect(hideBtn, &QPushButton::clicked, this, &SoftKeyboard::onDone);
        m_grid->addWidget(hideBtn, 4, 8, 1, 2);
    }
}

void SoftKeyboard::insertText(const QString &t)
{
    if (!m_target)
        return;
    m_target->insert(t);
    updatePreview();
}

void SoftKeyboard::updatePreview()
{
    if (!m_preview)
        return;
    if (!m_target) {
        m_preview->clear();
        return;
    }
    const QString text = m_target->text();
    // 预览不再区分密码/普通输入：始终显示真实输入内容（用户要求去掉打码格式）
    QString shown = text;
    if (shown.size() > 64)                     // 先按字符数粗截，防极端超长
        shown = QStringLiteral("…") + shown.right(64);
    // 再按像素收尾：显示条占满键盘宽度（560 宽键盘内约 500px 文本预算）
    QFontMetrics fm(m_preview->font());
    while (shown.size() > 1 && fm.width(shown) > 500)
        shown.remove(0, 1);
    if (shown != text && !shown.startsWith(QStringLiteral("…")))
        shown = QStringLiteral("…") + shown;
    m_preview->setText(shown.isEmpty() ? QStringLiteral("（空）") : shown);
}

// ------------------------------- 槽 -------------------------------
void SoftKeyboard::onKeyClicked()
{
    QPushButton *b = qobject_cast<QPushButton *>(sender());
    if (!b)
        return;
    const QString ch = b->property("kchar").toString();
    if (!ch.isEmpty())
        insertText(ch);
}

void SoftKeyboard::onBackspace()
{
    if (m_target)
        m_target->backspace();
    updatePreview();
}

void SoftKeyboard::onClear()
{
    if (m_target)
        m_target->clear();
    updatePreview();
}

void SoftKeyboard::onSpace()
{
    insertText(QStringLiteral(" "));
}

void SoftKeyboard::onShift()
{
    m_shift = !m_shift;
    rebuildKeys();
}

void SoftKeyboard::onSwitchPage()
{
    m_symbols = !m_symbols;
    rebuildKeys();
}

void SoftKeyboard::onDone()
{
    emit done();
}
