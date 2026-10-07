#include "matrixsessionpolicy.h"

#include <interfaces/ipresence.h>
#include <iostream>

namespace
{
bool check(bool condition, const char *message)
{
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
	return condition;
}
}

int main()
{
	bool passed = true;
	using MatrixSessionPolicy::PresenceAction;

	passed &= check(MatrixSessionPolicy::actionForPresence(IPresence::Offline, true) ==
		PresenceAction::Disconnect, "offline selection disconnects an active session");
	passed &= check(MatrixSessionPolicy::actionForPresence(IPresence::Offline, false) ==
		PresenceAction::Disconnect, "offline selection remains disconnected when already offline");
	passed &= check(MatrixSessionPolicy::actionForPresence(IPresence::Online, false) ==
		PresenceAction::Connect, "online selection initiates login when disconnected");
	passed &= check(MatrixSessionPolicy::actionForPresence(IPresence::Away, false) ==
		PresenceAction::Connect, "non-offline selection initiates login when disconnected");
	passed &= check(MatrixSessionPolicy::actionForPresence(IPresence::Online, true) ==
		PresenceAction::UpdateRemotePresence, "online selection updates presence on an active session");
	passed &= check(!MatrixSessionPolicy::acceptLoginSuccess(false),
		"a late login success is rejected after offline was requested");
	passed &= check(MatrixSessionPolicy::acceptLoginSuccess(true),
		"login success is accepted while an online connection is requested");
	passed &= check(MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("@alice:example.org"), QStringLiteral("@alice:example.org")),
		"a fully qualified configured Matrix user must match exactly");
	passed &= check(MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("alice"), QStringLiteral("@alice:example.org")),
		"a configured localpart matches the server's canonical Matrix user ID");
	passed &= check(MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("@alice"), QStringLiteral("@alice:example.org")),
		"a localpart prefixed with @ matches the server's canonical Matrix user ID");
	passed &= check(!MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("bob"), QStringLiteral("@alice:example.org")),
		"a different localpart is rejected");
	passed &= check(!MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("@alice:other.org"), QStringLiteral("@alice:example.org")),
		"a different fully qualified Matrix user ID is rejected");
	passed &= check(!MatrixSessionPolicy::matchesLoginUserId(
		QStringLiteral("alice"), QStringLiteral("alice")),
		"a non-canonical server identity is rejected");
	return passed ? 0 : 1;
}
