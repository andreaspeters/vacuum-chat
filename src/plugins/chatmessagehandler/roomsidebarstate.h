#ifndef ROOMSIDEBARSTATE_H
#define ROOMSIDEBARSTATE_H

#include <interfaces/iprotocolroster.h>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>

namespace RoomSidebarState {

inline QByteArray snapshot(const ProtocolRoom &ARoom, const QString &AAccountId)
{
	QList<ProtocolRosterEntry> members = ARoom.members;
	std::sort(members.begin(), members.end(), [](const ProtocolRosterEntry &left,
		const ProtocolRosterEntry &right) { return left.id < right.id; });

	QByteArray result;
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream << AAccountId << ARoom.id << ARoom.name << ARoom.isEncrypted
		<< static_cast<quint32>(members.size());
	for (const ProtocolRosterEntry &member : members)
		stream << member.id << member.name << member.avatarUrl
			<< member.hasVerificationState << member.isVerified;
	return result;
}

inline bool canReuseSidebar(const QString &AExistingAccountId, const QString &AExistingRoomId,
	const QByteArray &AExistingSnapshot, const QString &AAccountId, const QString &ARoomId,
	const QByteArray &ASnapshot)
{
	return !ARoomId.isEmpty() && AExistingAccountId == AAccountId &&
		AExistingRoomId == ARoomId && AExistingSnapshot == ASnapshot;
}

enum class AvatarAction
{
	ReuseLoaded,
	LoadCached,
	Placeholder
};

inline AvatarAction avatarAction(bool AHasLoadedAvatar, bool AHasCachedPath)
{
	if (AHasLoadedAvatar)
		return AvatarAction::ReuseLoaded;
	return AHasCachedPath ? AvatarAction::LoadCached : AvatarAction::Placeholder;
}

} // namespace RoomSidebarState

#endif // ROOMSIDEBARSTATE_H
