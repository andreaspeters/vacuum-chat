#ifndef PROTOCOLNOTIFICATIONROSTERSTREAMID_H
#define PROTOCOLNOTIFICATIONROSTERSTREAMID_H

#include <interfaces/iprotocolnotifications.h>

static inline QString protocolNotificationRosterStreamId(const ProtocolNotification &notification)
{
	return notification.streamId.isEmpty() ? notification.accountId : notification.streamId;
}

#endif // PROTOCOLNOTIFICATIONROSTERSTREAMID_H