#include "vcarddialogbindingpolicy.h"

#include <QPair>
#include <QString>
#include <cstdio>

int main()
{
    const QString contactJid = QStringLiteral("alice@example.test");
    const QString firstStreamJid = QStringLiteral("alice@example.test/device-one");
    const QString secondStreamJid = QStringLiteral("alice@example.test/device-two");
    const QString otherContactJid = QStringLiteral("bob@example.test");

    const VCardDialogBindingPolicy::DialogKey firstKey =
        VCardDialogBindingPolicy::makeDialogKey(firstStreamJid, contactJid);
    const VCardDialogBindingPolicy::DialogKey sameBindingKey =
        VCardDialogBindingPolicy::makeDialogKey(firstStreamJid, contactJid);
    const VCardDialogBindingPolicy::DialogKey otherStreamKey =
        VCardDialogBindingPolicy::makeDialogKey(secondStreamJid, contactJid);
    const VCardDialogBindingPolicy::DialogKey otherContactKey =
        VCardDialogBindingPolicy::makeDialogKey(firstStreamJid, otherContactJid);

    if (firstKey != sameBindingKey) {
        std::fprintf(stderr, "Identical stream/contact bindings should reuse one dialog key\n");
        return 1;
    }
    if (firstKey == otherStreamKey) {
        std::fprintf(stderr, "The same contact on different streams must use different dialog keys\n");
        return 1;
    }
    if (firstKey == otherContactKey) {
        std::fprintf(stderr, "Different contacts on one stream must use different dialog keys\n");
        return 1;
    }
    return 0;
}
