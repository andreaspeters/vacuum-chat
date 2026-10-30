#ifndef ADVANCEDITEMDELEGATE_H
#define ADVANCEDITEMDELEGATE_H

#include <QMap>
#include <QTimer>
#include <QWidget>
#include <QMargins>
#include <QVariant>
#include <QSizePolicy>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include "utilsexport.h"

#define ADVANCED_DELEGATE_EDITOR_VALUE_PROPERTY "AdvanceDelegateEditorValue"

#include "../../../utils/advanceddelegateitem.h"

class AdvancedItemDelegate;
class UTILS_EXPORT AdvancedDelegateEditProxy
{
public:
	virtual QWidget *createEditor(const AdvancedItemDelegate *ADelegate, QWidget *AParent, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex);
	virtual bool setEditorData(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, const QModelIndex &AIndex);
	virtual bool setModelData(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, QAbstractItemModel *AModel, const QModelIndex &AIndex);
	virtual bool updateEditorGeometry(const AdvancedItemDelegate *ADelegate, QWidget *AEditor, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
};

class UTILS_EXPORT AdvancedItemDelegate : 
	public QStyledItemDelegate
{
	Q_OBJECT;
	struct ItemsLayout;
public:
	enum BlinkMode {
		BlinkHide,
		BlinkFade
	};
public:
	AdvancedItemDelegate(QObject *AParent);
	~AdvancedItemDelegate();
	int itemsRole() const { return FItemsRole; }
	void setItemsRole(int ARole) { FItemsRole = ARole; }
	int verticalSpacing() const { return FVerticalSpacing; }
	void setVertialSpacing(int ASpacing) { FVerticalSpacing = ASpacing; }
	int horizontalSpacing() const { return FHorizontalSpacing; }
	void setHorizontalSpacing(int ASpacing) { FHorizontalSpacing = ASpacing; }
	bool focusRectVisible() const { return FFocusRectVisible; }
	void setFocusRectVisible(bool AVisible) { FFocusRectVisible = AVisible; }
	bool defaultBranchItemEnabled() const { return FDefaultBranchEnabled; }
	void setDefaultBranchItemEnabled(bool AEnabled) { FDefaultBranchEnabled = AEnabled; }
	QMargins contentsMargins() const { return FMargins; }
	void setContentsMargings(const QMargins &AMargins) { FMargins = AMargins; }
	BlinkMode blinkMode() const { return FBlinkMode; }
	void setBlinkMode(BlinkMode AMode) { FBlinkMode = AMode; }
public:
	int editRole() const { return FEditRole; }
	void setEditRole(int ARole) { FEditRole = ARole; }
	quint32 editItemId() const { return FEditItemId; }
	void setEditItemId(quint32 AItemId) { FEditItemId = AItemId; }
	AdvancedDelegateEditProxy *editProxy() const { return FEditProxy; }
	void setEditProxy(AdvancedDelegateEditProxy *AProxy) { FEditProxy = AProxy; }
	const QItemEditorFactory *editorFactory() const;
public:
	void paint(QPainter *APainter, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
	QSize sizeHint(const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
	QWidget *createEditor(QWidget *AParent, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
	void setEditorData(QWidget *AEditor, const QModelIndex &AIndex) const;
	void setModelData(QWidget *AEditor, QAbstractItemModel *AModel, const QModelIndex &AIndex) const;
	void updateEditorGeometry(QWidget *AEditor, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
public:
	AdvancedDelegateItems getIndexItems(const QModelIndex &AIndex, const QStyleOptionViewItemV4 &AIndexOption) const;
	QStyleOptionViewItemV4 indexStyleOption(const QStyleOptionViewItem &AOption, const QModelIndex &AIndex, bool ACorrect=false) const;
	QStyleOptionViewItemV4 itemStyleOption(const AdvancedDelegateItem &AItem, const QStyleOptionViewItemV4 &AIndexOption) const;
	ItemsLayout *createItemsLayout(const AdvancedDelegateItems &AItems, const QStyleOptionViewItemV4 &AIndexOption) const;
	void destroyItemsLayout(ItemsLayout *ALayout) const;
public:
	QRect itemRect(quint32 AItemId, const ItemsLayout *ALayout, const QRect &AGeometry) const;
	QRect itemRect(quint32 AItemId, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
	quint32 itemAt(const QPoint &APoint, const ItemsLayout *ALayout, const QRect &AGeometry) const;
	quint32 itemAt(const QPoint &APoint, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex) const;
public:
	static bool isItemVisible(const AdvancedDelegateItem &AItem, const QStyleOptionViewItemV4 &AItemOption);
	static QSize itemSizeHint(const AdvancedDelegateItem &AItem, const QStyleOptionViewItemV4 &AItemOption);
signals:
	void updateBlinkItems();
protected:
	void drawBackground(QPainter *APainter, const QStyleOptionViewItemV4 &AIndexOption) const;
	void drawFocusRect(QPainter *APainter, const QStyleOptionViewItemV4 &AIndexOption, const QRect &ARect) const;
protected:
	bool editorEvent(QEvent *AEvent, QAbstractItemModel *AModel, const QStyleOptionViewItem &AOption, const QModelIndex &AIndex);
protected slots:
	void onBlinkTimerTimeout();
private:
	int FItemsRole;
	int FVerticalSpacing;
	int FHorizontalSpacing;
	bool FFocusRectVisible;
	bool FDefaultBranchEnabled;
	QMargins FMargins;
private:
	qreal FBlinkOpacity;
	BlinkMode FBlinkMode;
	QTimer FBlinkTimer;
private:
	int FEditRole;
	quint32 FEditItemId;
	AdvancedDelegateEditProxy *FEditProxy;
};

#endif // ADVANCEDITEMDELEGATE_H
