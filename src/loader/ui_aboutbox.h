/********************************************************************************
** Form generated from reading UI file 'aboutbox.ui'
**
** Created: Sat Jan 26 01:23:00 2013
**      by: Qt User Interface Compiler version 4.7.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ABOUTBOX_H
#define UI_ABOUTBOX_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QDialog>
#include <QtGui/QDialogButtonBox>
#include <QtGui/QFrame>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QSpacerItem>
#include <QtGui/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_AboutBoxClass
{
public:
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QLabel *lblName;
    QLabel *lblLogo;
    QLabel *lblVersion;
    QHBoxLayout *horizontalLayout_2;
    QLabel *lblHomePage;
    QSpacerItem *horizontalSpacer;
    QLabel *lblSourcePage;
    QLabel *lblCopyright;
    QFrame *frame;
    QDialogButtonBox *btbButtons;

    void setupUi(QDialog *AboutBoxClass)
    {
        if (AboutBoxClass->objectName().isEmpty())
            AboutBoxClass->setObjectName(QString::fromUtf8("AboutBoxClass"));
        AboutBoxClass->resize(296, 152);
        verticalLayout = new QVBoxLayout(AboutBoxClass);
        verticalLayout->setSpacing(6);
        verticalLayout->setContentsMargins(5, 5, 5, 5);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setSizeConstraint(QLayout::SetFixedSize);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        lblName = new QLabel(AboutBoxClass);
        lblName->setObjectName(QString::fromUtf8("lblName"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(lblName->sizePolicy().hasHeightForWidth());
        lblName->setSizePolicy(sizePolicy);
        QPalette palette;
        QBrush brush(QColor(170, 0, 0, 255));
        brush.setStyle(Qt::SolidPattern);
        palette.setBrush(QPalette::Active, QPalette::WindowText, brush);
        palette.setBrush(QPalette::Inactive, QPalette::WindowText, brush);
        QBrush brush1(QColor(120, 120, 120, 255));
        brush1.setStyle(Qt::SolidPattern);
        palette.setBrush(QPalette::Disabled, QPalette::WindowText, brush1);
        lblName->setPalette(palette);
        QFont font;
        font.setPointSize(18);
        lblName->setFont(font);
        lblName->setText(QString::fromUtf8("Vacuum Chat"));

        horizontalLayout->addWidget(lblName);

        lblLogo = new QLabel(AboutBoxClass);
        lblLogo->setObjectName(QString::fromUtf8("lblLogo"));
        lblLogo->setTextFormat(Qt::PlainText);
        lblLogo->setAlignment(Qt::AlignCenter);

        horizontalLayout->addWidget(lblLogo);


        verticalLayout->addLayout(horizontalLayout);

        lblVersion = new QLabel(AboutBoxClass);
        lblVersion->setObjectName(QString::fromUtf8("lblVersion"));
        lblVersion->setText(QString::fromUtf8("Version: 2.1.0"));
        lblVersion->setTextFormat(Qt::PlainText);

        verticalLayout->addWidget(lblVersion);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(6);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        lblHomePage = new QLabel(AboutBoxClass);
        lblHomePage->setObjectName(QString::fromUtf8("lblHomePage"));
        lblHomePage->setText(QString::fromUtf8("<a href='https://github.com/andreaspeters/vacuum-chat'>https://github.com/andreaspeters/vacuum-chat</a>"));
        lblHomePage->setTextFormat(Qt::RichText);
        lblHomePage->setAlignment(Qt::AlignLeading|Qt::AlignLeft|Qt::AlignVCenter);

        horizontalLayout_2->addWidget(lblHomePage);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer);

        lblSourcePage = new QLabel(AboutBoxClass);
        lblSourcePage->setObjectName(QString::fromUtf8("lblSourcePage"));
        lblSourcePage->setText(QString::fromUtf8("<a href='https://github.com/andreaspeters/vacuum-chat'>https://github.com/andreaspeters/vacuum-chat</a>"));
        lblSourcePage->setTextFormat(Qt::RichText);
        lblSourcePage->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        horizontalLayout_2->addWidget(lblSourcePage);


        verticalLayout->addLayout(horizontalLayout_2);

        lblCopyright = new QLabel(AboutBoxClass);
        lblCopyright->setObjectName(QString::fromUtf8("lblCopyright"));
        lblCopyright->setText(QString::fromUtf8("This software is released under the terms of the GNU General Public License version 3."));
        lblCopyright->setTextFormat(Qt::PlainText);
        lblCopyright->setWordWrap(true);

        verticalLayout->addWidget(lblCopyright);

        frame = new QFrame(AboutBoxClass);
        frame->setObjectName(QString::fromUtf8("frame"));
        frame->setFrameShape(QFrame::HLine);
        frame->setFrameShadow(QFrame::Raised);

        verticalLayout->addWidget(frame);

        btbButtons = new QDialogButtonBox(AboutBoxClass);
        btbButtons->setObjectName(QString::fromUtf8("btbButtons"));
        btbButtons->setStandardButtons(QDialogButtonBox::Ok);

        verticalLayout->addWidget(btbButtons);


        retranslateUi(AboutBoxClass);
        QObject::connect(btbButtons, SIGNAL(accepted()), AboutBoxClass, SLOT(accept()));

        QMetaObject::connectSlotsByName(AboutBoxClass);
    } // setupUi

    void retranslateUi(QDialog *AboutBoxClass)
    {
        AboutBoxClass->setWindowTitle(QApplication::translate("AboutBoxClass", "About Vacuum Chat", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class AboutBoxClass: public Ui_AboutBoxClass {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ABOUTBOX_H
