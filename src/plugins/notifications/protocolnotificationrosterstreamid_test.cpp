#include "protocolnotificationrosterstreamid.h"

#include <iostream>

int main()
{
	ProtocolNotification notification;
	notification.accountId = QStringLiteral("persistent-account-id");
	notification.streamId = QStringLiteral("roster-stream-id");
	if (protocolNotificationRosterStreamId(notification) != notification.streamId) {
		std::cerr << "roster lookup must use streamId rather than accountId\n";
		return 1;
	}

	notification.streamId.clear();
	if (protocolNotificationRosterStreamId(notification) != notification.accountId) {
		std::cerr << "legacy notifications without streamId must fall back to accountId\n";
		return 1;
	}
	return 0;
}