#include <iostream>
#include <string>
#include "protocolmessagerouting.h"

namespace {
struct TestProvider
{
    std::string stream;
    std::string protocol;
};

bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << description << " failed\n";
    return condition;
}
}

int main()
{
    const TestProvider matrix{"@Matrix", "matrix"};
    const TestProvider meshcore{"@Meshcore", "meshcore"};

    bool passed = true;
    passed &= check(ProtocolMessageRouting::hasExactStream(meshcore.stream,
        std::string("@Meshcore")),
        "exact stream selects its provider");
    passed &= check(!ProtocolMessageRouting::hasExactStream(matrix.stream,
        std::string("@Unknown")),
        "unknown stream does not fall back to Matrix");

    passed &= check(ProtocolMessageRouting::matchesProtocol(meshcore.protocol,
        std::string("meshcore")),
        "matching provider protocol accepts message");
    passed &= check(!ProtocolMessageRouting::matchesProtocol(meshcore.protocol, matrix.protocol),
        "different provider protocol rejects message");
    passed &= check(!ProtocolMessageRouting::matchesProtocol(std::string(), meshcore.protocol),
        "empty message protocol rejects message");

    return passed ? 0 : 1;
}
