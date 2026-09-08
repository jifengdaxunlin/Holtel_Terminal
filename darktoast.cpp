#include "darktoast.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>

DarkToast::DarkToast(QWidget *parent, const QString &title, const QString &message, Theme theme)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    // Compact size that never covers the whole panel.
    setFixedSize(280, 130);

    const bool dark = (theme == Dark);
    const QString bg = dark ? "#0d1b2a" : "#ffffff";
    const QString border = dark ? "#1b3a4b" : "#d0d8e2";
    const QString titleColor = dark ? "#f8fafc" : "#1a2332";
    const QString msgColor = dark ? "#94a3b8" : "#5a6b82";
    const QString btnBg = dark ? "#0ea5e9" : "#0077be";
    const QString btnHover = dark ? "#38bdf8" : "#3394d4";
    const QString btnPressed = dark ? "#0284c7" : "#005f8f";
    const QString dotColor = dark ? "#0ea5e9" : "#0077be";

    QWidget *card = new QWidget(this);
    card->setObjectName("darkToastCard");
    card->setStyleSheet(
        QString(
            "QWidget#darkToastCard {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 12px;"
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
            "  font-size: 13px;"
            "  line-height: 140%;"
            "}"
            "QPushButton#toastOk {"
            "  background-color: %6;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 6px 22px;"
            "  font-size: 13px;"
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

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, dark ? 170 : 80));
    shadow->setOffset(0, 8);
    card->setGraphicsEffect(shadow);

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(card, 0, Qt::AlignCenter);

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
}

void DarkToast::showInformation(QWidget *parent, const QString &title, const QString &message, Theme theme)
{
    DarkToast dlg(parent, title, message, theme);

    if (parent) {
        // Use the parent's client geometry (not frameGeometry) so the dialog
        // is centred inside the application window on embedded panels too.
        QRect pr = parent->geometry();
        dlg.move(pr.left() + (pr.width() - dlg.width()) / 2,
                 pr.top() + (pr.height() - dlg.height()) / 2);
    }

    dlg.exec();
}
