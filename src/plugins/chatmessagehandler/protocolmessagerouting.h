#ifndef PROTOCOLMESSAGEROUTING_H
#define PROTOCOLMESSAGEROUTING_H

namespace ProtocolMessageRouting
{
template<typename String>
bool hasExactStream(const String &providerStream, const String &requestedStream)
{
	return providerStream.size() > 0 && requestedStream.size() > 0 &&
		providerStream == requestedStream;
}

template<typename String>
bool matchesProtocol(const String &messageProtocol, const String &providerProtocol)
{
	return messageProtocol.size() > 0 && providerProtocol.size() > 0 &&
		messageProtocol == providerProtocol;
}
}

#endif // PROTOCOLMESSAGEROUTING_H
