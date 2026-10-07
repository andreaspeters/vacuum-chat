#ifndef VCARD_DIALOG_BINDING_POLICY_H
#define VCARD_DIALOG_BINDING_POLICY_H

#include <QPair>
#include <QString>

namespace VCardDialogBindingPolicy
{
typedef QPair<QString, QString> DialogKey;

inline DialogKey makeDialogKey(const QString &streamJid, const QString &contactJid)
{
    return qMakePair(streamJid, contactJid);
}
}

#endif // VCARD_DIALOG_BINDING_POLICY_H
