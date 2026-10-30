#include <utils/advanceditemdelegate.h>

#include <QApplication>
#include <QDataStream>
#include <QImage>
#include <QKeyEvent>
#include <QIODevice>
#include <QLineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QStandardItem>
#include <QStandardItemModel>

#include <iostream>

class TestAdvancedItemDelegate : public AdvancedItemDelegate
{
public:
    explicit TestAdvancedItemDelegate(QObject *parent) : AdvancedItemDelegate(parent) {}
    using AdvancedItemDelegate::editorEvent;
};

class FocusTrackingStyle : public QProxyStyle
{
public:
    FocusTrackingStyle() : focusRectCalls(0), backgroundCalls(0), textCalls(0),
                           branchIndicatorCalls(0), checkIndicatorCalls(0) {}

    void drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption *option,
            QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element == QStyle::PE_FrameFocusRect)
            ++focusRectCalls;
        if (element == QStyle::PE_PanelItemViewItem)
            ++backgroundCalls;
        if (element == QStyle::PE_IndicatorBranch)
            ++branchIndicatorCalls;
        if (element == QStyle::PE_IndicatorItemViewItemCheck)
            ++checkIndicatorCalls;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawItemText(QPainter *painter, const QRect &rect, int flags, const QPalette &palette,
                      bool enabled, const QString &text, QPalette::ColorRole textRole) const override
    {
        ++textCalls;
        QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, text, textRole);
    }

    mutable int focusRectCalls;
    mutable int backgroundCalls;
    mutable int textCalls;
    mutable int branchIndicatorCalls;
    mutable int checkIndicatorCalls;
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    AdvancedDelegateItem original(AdvancedDelegateItem::DisplayId);
    if (original.d->showStates != QStyle::State()
            || original.d->hideStates != QStyle::State()) {
        std::cerr << "new item has non-empty state flags\n";
        return 1;
    }
    original.d->kind = AdvancedDelegateItem::Display;
    AdvancedDelegateItem copy(original);
    copy.detach();
    copy.d->kind = AdvancedDelegateItem::Stretch;
    if (original.d->kind != AdvancedDelegateItem::Display
            || copy.d->kind != AdvancedDelegateItem::Stretch) {
        std::cerr << "detach did not preserve copy-on-write semantics\n";
        return 1;
    }

    const quint32 id = AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleRight, 42, 1234);
    if (AdvancedDelegateItem::getPosition(id) != AdvancedDelegateItem::MiddleRight
            || AdvancedDelegateItem::getFloor(id) != 42
            || AdvancedDelegateItem::getOrder(id) != 1234) {
        std::cerr << "delegate item id fields did not round-trip\n";
        return 1;
    }

    const quint32 stretchId = AdvancedDelegateItem::makeStretchId(AdvancedDelegateItem::Top, 7);
    if (AdvancedDelegateItem::getPosition(stretchId) != AdvancedDelegateItem::Top
            || AdvancedDelegateItem::getFloor(stretchId) != 7
            || AdvancedDelegateItem::getOrder(stretchId) != AdvancedDelegateItem::AlignRightOrderMask) {
        std::cerr << "stretch item id fields did not round-trip\n";
        return 1;
    }

    original.d->flags = AdvancedDelegateItem::Blink;
    original.d->data = QStringLiteral("stored data");
    original.c->value = QStringLiteral("context value");
    original.c->blinkOpacity = 0.375;
    QByteArray serialized;
    {
        QDataStream stream(&serialized, QIODevice::WriteOnly);
        stream << original;
    }
    AdvancedDelegateItem restored;
    {
        QDataStream stream(&serialized, QIODevice::ReadOnly);
        stream >> restored;
    }
    if (restored.d->id != original.d->id
            || restored.d->kind != original.d->kind
            || restored.d->flags != original.d->flags
            || restored.d->data != original.d->data
            || restored.c->value != original.c->value
            || restored.c->blinkOpacity != original.c->blinkOpacity) {
        std::cerr << "delegate item stream round-trip lost state\n";
        return 1;
    }

    TestAdvancedItemDelegate delegate(nullptr);
    AdvancedDelegateEditProxy editProxy;
    const QStyleOptionViewItem proxyOption;
    if (editProxy.createEditor(&delegate, nullptr, proxyOption, QModelIndex()) != nullptr
            || editProxy.setEditorData(&delegate, nullptr, QModelIndex())
            || editProxy.setModelData(&delegate, nullptr, nullptr, QModelIndex())
            || editProxy.updateEditorGeometry(&delegate, nullptr, proxyOption, QModelIndex())) {
        std::cerr << "default edit proxy should decline unsupported operations\n";
        return 1;
    }
    delegate.setItemsRole(Qt::UserRole + 15);
    delegate.setVertialSpacing(3);
    delegate.setHorizontalSpacing(5);
    delegate.setFocusRectVisible(true);
    delegate.setDefaultBranchItemEnabled(true);
    const QMargins margins(1, 2, 3, 4);
    delegate.setContentsMargings(margins);
    delegate.setBlinkMode(AdvancedItemDelegate::BlinkFade);
    delegate.setEditRole(Qt::UserRole + 16);
    delegate.setEditItemId(id);
    delegate.setEditProxy(&editProxy);
    if (delegate.itemsRole() != Qt::UserRole + 15
            || delegate.verticalSpacing() != 3
            || delegate.horizontalSpacing() != 5
            || !delegate.focusRectVisible()
            || !delegate.defaultBranchItemEnabled()
            || delegate.contentsMargins() != margins
            || delegate.blinkMode() != AdvancedItemDelegate::BlinkFade
            || delegate.editRole() != Qt::UserRole + 16
            || delegate.editItemId() != id
            || delegate.editProxy() != &editProxy
            || delegate.editorFactory() == nullptr) {
        std::cerr << "delegate settings accessors did not round-trip\n";
        return 1;
    }

    TestAdvancedItemDelegate editingDelegate(nullptr);
    editingDelegate.setEditRole(Qt::EditRole);
    editingDelegate.setEditItemId(AdvancedDelegateItem::NullId);
    QStandardItemModel editingModel(1, 1);
    const QModelIndex editingIndex = editingModel.index(0, 0);
    editingModel.setData(editingIndex, QStringLiteral("before"), Qt::DisplayRole);
    editingModel.setData(editingIndex, QStringLiteral("before"), Qt::EditRole);
    QStyleOptionViewItem editingOption;
    editingOption.features |= QStyleOptionViewItem::HasDisplay;
    editingOption.rect = QRect(5, 6, 140, 25);
    QWidget editorParent;
    QWidget *editor = editingDelegate.createEditor(&editorParent, editingOption, editingIndex);
    QLineEdit *lineEditor = qobject_cast<QLineEdit *>(editor);
    if (!lineEditor) {
        std::cerr << "string edit role should create a line editor\n";
        return 1;
    }
    editingDelegate.setEditorData(lineEditor, editingIndex);
    if (lineEditor->text() != QStringLiteral("before")) {
        std::cerr << "editor data should be initialized from the model\n";
        return 1;
    }
    lineEditor->setText(QStringLiteral("after"));
    editingDelegate.setModelData(lineEditor, &editingModel, editingIndex);
    if (editingModel.data(editingIndex, Qt::EditRole).toString() != QStringLiteral("after")) {
        std::cerr << "editor data should be committed back to the model\n";
        return 1;
    }
    editingDelegate.updateEditorGeometry(lineEditor, editingOption, editingIndex);
    QRect expectedEditorRect = editingDelegate.itemRect(
            AdvancedDelegateItem::DisplayId,
            editingDelegate.indexStyleOption(editingOption, editingIndex, true), editingIndex);
    expectedEditorRect.adjust(-1, -1, 1, 1);
    if (lineEditor->geometry() != expectedEditorRect) {
        std::cerr << "editor geometry should follow the display item rectangle\n";
        return 1;
    }

    QStyleOptionViewItem sizeOption;
    sizeOption.features |= QStyleOptionViewItem::HasDisplay;
    AdvancedDelegateItem sizeItem;
    if (AdvancedItemDelegate::itemSizeHint(sizeItem, sizeOption) != QSize(0, 0)) {
        std::cerr << "null delegate item should have zero size\n";
        return 1;
    }
    sizeItem.d->kind = AdvancedDelegateItem::Branch;
    if (AdvancedItemDelegate::itemSizeHint(sizeItem, sizeOption) != QSize(12, 12)) {
        std::cerr << "branch delegate item should use its fixed size\n";
        return 1;
    }

    QStandardItemModel sizeModel(1, 1);
    const QModelIndex sizeIndex = sizeModel.index(0, 0);
    sizeModel.setData(sizeIndex, QStringLiteral("size hint"), Qt::DisplayRole);
    sizeOption.rect = QRect(0, 0, 200, 50);
    QImage paintImage(240, 80, QImage::Format_ARGB32_Premultiplied);
    const QColor sentinelColor(29, 47, 61);
    paintImage.fill(sentinelColor);
    QPainter paintPainter(&paintImage);
    paintPainter.setClipRect(QRect(0, 0, 15, 15));
    const QRectF originalClip = paintPainter.clipBoundingRect();
    FocusTrackingStyle focusStyle;
    QWidget focusWidget;
    focusWidget.setStyle(&focusStyle);
    QStyleOptionViewItem paintOption = sizeOption;
    paintOption.rect = QRect(20, 20, 100, 40);
    paintOption.widget = &focusWidget;
    paintOption.state = QStyle::State_Enabled | QStyle::State_HasFocus;
    delegate.paint(&paintPainter, paintOption, sizeIndex);
    const bool clipRestored = paintPainter.hasClipping()
            && paintPainter.clipBoundingRect() == originalClip;
    paintPainter.end();
    if (!clipRestored || paintImage.pixelColor(0, 0) != sentinelColor
            || focusStyle.focusRectCalls == 0 || focusStyle.backgroundCalls == 0 || focusStyle.textCalls == 0) {
        std::cerr << "delegate painting should render background/text/focus and restore the caller's clip\n";
        return 1;
    }
    QStandardItemModel treeModel;
    QStandardItem *treeParent = new QStandardItem(QStringLiteral("parent"));
    treeParent->setCheckable(true);
    treeParent->setCheckState(Qt::Checked);
    treeParent->appendRow(new QStandardItem(QStringLiteral("child")));
    treeModel.appendRow(treeParent);
    QStyleOptionViewItem treeOption = paintOption;
    treeOption.state |= QStyle::State_Children;
    QImage treeImage(240, 80, QImage::Format_ARGB32_Premultiplied);
    treeImage.fill(sentinelColor);
    QPainter treePainter(&treeImage);
    delegate.paint(&treePainter, treeOption, treeModel.index(0, 0));
    treePainter.end();
    if (focusStyle.branchIndicatorCalls == 0 || focusStyle.checkIndicatorCalls == 0) {
        std::cerr << "painting a checked parent item should draw branch and check indicators\n";
        return 1;
    }
    const QRect displayRect = delegate.itemRect(AdvancedDelegateItem::DisplayId, sizeOption, sizeIndex);
    if (displayRect.isEmpty()
            || delegate.itemAt(displayRect.center(), sizeOption, sizeIndex) != AdvancedDelegateItem::DisplayId) {
        std::cerr << "display role item should be present in the delegate layout\n";
        return 1;
    }
    delegate.setContentsMargings(QMargins(0, 0, 0, 0));
    const QSize sizeWithoutMargins = delegate.sizeHint(sizeOption, sizeIndex);
    delegate.setContentsMargings(QMargins(2, 3, 5, 7));
    const QSize sizeWithMargins = delegate.sizeHint(sizeOption, sizeIndex);
    if (sizeWithMargins != sizeWithoutMargins + QSize(7, 10)) {
        std::cerr << "size hint should add the configured contents margins\n";
        return 1;
    }

    const QSize defaultTextSize = delegate.sizeHint(sizeOption, sizeIndex);
    AdvancedDelegateItem fontHintItem(AdvancedDelegateItem::DisplayId);
    fontHintItem.d->kind = AdvancedDelegateItem::Display;
    fontHintItem.d->data = QStringLiteral("size hint");
    fontHintItem.d->hints.insert(AdvancedDelegateItem::FontSize, 28);
    AdvancedDelegateItems fontHintItems;
    fontHintItems.insert(AdvancedDelegateItem::DisplayId, fontHintItem);
    sizeModel.setData(sizeIndex, QVariant::fromValue(fontHintItems), delegate.itemsRole());
    const QSize largeFontTextSize = delegate.sizeHint(sizeOption, sizeIndex);
    if (largeFontTextSize.height() <= defaultTextSize.height()) {
        std::cerr << "font size hint should increase the display item height\n";
        return 1;
    }
    const QStyleOptionViewItem resolvedOption = delegate.indexStyleOption(sizeOption, sizeIndex, true);
    const AdvancedDelegateItems resolvedItems = delegate.getIndexItems(sizeIndex, resolvedOption);
    const quint32 centerStretchId = AdvancedDelegateItem::makeStretchId(
            AdvancedDelegateItem::MiddleCenter,
            AdvancedDelegateItem::getFloor(AdvancedDelegateItem::DisplayId));
    if (!resolvedItems.contains(centerStretchId)
            || resolvedItems.value(centerStretchId).d->kind != AdvancedDelegateItem::Stretch) {
        std::cerr << "middle-center display item should produce its synthetic stretch item\n";
        return 1;
    }

    QStandardItemModel checkModel;
    QStandardItem *checkItem = new QStandardItem(QStringLiteral("check"));
    checkItem->setCheckable(true);
    checkItem->setUserTristate(true);
    checkItem->setCheckState(Qt::Unchecked);
    checkModel.appendRow(checkItem);
    const QModelIndex checkIndex = checkModel.index(0, 0);
    QStyleOptionViewItem checkOption;
    checkOption.state |= QStyle::State_Enabled;
    const Qt::CheckState expectedStates[] = {Qt::PartiallyChecked, Qt::Checked, Qt::Unchecked};
    for (int i = 0; i < 3; ++i) {
        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        if (!delegate.editorEvent(&keyEvent, &checkModel, checkOption, checkIndex)
                || checkModel.data(checkIndex, Qt::CheckStateRole).toInt() != expectedStates[i]) {
            std::cerr << "tristate check item should cycle through unchecked/partial/checked states\n";
            return 1;
        }
    }

    QStandardItemModel binaryCheckModel;
    QStandardItem *binaryCheckItem = new QStandardItem(QStringLiteral("binary check"));
    binaryCheckItem->setCheckable(true);
    binaryCheckItem->setCheckState(Qt::Unchecked);
    binaryCheckModel.appendRow(binaryCheckItem);
    const QModelIndex binaryCheckIndex = binaryCheckModel.index(0, 0);
    const Qt::CheckState binaryExpectedStates[] = {Qt::Checked, Qt::Unchecked};
    for (int i = 0; i < 2; ++i) {
        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        if (!delegate.editorEvent(&keyEvent, &binaryCheckModel, checkOption, binaryCheckIndex)
                || binaryCheckModel.data(binaryCheckIndex, Qt::CheckStateRole).toInt() != binaryExpectedStates[i]) {
            std::cerr << "binary check item should alternate between unchecked and checked\n";
            return 1;
        }
    }
    return 0;
}
