#include "darktoast.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QFontMetrics>
#include <QPushButton>
#include <QTimer>

DarkToast::DarkToast(QWidget *parent, const QString &title, const QString &message, Theme theme,
                   const QString &accent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    // 不用 WA_TranslucentBackground：嵌入式平台透明窗口叠加会残留上一弹窗背景。
    // 且不使用 exec 模态：触摸屏上模态子窗口可能收不到点击导致"卡死"，
    // 改为非模态 + 定时自动消失（见 showInformation）。

    // Compact size that never covers the whole panel.
    // 宽度固定；高度按内容自适应（两行消息不再被截断），最低 130
    setFixedWidth(280);
    setMinimumHeight(130);

    const bool dark = (theme == Dark);
    const QString bg = dark ? "#0d1b2a" : "#ffffff";
    const QString border = dark ? "#1b3a4b" : "#d0d8e2";
    const QString titleColor = dark ? "#f8fafc" : "#1a2332";
    const QString msgColor = dark ? "#94a3b8" : "#5a6b82";
    // 主按钮与圆点使用主题强调色（未传时回落为默认蓝）
    const QString accent0 = accent.isEmpty() ? (dark ? QStringLiteral("#0ea5e9") : QStringLiteral("#0077be")) : accent;
    const QString btnBg = accent0;
    const QString btnHover = accent0;
    const QString btnPressed = accent0;
    const QString dotColor = accent0;

    QWidget *card = new QWidget(this);
    card->setObjectName("darkToastCard");
    // 对话框自身背景与卡片同色，避免透明合成
    setStyleSheet(QString("QDialog { background-color: %1; }").arg(bg));
    card->setStyleSheet(
        QString(
            "QWidget#darkToastCard {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
            "  font-family: \"Microsoft YaHei\", \"PingFang SC\", sans-serif;"
            "}"
            "QLabel#toastDot {"
            "  background-color: %3;"
            "  border-radius: 4px;"
            "  min-width: 8px;"
            "  max-width: 8px;"
            "  min-height: 8px;"
            "  max-height: 8px;"
            "}"
            "QLabel#toastTitle {"
            "  color: %4;"
            "  font-size: 16px;"
            "  font-weight: bold;"
            "}"
            "QLabel#toastMessage {"
            "  color: %5;"
            "  font-size: 14px;"
            "  line-height: 140%;"
            "}"
            "QPushButton#toastOk {"
            "  background-color: %6;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 6px 22px;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "  min-width: 80px;"
            "}"
            "QPushButton#toastOk:hover {"
            "  background-color: %7;"
            "}"
            "QPushButton#toastOk:pressed {"
            "  background-color: %8;"
            "}")
            .arg(bg, border, dotColor, titleColor, msgColor, btnBg, btnHover, btnPressed)
    );

    // 阴影已移除：QGraphicsDropShadowEffect 在嵌入式软件渲染下开销大且可能引起残留
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(card);   // 卡片铺满对话框：边框贴边，颜色不再悬空在边框外

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 16);
    cardLayout->setSpacing(12);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);
    titleLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QLabel *dot = new QLabel(card);
    dot->setObjectName("toastDot");

    QLabel *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("toastTitle");

    titleLayout->addWidget(dot);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    QLabel *messageLabel = new QLabel(message, card);
    messageLabel->setObjectName("toastMessage");
    messageLabel->setWordWrap(true);
    messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    QPushButton *okBtn = new QPushButton(tr("知道了"), card);
    okBtn->setObjectName("toastOk");
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(okBtn);

    cardLayout->addLayout(titleLayout);
    cardLayout->addWidget(messageLabel, 1);
    cardLayout->addLayout(btnLayout);

    // 高度手工计算：wordWrap 的 sizeHint 不含换行结果，多行消息会被截断。
    // 字号按 QSS（标题 16px / 正文 14px），行距按 1.4 估算，宁大勿小。
    QFont msgFont = messageLabel->font();
    msgFont.setPixelSize(14);
    QFontMetrics msgFm(msgFont);
    const int innerW = 280 - 20 - 20;                 // cardLayout 左右 margins
    const QRect wrapRect = msgFm.boundingRect(QRect(0, 0, innerW, 4096),
                                              Qt::TextWordWrap | Qt::AlignLeft, message);
    const int lines = qMax(1, (wrapRect.height() + msgFm.lineSpacing() - 1)
                                  / msgFm.lineSpacing());
    const int msgH = lines * 20 + 6;                  // 14px x 1.4 ≈ 20px/行
    QFont titleFont = titleLabel->font();
    titleFont.setPixelSize(16);
    titleFont.setBold(true);
    const int titleH = QFontMetrics(titleFont).height();
    const int btnH = qMax(okBtn->sizeHint().height(), 36);
    const int total = 18 + titleH + 12 + msgH + 12 + btnH + 16 + 2; // 上下margins+间距+边框
    setFixedHeight(qMax(130, total));
}

void DarkToast::showInformation(QWidget *parent, const QString &title, const QString &message,
                                Theme theme, const QString &accent)
{
    DarkToast *dlg = new DarkToast(parent, title, message, theme, accent);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);

    if (parent) {
        // Use the parent's client geometry (not frameGeometry) so the dialog
        // is centred inside the application window on embedded panels too.
        QRect pr = parent->geometry();
        dlg->move(pr.left() + (pr.width() - dlg->width()) / 2,
                  pr.top() + (pr.height() - dlg->height()) / 2);
    }

    // 非模态显示，2.5 秒后自动消失：不阻塞主流程，也避免模态窗口
    // 在触摸屏上收不到点击导致的"卡死"。
    dlg->show();
    QTimer::singleShot(2500, dlg, &QDialog::close);
}
