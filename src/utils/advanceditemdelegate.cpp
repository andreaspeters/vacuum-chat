#include "advanceditemdelegate.h"
#include "advanceditemdelegatehelpers.h"

#include <QStyle>
#include <QPainter>
#include <QKeyEvent>
#include <QMultiMap>
#include <QDateTime>
#include <QDataStream>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QItemEditorFactory>

typedef AdvancedItemDelegateHelpers::LayoutItem<QStyleOptionViewItem> AdvancedDelegateLayoutItem;

QWidget *AdvancedDelegateEditProxy::createEditor(const AdvancedItemDelegate *ADelegate, QWidget *AParent, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex)
{
	Q_UNUSED(ADelegate); Q_UNUSED(AParent); Q_UNUSED(AOption); Q_UNUSED(AIndex);
	return NULL;
}

bool AdvancedDelegateEditProxy::setEditorData(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, const QModelIndex &AIndex)
{
	Q_UNUSED(ADelegate); Q_UNUSED(AEditor); Q_UNUSED(AIndex);
	return false;
}

bool AdvancedDelegateEditProxy::setModelData(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, QAbstractItemModel *AModel, const QModelIndex &AIndex)
{
	Q_UNUSED(ADelegate); Q_UNUSED(AEditor); Q_UNUSED(AModel); Q_UNUSED(AIndex);
	return false;
}

bool AdvancedDelegateEditProxy::updateEditorGeometry(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	Q_UNUSED(ADelegate); Q_UNUSED(AEditor); Q_UNUSED(AOption); Q_UNUSED(AIndex);
	return false;
}

/*********************
 AdvancedItemDelegate
**********************/
struct AdvancedItemDelegate::ItemsLayout
{
	QBoxLayout *mainLayout;
	QBoxLayout *middleLayout;
	QStyleOptionViewItem indexOption;
	QMap<int/*id*/, AdvancedDelegateLayoutItem *> items;
	QMap<int/*position*/, QBoxLayout *> positionLayouts;
	QMap<int/*position*/, QMap<int/*floor*/, QBoxLayout *> > floorLayouts;
};

AdvancedItemDelegate::AdvancedItemDelegate(QObject *AParent) : QStyledItemDelegate(AParent)
{
	FItemsRole = Qt::UserRole + 1000;
	FVerticalSpacing = -1;
	FHorizontalSpacing = -1;
	FFocusRectVisible = false;
	FDefaultBranchEnabled = false;
	FMargins = QMargins(1, 1, 1, 1);
	FEditProxy = NULL;
	FEditRole = Qt::EditRole;
	FEditItemId = AdvancedDelegateItem::NullId;
	FBlinkOpacity = 1.0;
	FBlinkMode = BlinkHide;
	FBlinkTimer.setSingleShot(false);
	FBlinkTimer.setInterval(100);
	connect(&FBlinkTimer, SIGNAL(timeout()), SLOT(onBlinkTimerTimeout()));
	FBlinkTimer.start();
}

AdvancedItemDelegate::~AdvancedItemDelegate()
{
}

const QItemEditorFactory *AdvancedItemDelegate::editorFactory() const
{
	return itemEditorFactory() ? itemEditorFactory() : QItemEditorFactory::defaultFactory();
}

quint32 AdvancedItemDelegate::itemAt(const QPoint &APoint, const ItemsLayout *ALayout, const QRect &AGeometry) const
{
	return AdvancedItemDelegateHelpers::itemAt(APoint, ALayout, AGeometry, FMargins);
}

void AdvancedItemDelegate::paint(QPainter *APainter, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	QStyleOptionViewItem indexOption = indexStyleOption(AOption,AIndex);
	AdvancedItemDelegateHelpers::paintItems(this, APainter, indexOption, AIndex);
}

QSize AdvancedItemDelegate::sizeHint(const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	QVariant hint = AIndex.data(Qt::SizeHintRole);
	if (hint.isValid())
		return qvariant_cast<QSize>(hint);

	QStyleOptionViewItem indexOption = indexStyleOption(AOption,AIndex,true);
	return AdvancedItemDelegateHelpers::calculateSizeHint(this, AIndex, indexOption);
}

QWidget *AdvancedItemDelegate::createEditor(QWidget *AParent, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	return AdvancedItemDelegateHelpers::createEditor(this, AParent, AOption, AIndex);
}

void AdvancedItemDelegate::setEditorData(QWidget *AEditor, const QModelIndex &AIndex) const
{
	AdvancedItemDelegateHelpers::setEditorData(this, AEditor, AIndex);
}

void AdvancedItemDelegate::setModelData(QWidget *AEditor, QAbstractItemModel *AModel, const QModelIndex &AIndex) const
{
	AdvancedItemDelegateHelpers::setModelData(this, AEditor, AModel, AIndex);
}

void AdvancedItemDelegate::updateEditorGeometry(QWidget *AEditor, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	AdvancedItemDelegateHelpers::updateEditorGeometry(this, AEditor, AOption, AIndex);
}

AdvancedDelegateItems AdvancedItemDelegate::getIndexItems(const QModelIndex &AIndex, const QStyleOptionViewItem &AIndexOption) const
{
	AdvancedDelegateItems items = AIndex.data(FItemsRole).value<AdvancedDelegateItems>();

	if (FDefaultBranchEnabled && (AIndexOption.state & QStyle::State_Children)>0)
	{
		AdvancedDelegateItem &branchItem = items[AdvancedDelegateItem::BranchId];
		if (branchItem.d->kind == AdvancedDelegateItem::Null)
		{
			branchItem.d->kind = AdvancedDelegateItem::Branch;
			branchItem.d->id = AdvancedDelegateItem::BranchId;
		}
	}

	if (AIndexOption.features & QStyleOptionViewItem::HasCheckIndicator)
	{
		AdvancedDelegateItem &checkItem = items[AdvancedDelegateItem::CheckStateId];
		if (checkItem.d->kind == AdvancedDelegateItem::Null)
		{
			checkItem.d->kind = AdvancedDelegateItem::CheckBox;
			checkItem.d->id = AdvancedDelegateItem::CheckStateId;
			checkItem.d->data = AIndex.data(Qt::CheckStateRole);
		}
	}

	if (AIndexOption.features & QStyleOptionViewItem::HasDecoration)
	{
		AdvancedDelegateItem &decorationItem = items[AdvancedDelegateItem::DecorationId];
		if (decorationItem.d->kind == AdvancedDelegateItem::Null)
		{
			decorationItem.d->kind = AdvancedDelegateItem::Decoration;
			decorationItem.d->id = AdvancedDelegateItem::DecorationId;
			decorationItem.d->data = AIndex.data(Qt::DecorationRole);
		}
	}

	if (AIndexOption.features & QStyleOptionViewItem::HasDisplay)
		AdvancedItemDelegateHelpers::ensureDisplayItem(items, AIndex);

	AdvancedDelegateItems stretches;
	AdvancedItemDelegateHelpers::populateStretches(items, stretches, AIndex, FBlinkOpacity);
	items.insert(stretches);
	
	return items;
}

QStyleOptionViewItem AdvancedItemDelegate::indexStyleOption(const QStyleOptionViewItem &AOption, const QModelIndex &AIndex, bool ACorrect) const
{
	QStyleOptionViewItem indexOption = AOption;

    QVariant value = AdvancedItemDelegateHelpers::initializeIndexStyleOption(indexOption, AIndex, ACorrect);

	value = AIndex.data(Qt::ForegroundRole);
	if (value.canConvert<QBrush>())
		indexOption.palette.setBrush(QPalette::Text, qvariant_cast<QBrush>(value));

	value = AIndex.data(Qt::CheckStateRole);
	if (value.isValid() && !value.isNull()) 
		indexOption.features |= QStyleOptionViewItem::HasCheckIndicator;

	value = AIndex.data(Qt::DecorationRole);
	if (value.isValid() && !value.isNull()) 
		indexOption.features |= QStyleOptionViewItem::HasDecoration;

	value = AIndex.data(Qt::DisplayRole);
	if (value.isValid() && !value.isNull()) 
		indexOption.features |= QStyleOptionViewItem::HasDisplay;

	indexOption.backgroundBrush = qvariant_cast<QBrush>(AIndex.data(Qt::BackgroundRole));

	return indexOption;
}

QStyleOptionViewItem AdvancedItemDelegate::itemStyleOption(const AdvancedDelegateItem &AItem, const QStyleOptionViewItem &AIndexOption) const
{
	QStyleOptionViewItem itemOption = AIndexOption;

	if (AItem.d->kind == AdvancedDelegateItem::CheckBox)
	{
		itemOption.state &= ~QStyle::State_HasFocus;
		itemOption.features |= QStyleOptionViewItem::HasCheckIndicator;
		itemOption.checkState = static_cast<Qt::CheckState>(AItem.c->value.toInt());

		switch (itemOption.checkState)
		{
		case Qt::Unchecked:
			itemOption.state |= QStyle::State_Off;
			break;
		case Qt::PartiallyChecked:
			itemOption.state |= QStyle::State_NoChange;
			break;
		case Qt::Checked:
			itemOption.state |= QStyle::State_On;
			break;
		}
	}
	else
	{
		itemOption.features &= ~QStyleOptionViewItem::HasCheckIndicator;
	}

	if (!AItem.c->value.isNull() && AItem.c->value.canConvert<QString>())
		itemOption.text = AdvancedItemDelegateHelpers::getSingleLineText(displayText(AItem.c->value,itemOption.locale));

	if (!AItem.d->hints.isEmpty())
	{
		QVariant hint = AItem.d->hints.value(AdvancedDelegateItem::FontWeight);
		if (!hint.isNull())
			itemOption.font.setWeight(static_cast<QFont::Weight>(hint.toInt()));

		AdvancedItemDelegateHelpers::applyItemStyleHints(AItem, itemOption);
	}

	return itemOption;
}

AdvancedItemDelegate::ItemsLayout *AdvancedItemDelegate::createItemsLayout(const AdvancedDelegateItems &AItems, const QStyleOptionViewItem &AIndexOption) const
{
	ItemsLayout *layout = new ItemsLayout;
	AdvancedItemDelegateHelpers::populateItemsLayout(this, layout, AItems, AIndexOption);
	return layout;
}

void AdvancedItemDelegate::destroyItemsLayout(ItemsLayout *ALayout) const
{
	AdvancedItemDelegateHelpers::destroyLayoutRecursive(ALayout->mainLayout);
	delete ALayout;
}

QRect AdvancedItemDelegate::itemRect(quint32 AItemId, const ItemsLayout *ALayout, const QRect &AGeometry) const
{
	QRect rect;
	if (ALayout->items.contains(AItemId))
	{
		if (ALayout->mainLayout->geometry() != AGeometry)
			ALayout->mainLayout->setGeometry(AGeometry.adjusted(FMargins.left(),FMargins.top(),-FMargins.right(),-FMargins.bottom()));
		rect = ALayout->items.value(AItemId)->geometry();
	}
	return rect;
}

QRect AdvancedItemDelegate::itemRect(quint32 AItemId, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	QRect rect;
	if (AIndex.isValid() && !AOption.rect.isEmpty())
	{
		QStyleOptionViewItem indexOption = indexStyleOption(AOption,AIndex);
		ItemsLayout *layout = createItemsLayout(getIndexItems(AIndex,indexOption),indexOption);
		rect = itemRect(AItemId,layout,indexOption.rect);
		destroyItemsLayout(layout);
	}
	return rect;
}

quint32 AdvancedItemDelegate::itemAt(const QPoint &APoint, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const
{
	quint32 itemId = AdvancedDelegateItem::NullId;
	if (AIndex.isValid() && !AOption.rect.isEmpty())
	{
		QStyleOptionViewItem indexOption = indexStyleOption(AOption,AIndex);
		ItemsLayout *layout = createItemsLayout(getIndexItems(AIndex,indexOption),indexOption);
		itemId = itemAt(APoint,layout,indexOption.rect);
		destroyItemsLayout(layout);
	}
	return itemId;
}

bool AdvancedItemDelegate::isItemVisible(const AdvancedDelegateItem &AItem, const QStyleOptionViewItem &AItemOption)
{
	if (( AItemOption.state & AItem.d->showStates) != AItem.d->showStates)
		return false;

	if (( AItemOption.state & AItem.d->hideStates) > 0)
		return false;

	switch (AItem.d->kind)
	{
	case AdvancedDelegateItem::Null:
		return false;
	case AdvancedDelegateItem::Branch:
		return (AItemOption.state & QStyle::State_Children)>0;
	case AdvancedDelegateItem::CheckBox:
		return (AItemOption.features & QStyleOptionViewItem::HasCheckIndicator)>0;
	case AdvancedDelegateItem::Stretch:
		return true;
	case AdvancedDelegateItem::CustomWidget:
		return AItem.d->widget!=NULL;
	default:
		return !AItem.c->value.isNull();
	}
}

QSize AdvancedItemDelegate::itemSizeHint(const AdvancedDelegateItem &AItem, const QStyleOptionViewItem &AItemOption)
{
	return AdvancedItemDelegateHelpers::itemSizeHint(AItem, AItemOption);
}

void AdvancedItemDelegate::drawBackground(QPainter *APainter, const QStyleOptionViewItem &AIndexOption) const
{
	AdvancedItemDelegateHelpers::drawBackground(APainter, AIndexOption);
}

void AdvancedItemDelegate::drawFocusRect(QPainter *APainter, const QStyleOptionViewItem &AIndexOption, const QRect &ARect) const
{
	Q_UNUSED(ARect);
	AdvancedItemDelegateHelpers::drawFocusRect(APainter, AIndexOption, FFocusRectVisible);
}

bool AdvancedItemDelegate::editorEvent(QEvent *AEvent, QAbstractItemModel *AModel, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex)
{
	Qt::ItemFlags flags = AModel->flags(AIndex);
	const bool isTristate = (flags & Qt::ItemIsUserTristate)>0;
	return AdvancedItemDelegateHelpers::handleCheckEditorEvent(this, AEvent, AModel, AOption, AIndex, flags, isTristate);
}

void AdvancedItemDelegate::onBlinkTimerTimeout()
{
	qreal blinkOpacity = AdvancedItemDelegateHelpers::currentBlinkOpacity(FBlinkMode == BlinkHide);
	if (FBlinkOpacity != blinkOpacity)
	{
		FBlinkOpacity = blinkOpacity;
		emit updateBlinkItems();
	}
}
