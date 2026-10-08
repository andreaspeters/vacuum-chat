#ifndef MESSAGEDECORATIONPOLICY_H
#define MESSAGEDECORATIONPOLICY_H

namespace MessageDecorationPolicy
{
enum Route
{
	InsertIntoDocument,
	StyleWidgetHandled
};

inline Route routeForStyleResult(bool styleHandled)
{
	return styleHandled ? StyleWidgetHandled : InsertIntoDocument;
}
}

#endif // MESSAGEDECORATIONPOLICY_H
