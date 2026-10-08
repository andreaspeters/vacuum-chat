#include "recentcontactsprotocolicon.h"

#include <definitions/menuicons.h>

QString RecentContactsProtocolIcon::menuIconKey(IProtocolAccount::ProtocolKind AKind, bool AFavorite)
{
    if (!AFavorite)
        return QString();

    switch (AKind)
    {
    case IProtocolAccount::ProtocolXmpp:
        return QString::fromLatin1(MNI_RECENT_PROTOCOL_XMPP);
    case IProtocolAccount::ProtocolMatrix:
        return QString::fromLatin1(MNI_RECENT_PROTOCOL_MATRIX);
    case IProtocolAccount::ProtocolMeshCore:
        return QString::fromLatin1(MNI_RECENT_PROTOCOL_MESHCORE);
    default:
        return QString();
    }
}

QStringList RecentContactsProtocolIcon::accountIdCandidates(const QString &AItemAccountId,
	const QString &AProxyAccountId, const QString &AIndexAccountId)
{
	QStringList accountIds;
	const QString candidates[] = { AItemAccountId, AProxyAccountId, AIndexAccountId };
	for (const QString &accountId : candidates)
		if (!accountId.isEmpty() && !accountIds.contains(accountId))
			accountIds.append(accountId);
	return accountIds;
}

QString RecentContactsProtocolIcon::accountIdForRosterStream(const QString &ARosterId,
	const QString &AStreamId, const QString &AAccountId)
{
	return !ARosterId.isEmpty() && ARosterId == AStreamId ? AAccountId : QString();
}

quint32 RecentContactsProtocolIcon::protocolLabelId()
{
    return AdvancedDelegateItem::makeId(AdvancedDelegateItem::MiddleCenter, 128, 400);
}

QList<quint32> RecentContactsProtocolIcon::favoriteLabelIds()
{
    return QList<quint32>() << protocolLabelId();
}

bool RecentContactsProtocolIcon::favoriteBulbVisible(bool AFavorite, bool AHasNotification,
	bool AFavoriteRosterRow)
{
	return AFavorite && !AHasNotification && !AFavoriteRosterRow;
}
