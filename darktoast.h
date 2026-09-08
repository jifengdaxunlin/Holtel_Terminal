#ifndef DARKTOAST_H
#define DARKTOAST_H

#include <QDialog>

class DarkToast : public QDialog
{
    Q_OBJECT

public:
    enum Theme { Dark, Light };

    explicit DarkToast(QWidget *parent, const QString &title, const QString &message, Theme theme = Dark);

    static void showInformation(QWidget *parent, const QString &title, const QString &message, Theme theme = Dark);
};

#endif // DARKTOAST_H