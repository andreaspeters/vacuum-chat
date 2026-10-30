#ifndef ADVANCEDDELEGATEITEM_H
#define ADVANCEDDELEGATEITEM_H

#include <QMap>
#include <QDataStream>
#include <QMetaType>
#include <QSizePolicy>
#include <QStyleOption>
#include <QVariant>
#include <QWidget>

#include "utilsexport.h"

struct UTILS_EXPORT AdvancedDelegateItem
{
	enum Kind {
		Null,
		Branch,
		CheckBox,
		Decoration,
		Display,
		Stretch,
		CustomData,
		CustomWidget
	};
	enum Position {
		Top,
		MiddleLeft,
		MiddleCenter,
		MiddleRight,
		Bottom
	};
	enum Hint {
		SizeHint,
		FontHint,
		FontStyle,
		FontWeight,
		FontSize,
		FontSizeDelta,
		FontUnderline,
		StatesForceOn,
		StatesForceOff,
		Foreground,
		Backgound,
		TextElideMode,
		TextAlignment,
		Opacity
	};
	enum Flag {
		Hidden         =0x00000001,
		Blink          =0x00000002
	};
	struct ExplicitData {
		int refs;
		quint32 id;
		quint8 kind;
		quint32 flags;
		QVariant data;
		QWidget *widget;
		QSizePolicy sizePolicy;
		QStyle::State showStates;
		QStyle::State hideStates;
		QMap<int,QVariant> hints;
		QMap<int,QVariant> properties;
	};
	struct ContextData {
		QVariant value;
		qreal blinkOpacity;
	};

	AdvancedDelegateItem(quint32 AId = NullId);
	AdvancedDelegateItem(const AdvancedDelegateItem &AOther);
	~AdvancedDelegateItem();

	void detach();
	AdvancedDelegateItem &operator =(const AdvancedDelegateItem &AOther);

	static const quint16 AlignRightOrderMask = 0x8000;
	static quint32 makeId(quint8 APosition, quint8 AFloor, quint16 AOrder);
	static quint32 makeStretchId(quint8 APosition, quint8 AFloor);
	static quint8 getPosition(quint32 AItemId);
	static quint8 getFloor(quint32 AItemId);
	static quint16 getOrder(quint32 AItemId);

	static const quint32 NullId;        //= 0
	static const quint32 BranchId;      //= makeId(MiddleLeft,128,10)
	static const quint32 CheckStateId;  //= makeId(MiddleLeft,128,100)
	static const quint32 DecorationId;  //= makeId(MiddleLeft,128,500)
	static const quint32 DisplayId;     //= makeId(MiddleCenter,128,500)

	ContextData *c;
	ExplicitData *d;
};
typedef QMap<quint32, AdvancedDelegateItem> AdvancedDelegateItems;

UTILS_EXPORT QDataStream &operator>>(QDataStream &AStream, AdvancedDelegateItem &AItem);
UTILS_EXPORT QDataStream &operator<<(QDataStream &AStream, const AdvancedDelegateItem &AItem);

Q_DECLARE_METATYPE(AdvancedDelegateItem)
Q_DECLARE_METATYPE(AdvancedDelegateItems)

#endif // ADVANCEDDELEGATEITEM_H
