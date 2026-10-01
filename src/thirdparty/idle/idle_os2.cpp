/*
 * idle_os2.cpp - detect desktop idle time on OS/2
 *
 * WinQueryMsgTime() returns the posting time of the last message removed
 * from the current Presentation Manager queue.  DosQuerySysInfo(QSV_MS_COUNT)
 * uses the same millisecond system-time base, so their difference is the
 * elapsed time since the last keyboard/mouse/window message.
 */

#include "idle.h"

#define INCL_DOSMISC
#define INCL_WIN
#include <os2.h>

class IdlePlatform::Private
{
public:
	Private() : hab(0) {}

	HAB hab;
};

IdlePlatform::IdlePlatform()
{
	d = new Private;
}

IdlePlatform::~IdlePlatform()
{
	delete d;
}

bool IdlePlatform::init()
{
	if (d->hab)
		return true;

	/* HWND_DESKTOP resolves to the PM anchor block of the current thread. */
	d->hab = WinQueryAnchorBlock(HWND_DESKTOP);
	return d->hab != 0;
}

int IdlePlatform::secondsIdle()
{
	if (!d->hab)
		return 0;

	ULONG now = 0;
	if (DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &now, sizeof(now)) != 0)
		return 0;

	ULONG last = WinQueryMsgTime(d->hab);
	if (last == 0)
		return 0;

	/* ULONG subtraction intentionally handles the 32-bit tick wraparound. */
	return static_cast<int>((now - last) / 1000UL);
}
