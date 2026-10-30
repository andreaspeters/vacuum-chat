#include "advanceddelegateitem.h"

#include <QDataStream>
#include <QMetaType>
#include <QStyle>

namespace
{
void registerAdvancedDelegateItemMetaTypes()
{
    static bool registered = false;
    if (!registered)
    {
        registered = true;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        qRegisterMetaTypeStreamOperators<AdvancedDelegateItem>("AdvancedDelegateItem");
        qRegisterMetaTypeStreamOperators<AdvancedDelegateItems>("AdvancedDelegateItems");
#else
        qRegisterMetaType<AdvancedDelegateItem>("AdvancedDelegateItem");
        qRegisterMetaType<AdvancedDelegateItems>("AdvancedDelegateItems");
#endif
    }
}
}

const quint32 AdvancedDelegateItem::NullId = 0;
const quint32 AdvancedDelegateItem::BranchId = AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleLeft, 128, 10);
const quint32 AdvancedDelegateItem::CheckStateId = AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleLeft, 128, 100);
const quint32 AdvancedDelegateItem::DecorationId = AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleLeft, 128, 500);
const quint32 AdvancedDelegateItem::DisplayId = AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleCenter, 128, 500);

AdvancedDelegateItem::AdvancedDelegateItem(quint32 AId)
{
    d = new ExplicitData;
    d->refs = 1;
    d->id = AId;
    d->kind = Null;
    d->flags = 0;
    d->showStates = QStyle::State();
    d->hideStates = QStyle::State();
    d->widget = NULL;

    c = new ContextData;
    c->blinkOpacity = 1.0;
    registerAdvancedDelegateItemMetaTypes();
}

AdvancedDelegateItem::AdvancedDelegateItem(const AdvancedDelegateItem &AOther)
{
    d = AOther.d;
    d->refs++;
    c = new ContextData;
    *c = *AOther.c;
}

AdvancedDelegateItem::~AdvancedDelegateItem()
{
    if (--d->refs == 0)
        delete d;
    delete c;
}

void AdvancedDelegateItem::detach()
{
    if (d->refs > 1)
    {
        ExplicitData *old = d;
        old->refs--;
        d = new ExplicitData;
        *d = *old;
        d->refs = 1;
    }
}

AdvancedDelegateItem &AdvancedDelegateItem::operator=(const AdvancedDelegateItem &AOther)
{
    if (d != AOther.d)
    {
        if (--d->refs == 0)
            delete d;
        d = AOther.d;
        d->refs++;
    }
    *c = *AOther.c;
    return *this;
}

quint32 AdvancedDelegateItem::makeId(quint8 APosition, quint8 AFloor, quint16 AOrder)
{
    return (((quint32)(APosition + 1)) << 24) + (((quint32)AFloor) << 16) + (quint32)AOrder;
}

quint32 AdvancedDelegateItem::makeStretchId(quint8 APosition, quint8 AFloor)
{
    return makeId(APosition, AFloor, AlignRightOrderMask);
}

quint8 AdvancedDelegateItem::getPosition(quint32 AItemId)
{
    return (AItemId >> 24) - 1;
}

quint8 AdvancedDelegateItem::getFloor(quint32 AItemId)
{
    return (AItemId & 0x00FF0000) >> 16;
}

quint16 AdvancedDelegateItem::getOrder(quint32 AItemId)
{
    return AItemId & 0x0000FFFF;
}

QDataStream &operator<<(QDataStream &AStream, const AdvancedDelegateItem &AItem)
{
    AStream << AItem.d->id
            << AItem.d->kind
            << AItem.d->flags
            << AItem.d->data
            << (qint64)AItem.d->widget
            << AItem.d->sizePolicy
            << (int)AItem.d->showStates
            << (int)AItem.d->hideStates
            << AItem.d->hints
            << AItem.d->properties
            << AItem.c->value
            << AItem.c->blinkOpacity;
    return AStream;
}

QDataStream &operator>>(QDataStream &AStream, AdvancedDelegateItem &AItem)
{
    qint64 widget;
    int showStates, hideStates;
    AStream >> AItem.d->id
            >> AItem.d->kind
            >> AItem.d->flags
            >> AItem.d->data
            >> widget
            >> AItem.d->sizePolicy
            >> showStates
            >> hideStates
            >> AItem.d->hints
            >> AItem.d->properties
            >> AItem.c->value
            >> AItem.c->blinkOpacity;
    AItem.d->widget = (QWidget *)widget;
    AItem.d->showStates = (QStyle::State)showStates;
    AItem.d->hideStates = (QStyle::State)hideStates;
    return AStream;
}
