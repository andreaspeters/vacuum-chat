#ifndef AUTOSTATUSSTREAMGUARD_H
#define AUTOSTATUSSTREAMGUARD_H

namespace AutoStatusInternal {

template<typename Stream>
inline bool isXmppStreamOpen(const Stream *stream)
{
    return stream != nullptr && stream->isOpen();
}

} // namespace AutoStatusInternal

#endif // AUTOSTATUSSTREAMGUARD_H
