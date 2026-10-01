#ifndef MESSAGENOTIFICATIONMUTE_H
#define MESSAGENOTIFICATIONMUTE_H

#include <definitions/optionvalues.h>
#include <utils/jid.h>
#include <utils/options.h>

#include <QCryptographicHash>

inline QString messageNotificationMuteIdentity(const QString &AId)
{
	if (AId.startsWith(QLatin1Char('!')) || AId.startsWith(QLatin1Char('@')))
		return AId;

	const Jid jid(AId);
	return jid.isValid() ? jid.pBare() : AId;
}

inline QString messageNotificationMuteKey(const QString &AStreamId, const QString &ATargetId)
{
	const QByteArray identity = (messageNotificationMuteIdentity(AStreamId) + QLatin1Char('\n')
		+ messageNotificationMuteIdentity(ATargetId)).toUtf8();
	return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

inline bool messageNotificationMuted(const QString &AStreamId, const QString &ATargetId)
{
	return Options::node(OPV_MESSAGES_MUTED_TARGETS).value(
		messageNotificationMuteKey(AStreamId, ATargetId)).toBool();
}

inline bool messageNotificationMuted(const Jid &AStreamJid, const Jid &ATargetJid)
{
	return messageNotificationMuted(AStreamJid.pBare(), ATargetJid.pBare());
}

inline void setMessageNotificationMuted(const QString &AStreamId, const QString &ATargetId, bool AMuted)
{
	Options::node(OPV_MESSAGES_MUTED_TARGETS).setValue(
		AMuted, messageNotificationMuteKey(AStreamId, ATargetId));
}

inline void setMessageNotificationMuted(const Jid &AStreamJid, const Jid &ATargetJid, bool AMuted)
{
	setMessageNotificationMuted(AStreamJid.pBare(), ATargetJid.pBare(), AMuted);
}

#endif // MESSAGENOTIFICATIONMUTE_H
