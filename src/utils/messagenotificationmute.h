#ifndef MESSAGENOTIFICATIONMUTE_H
#define MESSAGENOTIFICATIONMUTE_H

#include <definitions/optionvalues.h>
#include <utils/jid.h>
#include <utils/options.h>

#include <QCryptographicHash>
#include <QRegularExpression>

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

inline QByteArray migrateLegacyMessageNotificationMuteOptionsXml(const QByteArray &AXml)
{
	const QString source = QString::fromUtf8(AXml);
	static const QRegularExpression sectionExpression(
		QStringLiteral(R"(<muted-notification-targets(?:\s[^>]*)?>)"));
	static const QRegularExpression legacyEntryExpression(QStringLiteral(
		R"(<([0-9a-fA-F]{64})(?:\s+[^>]*)?>([^<]*)</\1\s*>|<([0-9a-fA-F]{64})(?:\s+[^>]*)?\s*/>)"));

	const QRegularExpressionMatch section = sectionExpression.match(source);
	if (!section.hasMatch())
		return AXml;

	const int contentStart = section.capturedEnd();
	const int contentEnd = source.indexOf(QStringLiteral("</muted-notification-targets>"), contentStart);
	if (contentEnd < 0)
		return AXml;

	const QString content = source.mid(contentStart, contentEnd - contentStart);
	QRegularExpressionMatchIterator matches = legacyEntryExpression.globalMatch(content);
	QString migratedContent;
	int copiedUntil = 0;
	bool changed = false;
	while (matches.hasNext())
	{
		const QRegularExpressionMatch match = matches.next();
		migratedContent += content.mid(copiedUntil, match.capturedStart() - copiedUntil);
		const QString key = !match.captured(1).isEmpty() ? match.captured(1) : match.captured(3);
		if (match.captured(2).trimmed().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
			migratedContent += QStringLiteral("<target ns=\"%1\" type=\"1\">true</target>").arg(key);
		copiedUntil = match.capturedEnd();
		changed = true;
	}

	if (!changed)
		return AXml;

	migratedContent += content.mid(copiedUntil);
	QString migrated = source;
	migrated.replace(contentStart, contentEnd - contentStart, migratedContent);
	return migrated.toUtf8();
}

inline bool messageNotificationMuted(const QString &AStreamId, const QString &ATargetId)
{
	const QString key = messageNotificationMuteKey(AStreamId, ATargetId);
	if (!Options::hasNode(OPV_MESSAGES_MUTED_TARGETS))
		return false;

	const OptionsNode targets = Options::node(OPV_MESSAGES_MUTED_TARGETS);
	if (targets.hasNode("target", key))
		return targets.node("target", key).value().toBool();
	return targets.hasNode(key) && targets.node(key).value().toBool();
}

inline bool messageNotificationMuted(const Jid &AStreamJid, const Jid &ATargetJid)
{
	return messageNotificationMuted(AStreamJid.pBare(), ATargetJid.pBare());
}

inline void setMessageNotificationMuted(const QString &AStreamId, const QString &ATargetId, bool AMuted)
{
	const QString key = messageNotificationMuteKey(AStreamId, ATargetId);
	if (!AMuted && !Options::hasNode(OPV_MESSAGES_MUTED_TARGETS))
		return;

	OptionsNode targets = Options::node(OPV_MESSAGES_MUTED_TARGETS);
	if (AMuted)
		targets.node("target", key).setValue(true);
	else
		targets.removeNode("target", key);

	// Remove entries written by the old dynamic-element-name format.
	targets.removeNode(key);
}

inline void setMessageNotificationMuted(const Jid &AStreamJid, const Jid &ATargetJid, bool AMuted)
{
	setMessageNotificationMuted(AStreamJid.pBare(), ATargetJid.pBare(), AMuted);
}

#endif // MESSAGENOTIFICATIONMUTE_H
