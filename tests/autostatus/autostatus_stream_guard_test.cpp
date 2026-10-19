#include "autostatusstreamguard.h"

#include <iostream>

struct FakeXmppStream
{
    bool open;

    bool isOpen() const
    {
        return open;
    }
};

int main()
{
    const FakeXmppStream *missingStream = nullptr;
    if (AutoStatusInternal::isXmppStreamOpen(missingStream)) {
        std::cerr << "a missing XMPP stream must not be reported open\n";
        return 1;
    }

    const FakeXmppStream openStream = {true};
    if (!AutoStatusInternal::isXmppStreamOpen(&openStream)) {
        std::cerr << "an open XMPP stream was rejected\n";
        return 1;
    }

    const FakeXmppStream closedStream = {false};
    if (AutoStatusInternal::isXmppStreamOpen(&closedStream)) {
        std::cerr << "a closed XMPP stream was reported open\n";
        return 1;
    }

    std::cout << "AutoStatus stream guard tests passed\n";
    return 0;
}
