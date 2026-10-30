#ifndef ADVANCEDITEMDELEGATEHELPERS_H
#define ADVANCEDITEMDELEGATEHELPERS_H

#include <QLayout>
#include <QLayoutItem>
#include <QString>
#include <QStyle>
#include <QPainter>
#include <QVariant>
#include <QPixmap>
#include <QImage>
#include <QIcon>
#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QStyleOption>
#include <QKeyEvent>
#include <QMouseEvent>

namespace AdvancedItemDelegateHelpers
{
inline qreal currentBlinkOpacity(bool hide)
{
    static const qreal hideSteps[] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0 };
    static const qreal fadeSteps[] = { 1.0, 0.8, 0.6, 0.4, 0.2, 0.2, 0.4, 0.6, 0.8, 1.0 };
    static const int duration = 1000;
    const int count = sizeof(hideSteps) / sizeof(hideSteps[0]);
    const int step = (QDateTime::currentMSecsSinceEpoch() % duration) * count / duration;
    return hide ? hideSteps[step] : fadeSteps[step];
}

inline void destroyLayoutRecursive(QLayout *layout)
{
    QLayoutItem *child;
    while ((child = layout->takeAt(0)) != 0)
    {
        if (child->layout())
            destroyLayoutRecursive(child->layout());
        else
            delete child;
    }
    delete layout;
}

inline QString getSingleLineText(const QString &text)
{
    static const QChar space(' ');
    static const QChar lineBreak('\n');
    static const QChar lineSep(QChar::LineSeparator);
    static const QChar paragraphSep(QChar::ParagraphSeparator);

    QString result = text;
    for (int i = 0; i < result.size(); ++i)
    {
        const QChar textChar = result.at(i);
        if (textChar == lineBreak || textChar == lineSep || textChar == paragraphSep)
            result[i] = space;
    }

    return result.trimmed();
}

template<class Option>
inline void applyItemStyleHints(const AdvancedDelegateItem &item, Option &itemOption)
{
    QVariant hint = item.d->hints.value(AdvancedDelegateItem::FontHint);
    if (!hint.isNull())
        itemOption.font.setStyleHint(static_cast<QFont::StyleHint>(hint.toInt()));

    hint = item.d->hints.value(AdvancedDelegateItem::FontStyle);
    if (!hint.isNull())
        itemOption.font.setStyle(static_cast<QFont::Style>(hint.toInt()));

    hint = item.d->hints.value(AdvancedDelegateItem::FontSize);
    if (!hint.isNull())
        itemOption.font.setPointSize(hint.toInt());

    hint = item.d->hints.value(AdvancedDelegateItem::FontSizeDelta);
    if (!hint.isNull())
        itemOption.font.setPointSize(itemOption.font.pointSize() + hint.toInt());

    hint = item.d->hints.value(AdvancedDelegateItem::FontUnderline);
    if (!hint.isNull())
        itemOption.font.setUnderline(hint.toBool());

    hint = item.d->hints.value(AdvancedDelegateItem::StatesForceOn);
    if (!hint.isNull())
        itemOption.state |= static_cast<QStyle::State>(hint.toInt());

    hint = item.d->hints.value(AdvancedDelegateItem::StatesForceOff);
    if (!hint.isNull())
        itemOption.state &= ~static_cast<QStyle::State>(hint.toInt());

    hint = item.d->hints.value(AdvancedDelegateItem::Foreground);
    if (!hint.isNull())
        itemOption.palette.setBrush(QPalette::Text, qvariant_cast<QBrush>(hint));

    hint = item.d->hints.value(AdvancedDelegateItem::Backgound);
    if (!hint.isNull())
        itemOption.backgroundBrush = qvariant_cast<QBrush>(hint);

    hint = item.d->hints.value(AdvancedDelegateItem::TextElideMode);
    if (!hint.isNull())
        itemOption.textElideMode = static_cast<Qt::TextElideMode>(hint.toInt());

    hint = item.d->hints.value(AdvancedDelegateItem::TextAlignment);
    if (!hint.isNull())
        itemOption.displayAlignment = static_cast<Qt::Alignment>(hint.toInt());

    itemOption.fontMetrics = QFontMetrics(itemOption.font);
}

inline void ensureDisplayItem(AdvancedDelegateItems &items, const QModelIndex &index)
{
    AdvancedDelegateItem &displayItem = items[AdvancedDelegateItem::DisplayId];
    if (displayItem.d->kind == AdvancedDelegateItem::Null)
    {
        displayItem.d->kind = AdvancedDelegateItem::Display;
        displayItem.d->id = AdvancedDelegateItem::DisplayId;
        displayItem.d->data = index.data(Qt::DisplayRole);
    }
}

inline void populateStretches(AdvancedDelegateItems &items, AdvancedDelegateItems &stretches,
                              const QModelIndex &index, qreal blinkOpacity)
{
    for (AdvancedDelegateItems::iterator it = items.begin(); it != items.end(); ++it)
    {
        const quint8 position = AdvancedDelegateItem::getPosition(it->d->id);
        if (position == AdvancedDelegateItem::MiddleCenter)
        {
            const quint8 floor = AdvancedDelegateItem::getFloor(it->d->id);
            const quint32 stretchId = AdvancedDelegateItem::makeStretchId(position, floor);
            if (!stretches.contains(stretchId) && !items.contains(stretchId))
            {
                AdvancedDelegateItem stretchItem(stretchId);
                stretchItem.d->kind = AdvancedDelegateItem::Stretch;
                stretchItem.d->sizePolicy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                stretches.insert(stretchId, stretchItem);
            }
        }

        it->c->value = it->d->data;
        if (it->d->kind == AdvancedDelegateItem::Decoration || it->d->kind == AdvancedDelegateItem::Display || it->d->kind == AdvancedDelegateItem::CustomData)
        {
            if (it->d->data.type() == QVariant::Int)
                it->c->value = index.data(it->d->data.toInt());
        }
        it->c->blinkOpacity = (it->d->flags & AdvancedDelegateItem::Blink) > 0 ? blinkOpacity : 1.0;
    }
}

template<class Option>
inline QVariant initializeIndexStyleOption(Option &option, const QModelIndex &index, bool correct)
{
    if (correct && index.model()->hasChildren(index))
        option.state |= QStyle::State_Children;

    option.index = index;

    QVariant value = index.data(Qt::FontRole);
    if (value.isValid() && !value.isNull())
    {
        option.font = qvariant_cast<QFont>(value).resolve(option.font);
        option.fontMetrics = QFontMetrics(option.font);
    }

    value = index.data(Qt::TextAlignmentRole);
    if (value.isValid() && !value.isNull())
        option.displayAlignment = Qt::Alignment(value.toInt());
    return value;
}

inline Qt::CheckState nextCheckState(int currentState, bool tristate)
{
    if (tristate)
        return static_cast<Qt::CheckState>((currentState + 1) % 3);
    if (currentState == Qt::Unchecked)
        return Qt::Checked;
    return Qt::Unchecked;
}

template<class Delegate, class Option>
inline bool handleCheckEditorEvent(const Delegate *delegate, QEvent *event, QAbstractItemModel *model,
                                   const Option &option, const QModelIndex &index,
                                   Qt::ItemFlags flags, bool tristate)
{
    if (!(flags & Qt::ItemIsUserCheckable) || !(option.state & QStyle::State_Enabled) || !(flags & Qt::ItemIsEnabled))
        return false;

    const QVariant value = index.data(Qt::CheckStateRole);
    if (value.isNull() || !value.isValid())
        return false;

    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick || type == QEvent::MouseButtonPress)
    {
        const QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return false;

        const QRect checkRect = delegate->itemRect(AdvancedDelegateItem::CheckStateId, option, index);
        if (!checkRect.contains(mouseEvent->pos()))
            return false;
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)
            return true;
    }
    else if (type == QEvent::KeyPress)
    {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() != Qt::Key_Space && keyEvent->key() != Qt::Key_Select)
            return false;
    }
    else
    {
        return false;
    }

    const Qt::CheckState state = nextCheckState(value.toInt(), tristate);
    return model->setData(index, state, Qt::CheckStateRole);
}

template<class Option> 
inline QSize itemSizeHint(const AdvancedDelegateItem &AItem, const Option &AItemOption)
{
    static const QSize zeroSize = QSize(0,0);
    static const QSize branchSize = QSize(12,12);

    switch (AItem.d->kind)
    {
    case AdvancedDelegateItem::Null:
    case AdvancedDelegateItem::Stretch:
        {
            return zeroSize;
        }
    case AdvancedDelegateItem::Branch:
        {
            return branchSize;
        }
    case AdvancedDelegateItem::CheckBox:
        {
            QStyle *style = AItemOption.widget!=NULL ? AItemOption.widget->style() : QApplication::style();
            int width = style->proxy()->pixelMetric(QStyle::PM_IndicatorWidth, &AItemOption, AItemOption.widget);
            int height = style->proxy()->pixelMetric(QStyle::PM_IndicatorHeight, &AItemOption, AItemOption.widget);
            return QSize(width,height);
        }
    case AdvancedDelegateItem::CustomWidget:
        {
            return AItem.d->widget!=NULL ? AItem.d->widget->sizeHint() : zeroSize;
        }
    default:
        {
            switch (AItem.c->value.type())
            {
            case QVariant::Pixmap:
                {
                    QPixmap pixmap = qvariant_cast<QPixmap>(AItem.c->value);
                    if (!pixmap.isNull())
                        return pixmap.size();
                    break;
                }
            case QVariant::Image:
                {
                    QImage image = qvariant_cast<QImage>(AItem.c->value);
                    if (!image.isNull())
                        return image.size();
                    break;
                }
            case QVariant::Icon:
                {
                    QIcon icon = qvariant_cast<QIcon>(AItem.c->value);
                    if (!icon.isNull())
                        return icon.actualSize(AItemOption.decorationSize);
                    break;
                }
            default:
                {
                    if (!AItemOption.text.isEmpty())
                        return AItemOption.fontMetrics.size(AItemOption.direction|Qt::TextSingleLine,AItemOption.text);
                    break;
                }
            }
            return zeroSize;
        }
    }
}

template<class Option>
class LayoutItem : public QLayoutItem
{
public:
    LayoutItem(const AdvancedDelegateItem &item, const Option &option) :
        QLayoutItem(Qt::AlignCenter), FItem(item), FOption(option)
    {
    }

    bool isEmpty() const { return false; }

    void invalidate()
    {
        FSizeHint = QSize();
    }

    Qt::Orientations expandingDirections() const
    {
        return FItem.d->sizePolicy.expandingDirections();
    }

    QSize sizeHint() const
    {
        if (!FSizeHint.isValid())
        {
            FSizeHint = FItem.d->hints.value(AdvancedDelegateItem::SizeHint).toSize();
            if (!FSizeHint.isValid())
                FSizeHint = AdvancedItemDelegate::itemSizeHint(FItem, FOption);
        }
        return FSizeHint;
    }

    QSize minimumSize() const
    {
        const QSize hint = sizeHint();
        const QSizePolicy &policy = FItem.d->sizePolicy;
        return QSize(policy.horizontalPolicy() & QSizePolicy::ShrinkFlag ? 0 : hint.width(),
                     policy.verticalPolicy() & QSizePolicy::ShrinkFlag ? 0 : hint.height());
    }

    QSize maximumSize() const
    {
        const QSize hint = sizeHint();
        const QSizePolicy &policy = FItem.d->sizePolicy;
        return QSize(policy.horizontalPolicy() & QSizePolicy::GrowFlag ? QLAYOUTSIZE_MAX : hint.width(),
                     policy.verticalPolicy() & QSizePolicy::GrowFlag ? QLAYOUTSIZE_MAX : hint.height());
    }

    QRect geometry() const { return FOption.rect; }

    void setGeometry(const QRect &rect)
    {
        if (FItem.d->widget != NULL)
            FItem.d->widget->setGeometry(rect);
        FOption.rect = rect;
    }

    const AdvancedDelegateItem &delegateItem() const { return FItem; }
    const Option &itemOption() const { return FOption; }

    void drawVariant(QPainter *painter, const QVariant &value)
    {
        QStyle *style = FOption.widget ? FOption.widget->style() : QApplication::style();
        switch (value.type())
        {
        case QVariant::Color:
            painter->fillRect(FOption.rect, qvariant_cast<QColor>(value));
            break;
        case QVariant::Pixmap:
            style->proxy()->drawItemPixmap(painter, FOption.rect, Qt::AlignCenter, qvariant_cast<QPixmap>(value));
            break;
        case QVariant::Image:
            painter->drawImage(FOption.rect.topLeft(), qvariant_cast<QImage>(value));
            break;
        case QVariant::Icon:
        {
            QIcon::Mode mode = QIcon::Normal;
            if (FOption.state & QStyle::State_Selected)
                mode = QIcon::Selected;
            else if (!(FOption.state & QStyle::State_Enabled))
                mode = QIcon::Disabled;
            QIcon icon = qvariant_cast<QIcon>(value);
            const QPixmap pixmap = style->generatedIconPixmap(mode, icon.pixmap(FOption.decorationSize), &FOption);
            style->proxy()->drawItemPixmap(painter, FOption.rect, FOption.decorationAlignment, pixmap);
            break;
        }
        default:
            if (!FOption.text.isEmpty())
            {
                painter->setFont(FOption.font);
                const int flags = FOption.displayAlignment | Qt::TextSingleLine;
                const QPalette::ColorRole role = FOption.state & QStyle::State_Selected
                        ? QPalette::HighlightedText : QPalette::Text;
                const QString text = FOption.fontMetrics.elidedText(
                        FOption.text, FOption.textElideMode, FOption.rect.width(), flags);
                style->proxy()->drawItemText(painter, FOption.rect, flags, FOption.palette,
                                             (FOption.state & QStyle::State_Enabled) > 0, text, role);
            }
            break;
        }
    }

    void drawItem(QPainter *painter)
    {
        if (FOption.rect.isEmpty())
            return;

        painter->save();
        painter->setClipRect(FOption.rect);
        if (FItem.d->hints.contains(AdvancedDelegateItem::Opacity))
            painter->setOpacity(FItem.d->hints.value(AdvancedDelegateItem::Opacity).toReal());
        painter->setOpacity(painter->opacity() * FItem.c->blinkOpacity);

        switch (FItem.d->kind)
        {
        case AdvancedDelegateItem::Branch:
            drawBranchIndicator(painter);
            break;
        case AdvancedDelegateItem::CheckBox:
            drawCheckIndicator(painter);
            break;
        case AdvancedDelegateItem::Null:
        case AdvancedDelegateItem::Stretch:
        case AdvancedDelegateItem::CustomWidget:
            break;
        default:
            drawVariant(painter, FItem.c->value);
            break;
        }
        painter->restore();
    }

private:
    void drawBranchIndicator(QPainter *painter)
    {
        Option option(FOption);
        option.rect = QStyle::alignedRect(option.direction, Qt::AlignCenter, FSizeHint, option.rect);
        QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->proxy()->drawPrimitive(QStyle::PE_IndicatorBranch, &option, painter, FOption.widget);
    }

    void drawCheckIndicator(QPainter *painter)
    {
        QStyle *style = FOption.widget ? FOption.widget->style() : QApplication::style();
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
        const QStyle::PrimitiveElement element = QStyle::PE_IndicatorViewItemCheck;
#else
        const QStyle::PrimitiveElement element = QStyle::PE_IndicatorItemViewItemCheck;
#endif
        style->proxy()->drawPrimitive(element, &FOption, painter, FOption.widget);
    }

    mutable QSize FSizeHint;
    AdvancedDelegateItem FItem;
    Option FOption;
};

template<class Option>
inline void drawBackground(QPainter *painter, const Option &option)
{
    QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    style->proxy()->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, option.widget);
}

template<class Option>
inline void drawFocusRect(QPainter *painter, const Option &option, bool visible)
{
    if (!visible || !(option.state & QStyle::State_HasFocus))
        return;

    QStyle *style = option.widget ? option.widget->style() : QApplication::style();
    QStyleOptionFocusRect focusOption;
    focusOption.QStyleOption::operator=(option);
    focusOption.rect = style->proxy()->subElementRect(QStyle::SE_ItemViewItemFocusRect, &option, option.widget);
    focusOption.state |= QStyle::State_KeyboardFocusChange | QStyle::State_Item;

    const QPalette::ColorGroup colorGroup = (option.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
    const QPalette::ColorRole colorRole = (option.state & QStyle::State_Selected) ? QPalette::Highlight : QPalette::Window;
    focusOption.backgroundColor = option.palette.color(colorGroup, colorRole);
    style->proxy()->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOption, painter);
}

template<class Delegate, class Option>
inline QWidget *createEditor(const Delegate *delegate, QWidget *parent, const Option &option, const QModelIndex &index)
{
    QWidget *widget = NULL;
    if (index.isValid())
    {
        AdvancedDelegateEditProxy *proxy = delegate->editProxy();
        widget = proxy ? proxy->createEditor(delegate, parent, option, index) : NULL;
        if (!widget)
        {
            QVariant value = index.data(delegate->editRole());
            if (delegate->editItemId() != AdvancedDelegateItem::NullId)
            {
                Option indexOption = delegate->indexStyleOption(option, index, true);
                AdvancedDelegateItems items = delegate->getIndexItems(index, indexOption);
                value = items.value(delegate->editItemId()).c->value;
            }

            const QVariant::Type type = static_cast<QVariant::Type>(value.userType());
            widget = delegate->editorFactory()->createEditor(type, parent);
            if (widget)
                widget->setProperty(ADVANCED_DELEGATE_EDITOR_VALUE_PROPERTY, value);
        }
    }
    return widget;
}

template<class Delegate>
inline void setEditorData(const Delegate *delegate, QWidget *editor, const QModelIndex &index)
{
    AdvancedDelegateEditProxy *proxy = delegate->editProxy();
    if (!proxy || !proxy->setEditorData(delegate, editor, index))
    {
        const QVariant value = editor->property(ADVANCED_DELEGATE_EDITOR_VALUE_PROPERTY);
        const QByteArray name = delegate->editorFactory()->valuePropertyName(value.type());
        if (!name.isEmpty())
            editor->setProperty(name, value);
    }
}

template<class Delegate>
inline void setModelData(const Delegate *delegate, QWidget *editor, QAbstractItemModel *model, const QModelIndex &index)
{
    AdvancedDelegateEditProxy *proxy = delegate->editProxy();
    if (!proxy || !proxy->setModelData(delegate, editor, model, index))
    {
        const QVariant value = editor->property(ADVANCED_DELEGATE_EDITOR_VALUE_PROPERTY);
        const QByteArray name = delegate->editorFactory()->valuePropertyName(value.type());
        if (!name.isEmpty())
            model->setData(index, editor->property(name), delegate->editRole());
    }
}

template<class Delegate, class Option>
inline void updateEditorGeometry(const Delegate *delegate, QWidget *editor, const Option &option, const QModelIndex &index)
{
    AdvancedDelegateEditProxy *proxy = delegate->editProxy();
    if (!proxy || !proxy->updateEditorGeometry(delegate, editor, option, index))
    {
        const quint32 itemId = delegate->editItemId() != AdvancedDelegateItem::NullId ? delegate->editItemId() : AdvancedDelegateItem::DisplayId;
        QRect rect = delegate->itemRect(itemId, delegate->indexStyleOption(option, index, true), index);
        rect.adjust(-1, -1, 1, 1);
        editor->setGeometry(rect);
    }
}

template<class Delegate, class Option>
inline QSize calculateSizeHint(const Delegate *delegate, const QModelIndex &index, const Option &option)
{
    auto layout = delegate->createItemsLayout(delegate->getIndexItems(index, option), option);
    const QMargins margins = delegate->contentsMargins();
    const QSize size = layout->mainLayout->sizeHint() + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    delegate->destroyItemsLayout(layout);
    return size;
}

template<class Layout>
inline quint32 itemAt(const QPoint &point, Layout *layout, const QRect &geometry, const QMargins &margins)
{
    if (geometry.contains(point))
    {
        if (layout->mainLayout->geometry() != geometry)
            layout->mainLayout->setGeometry(geometry.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom()));

        for (auto it = layout->items.constBegin(); it != layout->items.constEnd(); ++it)
        {
            if (it.value()->geometry().contains(point))
                return it.key();
        }
    }
    return AdvancedDelegateItem::NullId;
}

template<class Delegate, class Option>
inline void paintItems(const Delegate *delegate, QPainter *painter, const Option &option, const QModelIndex &index)
{
    painter->save();
    painter->setClipping(true);
    painter->setClipRect(option.rect);
    drawBackground(painter, option);

    auto layout = delegate->createItemsLayout(delegate->getIndexItems(index, option), option);
    const QMargins margins = delegate->contentsMargins();
    layout->mainLayout->setGeometry(option.rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom()));
    for (auto it = layout->items.constBegin(); it != layout->items.constEnd(); ++it)
        it.value()->drawItem(painter);
    delegate->destroyItemsLayout(layout);

    drawFocusRect(painter, option, delegate->focusRectVisible());
    painter->restore();
}

template<class Delegate, class Layout, class Option>
inline void populateItemsLayout(const Delegate *delegate, Layout *layout,
                               const AdvancedDelegateItems &items, const Option &indexOption)
{
    typedef LayoutItem<Option> Item;
    typedef QMap<int, QMap<int, QMultiMap<int, Item *> > > OrderedItems;
    static const Qt::Alignment layoutAlignment = Qt::AlignLeft | Qt::AlignVCenter;

    QStyle *style = indexOption.widget ? indexOption.widget->style() : QApplication::style();
    const int verticalSpacing = delegate->verticalSpacing() < 0
        ? style->proxy()->pixelMetric(QStyle::PM_FocusFrameVMargin) + 1 : delegate->verticalSpacing();
    const int horizontalSpacing = delegate->horizontalSpacing() < 0
        ? style->proxy()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1 : delegate->horizontalSpacing();

    layout->middleLayout = NULL;
    layout->indexOption = indexOption;
    layout->mainLayout = new QVBoxLayout;
    layout->mainLayout->setSpacing(verticalSpacing);
    layout->mainLayout->setAlignment(layoutAlignment);

    OrderedItems orderedItems;
    for (typename AdvancedDelegateItems::const_iterator it = items.constBegin(); it != items.constEnd(); ++it)
    {
        Option itemOption = delegate->itemStyleOption(it.value(), indexOption);
        if (!delegate->isItemVisible(it.value(), itemOption))
            continue;

        const quint8 position = AdvancedDelegateItem::getPosition(it->d->id);
        const quint8 floor = AdvancedDelegateItem::getFloor(it->d->id);
        const quint16 order = AdvancedDelegateItem::getOrder(it->d->id);

        QBoxLayout *&positionLayout = layout->positionLayouts[position];
        if (!positionLayout)
        {
            positionLayout = new QVBoxLayout;
            positionLayout->setSpacing(verticalSpacing);
            positionLayout->setAlignment(layoutAlignment);
        }

        QBoxLayout *&floorLayout = layout->floorLayouts[position][floor];
        if (!floorLayout)
        {
            floorLayout = new QHBoxLayout;
            floorLayout->setSpacing(horizontalSpacing);
            floorLayout->setAlignment(layoutAlignment);
        }

        Item *item = new Item(it.value(), itemOption);
        layout->items.insert(it.key(), item);
        orderedItems[position][floor].insertMulti(order, item);
    }

    for (typename OrderedItems::const_iterator positionIt = orderedItems.constBegin(); positionIt != orderedItems.constEnd(); ++positionIt)
    {
        QBoxLayout *positionLayout = layout->positionLayouts.value(positionIt.key());
        if (positionIt.key() >= AdvancedDelegateItem::MiddleLeft && positionIt.key() <= AdvancedDelegateItem::MiddleRight)
        {
            if (!layout->middleLayout)
            {
                layout->middleLayout = new QHBoxLayout;
                layout->middleLayout->setSpacing(horizontalSpacing);
                layout->middleLayout->setAlignment(layoutAlignment);
                layout->mainLayout->addLayout(layout->middleLayout);
            }
            layout->middleLayout->addLayout(positionLayout);
        }
        else
        {
            layout->mainLayout->addLayout(positionLayout);
        }

        QMap<int, QBoxLayout *> &floorLayouts = layout->floorLayouts[positionIt.key()];
        for (typename QMap<int, QMultiMap<int, Item *> >::const_iterator floorIt = positionIt->constBegin(); floorIt != positionIt->constEnd(); ++floorIt)
        {
            QBoxLayout *floorLayout = floorLayouts.value(floorIt.key());
            positionLayout->addLayout(floorLayout);
            for (typename QMultiMap<int, Item *>::const_iterator orderIt = floorIt->constBegin(); orderIt != floorIt->constEnd(); ++orderIt)
                floorLayout->addItem(orderIt.value());
        }
    }
}

} // namespace AdvancedItemDelegateHelpers

#endif // ADVANCEDITEMDELEGATEHELPERS_H