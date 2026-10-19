#include "roomsidebarstate.h"

#include <iostream>
#include <utility>

static bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << "FAIL: " << description << '\n';
	return condition;
}

int main()
{
	ProtocolRoom room;
	room.id = QStringLiteral("!room:test");
	room.name = QStringLiteral("Room");
	room.isEncrypted = true;
	ProtocolRosterEntry alice;
	alice.id = QStringLiteral("@alice:test");
	alice.name = QStringLiteral("Alice");
	alice.avatarUrl = QStringLiteral("mxc://example/alice");
	alice.hasVerificationState = true;
	alice.isVerified = true;
	room.members.append(alice);

	const QByteArray initialSnapshot = RoomSidebarState::snapshot(room, QStringLiteral("account"));
	room.lastEventId = QStringLiteral("$message-event");
	room.lastMessage = QStringLiteral("new chat message");
	room.notificationCount = 3;
	room.members[0].presence = QStringLiteral("online");
	bool passed = check(initialSnapshot == RoomSidebarState::snapshot(room, QStringLiteral("account")),
		"non-sidebar sync changes do not invalidate the member sidebar");

	ProtocolRosterEntry bob;
	bob.id = QStringLiteral("@bob:test");
	bob.name = QStringLiteral("Bob");
	room.members.append(bob);
	const QByteArray twoMembers = RoomSidebarState::snapshot(room, QStringLiteral("account"));
	std::swap(room.members[0], room.members[1]);
	passed &= check(twoMembers == RoomSidebarState::snapshot(room, QStringLiteral("account")),
		"member ordering alone does not invalidate the sidebar");
	room.members[0].avatarUrl = QStringLiteral("mxc://example/changed");
	passed &= check(twoMembers != RoomSidebarState::snapshot(room, QStringLiteral("account")),
		"member avatar changes invalidate the cached snapshot");
	room.members[0].avatarUrl.clear();
	room.members[0].name = QStringLiteral("Changed name");
	passed &= check(twoMembers != RoomSidebarState::snapshot(room, QStringLiteral("account")),
		"member display-name changes invalidate the cached snapshot");

	passed &= check(RoomSidebarState::canReuseSidebar(QStringLiteral("account"), room.id,
		initialSnapshot, QStringLiteral("account"), room.id, initialSnapshot),
		"same room and sidebar state reuse the existing widget tree");
	passed &= check(!RoomSidebarState::canReuseSidebar(QStringLiteral("other-account"), room.id,
		initialSnapshot, QStringLiteral("account"), room.id, initialSnapshot),
		"a different account cannot reuse the existing sidebar");
	passed &= check(!RoomSidebarState::canReuseSidebar(QStringLiteral("account"), room.id,
		initialSnapshot, QStringLiteral("account"), room.id, twoMembers),
		"changed member state invalidates sidebar reuse");
	passed &= check(RoomSidebarState::avatarAction(false, false) == RoomSidebarState::AvatarAction::Placeholder,
		"placeholder is used only when there is no cached or loaded avatar");
	passed &= check(RoomSidebarState::avatarAction(false, true) == RoomSidebarState::AvatarAction::LoadCached,
		"cached avatar is loaded instead of showing the dummy");
	passed &= check(RoomSidebarState::avatarAction(true, false) == RoomSidebarState::AvatarAction::ReuseLoaded,
		"already loaded avatar is retained when no cached path is available");
	passed &= check(RoomSidebarState::avatarAction(true, true) == RoomSidebarState::AvatarAction::ReuseLoaded,
		"loaded avatar remains visible while an updated cached image is loaded");
	return passed ? 0 : 1;
}
