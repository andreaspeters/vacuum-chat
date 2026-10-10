#include <interfaces/iprotocolaccount.h>

#include <iostream>

int main()
{
    using ProtocolKind = IProtocolAccount::ProtocolKind;
    struct Case { QString name; ProtocolKind expected; };
    const Case cases[] = {
        {QString(), IProtocolAccount::ProtocolXmpp},
        {QStringLiteral("jabber"), IProtocolAccount::ProtocolXmpp},
        {QStringLiteral("xmpp"), IProtocolAccount::ProtocolXmpp},
        {QStringLiteral("matrix"), IProtocolAccount::ProtocolMatrix},
        {QStringLiteral("meshcore"), IProtocolAccount::ProtocolMeshCore},
        {QStringLiteral("ax25"), IProtocolAccount::ProtocolAx25},
        {QStringLiteral("future-protocol"), IProtocolAccount::ProtocolUnknown}
    };
    for (const Case &test : cases) {
        if (IProtocolAccount::protocolKindForType(test.name) != test.expected) {
            std::cerr << "unexpected account protocol kind for " << test.name.toStdString() << '\n';
            return 1;
        }
    }
    return 0;
}
