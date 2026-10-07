#ifndef MATRIXSESSIONPOLICY_H
#define MATRIXSESSIONPOLICY_H

#include <interfaces/ipresence.h>
#include <QString>

namespace MatrixSessionPolicy
{
enum class PresenceAction
{
	Disconnect,
	Connect,
	UpdateRemotePresence
};

inline PresenceAction actionForPresence(int show, bool connected)
{
	if (show == IPresence::Offline || show == IPresence::Error)
		return PresenceAction::Disconnect;
	return connected ? PresenceAction::UpdateRemotePresence : PresenceAction::Connect;
}

inline bool acceptLoginSuccess(bool connectRequested)
{
	return connectRequested;
}

inline bool matchesLoginUserId(const QString &requestedUserId, const QString &authenticatedUserId)
{
	const QString requested = requestedUserId.trimmed();
	const QString authenticated = authenticatedUserId.trimmed();
	if (requested.isEmpty() || !authenticated.startsWith(QLatin1Char('@')))
		return false;

	const int authenticatedSeparator = authenticated.indexOf(QLatin1Char(':'), 1);
	if (authenticatedSeparator <= 1 || authenticatedSeparator == authenticated.size() - 1)
		return false;
	if (requested == authenticated)
		return true;

	if (requested.startsWith(QLatin1Char('@')) &&
		requested.indexOf(QLatin1Char(':')) > 1)
		return false;

	QString requestedLocalpart = requested;
	if (requestedLocalpart.startsWith(QLatin1Char('@')))
		requestedLocalpart.remove(0, 1);
	const int requestedSeparator = requestedLocalpart.indexOf(QLatin1Char(':'));
	if (requestedSeparator >= 0)
		return QLatin1Char('@') + requestedLocalpart == authenticated;

	const QString authenticatedLocalpart = authenticated.mid(1,
		authenticatedSeparator - 1);
	return requestedLocalpart == authenticatedLocalpart;
}
}

#endif // MATRIXSESSIONPOLICY_H
