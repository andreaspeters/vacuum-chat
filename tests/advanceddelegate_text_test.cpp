#include <utils/advanceditemdelegate.h>

#include <QApplication>
#include <QStyleOptionViewItem>

#include <iostream>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    AdvancedItemDelegate delegate(nullptr);
    AdvancedDelegateItem item(AdvancedDelegateItem::DisplayId);
    item.d->kind = AdvancedDelegateItem::Display;
    item.c->value = QStringLiteral("  alpha\nbeta") + QChar(QChar::LineSeparator)
        + QStringLiteral("gamma") + QChar(QChar::ParagraphSeparator)
        + QStringLiteral("delta  ");

    const QStyleOptionViewItem option;
    const QString actual = delegate.itemStyleOption(item, option).text;
    const QString expected = QStringLiteral("alpha beta gamma delta");
    if (actual != expected) {
        std::cerr << "delegate display text was not flattened and trimmed\n";
        return 1;
    }
    return 0;
}
