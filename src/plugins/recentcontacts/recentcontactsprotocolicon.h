#ifndef RECENTCONTACTSPROTOCOLICON_H
#define RECENTCONTACTSPROTOCOLICON_H

#include <QString>
#include <QList>
#include <QStringList>
#include <interfaces/iprotocolaccount.h>
#include <utils/advanceditemdelegate.h>

namespace RecentContactsProtocolIcon
{
QString menuIconKey(IProtocolAccount::ProtocolKind AKind, bool AFavorite);
QStringList accountIdCandidates(const QString &AItemAccountId, const QString &AProxyAccountId,
	const QString &AIndexAccountId);
QString accountIdForRosterStream(const QString &ARosterId, const QString &AStreamId,
	const QString &AAccountId);
QList<quint32> favoriteLabelIds();
quint32 protocolLabelId();
bool favoriteBulbVisible(bool AFavorite, bool AHasNotification);
}

#endif // RECENTCONTACTSPROTOCOLICON_H
