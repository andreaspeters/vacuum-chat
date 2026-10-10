#include "ax25kisscodec.h"

#include <cstdio>
#include <limits>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool testKissEncodingAndIncrementalDecode()
{
    const QByteArray payload = QByteArray::fromHex("00c0db55");
    const QByteArray encoded = Ax25Kiss::KissCodec::encodeDataFrame(payload);
    if (!check(encoded == QByteArray::fromHex("c00000dbdcdbdd55c0"),
               "KISS encoding matches FEND/type/escaped-payload/FEND vector"))
        return false;

    Ax25Kiss::KissStreamDecoder decoder;
    if (!check(decoder.feed(encoded.left(4)).isEmpty(),
               "incomplete KISS frame is not emitted"))
        return false;
    const QList<QByteArray> decoded = decoder.feed(encoded.mid(4));
    return check(decoded.size() == 1 && decoded.first() == payload,
                 "split stream decodes one exact DATA payload");
}

bool testKissMultipleFramesAndFiltering()
{
    const QByteArray first = Ax25Kiss::KissCodec::encodeDataFrame(QByteArray("one"));
    const QByteArray second = Ax25Kiss::KissCodec::encodeDataFrame(QByteArray("two"));
    const QByteArray command = QByteArray::fromHex("c00142c0");
    const QByteArray otherPort = QByteArray::fromHex("c01042c0");
    Ax25Kiss::KissStreamDecoder decoder;
    const QList<QByteArray> decoded = decoder.feed(command + first + otherPort +
                                                    QByteArray::fromHex("c0c0") + second);
    return check(decoded.size() == 2 && decoded.at(0) == QByteArray("one") &&
                     decoded.at(1) == QByteArray("two"),
                 "parser ignores empty, non-DATA, and nonzero-port frames while continuing");
}

bool testKissMalformedAndOversizedFrames()
{
    Ax25Kiss::KissStreamDecoder decoder;
    const QByteArray malformed = QByteArray::fromHex("c000db55aa c0");
    const QByteArray valid = Ax25Kiss::KissCodec::encodeDataFrame(QByteArray("ok"));
    const QList<QByteArray> recovered = decoder.feed(malformed + valid);
    if (!check(recovered.size() == 1 && recovered.first() == QByteArray("ok"),
               "invalid escape discards only its frame and parser recovers"))
        return false;

    Ax25Kiss::KissStreamDecoder bounded(2);
    const QByteArray oversized = QByteArray::fromHex("c000010203c0");
    const QList<QByteArray> afterOversize = bounded.feed(
        oversized + Ax25Kiss::KissCodec::encodeDataFrame(QByteArray("ok")));
    if (!check(afterOversize.size() == 1 && afterOversize.first() == QByteArray("ok"),
               "oversized frame is bounded, discarded, and followed by a valid frame"))
        return false;

    Ax25Kiss::KissStreamDecoder veryLarge((std::numeric_limits<int>::max)());
    const QList<QByteArray> withLargeLimit = veryLarge.feed(
        Ax25Kiss::KissCodec::encodeDataFrame(QByteArray("ok")));
    return check(withLargeLimit.size() == 1 && withLargeLimit.first() == QByteArray("ok"),
                 "extreme frame limit cannot overflow the bounded decoder");
}

bool testAx25UiFrameKnownVectorAndRoundTrip()
{
    const QByteArray information = QByteArray::fromHex("01aabb");
    QByteArray frame;
    if (!check(Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("DL1AAA-7"), QStringLiteral("W1AW"), information, &frame),
               "valid AX.25 callsigns encode"))
        return false;

    if (!check(frame == QByteArray::fromHex("ae6282ae4040e08898628282826f03f001aabb"),
               "AX.25 UI address, command C-bits, control, PID, and payload match vector"))
        return false;

    Ax25Kiss::Ax25UiFrame decoded;
    if (!check(Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(frame, &decoded),
               "valid direct AX.25 UI frame decodes"))
        return false;
    return check(decoded.destination == QStringLiteral("W1AW") &&
                     decoded.source == QStringLiteral("DL1AAA-7") &&
                     decoded.information == information,
                 "AX.25 frame decode preserves payload and normalizes SSID zero");
}

bool testAx25UiFrameRejectsInvalidAddressesAndFrames()
{
    QByteArray frame;
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("DL1AA@"), QStringLiteral("W1AW"), QByteArray(), &frame),
               "invalid callsign characters are rejected"))
        return false;
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("DL1AAA-16"), QStringLiteral("W1AW"), QByteArray(), &frame),
               "SSID above 15 is rejected"))
        return false;
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("DL1AAAA"), QStringLiteral("W1AW"), QByteArray(), &frame),
               "callsign base above six characters is rejected"))
        return false;

    if (!Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
            QStringLiteral("DL1AAA-7"), QStringLiteral("W1AW"), QByteArray("x"), &frame))
        return false;
    Ax25Kiss::Ax25UiFrame ignored;
    QByteArray pollUi = frame;
    pollUi[14] = 0x13;
    Ax25Kiss::Ax25UiFrame polledDecoded;
    if (!check(Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(pollUi, &polledDecoded),
               "valid UI frame with the P/F bit set is accepted"))
        return false;

    QByteArray notUi = frame;
    notUi[14] = 0x2f;
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(notUi, &ignored),
               "non-UI control field is rejected"))
        return false;
    QByteArray wrongPid = frame;
    wrongPid[15] = 0xcf;
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(wrongPid, &ignored),
               "non-AXCP PID is rejected"))
        return false;
    QByteArray badReservedBits = frame;
    badReservedBits[6] = static_cast<char>(static_cast<unsigned char>(badReservedBits.at(6)) & ~0x20);
    if (!check(!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(badReservedBits, &ignored),
               "invalid reserved address bits are rejected"))
        return false;

    QByteArray withRepeater = frame;
    withRepeater[13] = static_cast<char>(static_cast<unsigned char>(withRepeater.at(13)) & ~0x01);
    withRepeater.insert(14, QByteArray::fromHex("82984040404061"));
    return check(!Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(withRepeater, &ignored),
                 "direct-only AX.25 decoder rejects repeater address paths");
}

bool testAx25NumericCallsignAndSsidRoundTrip()
{
    QByteArray frame;
    if (!check(Ax25Kiss::Ax25UiFrameCodec::encodeUiFrame(
                   QStringLiteral("123456-15"), QStringLiteral("654321-1"),
                   QByteArray("x"), &frame),
               "AX.25 addresses may use a numeric-leading alphanumeric identifier"))
        return false;

    Ax25Kiss::Ax25UiFrame decoded;
    if (!check(Ax25Kiss::Ax25UiFrameCodec::decodeUiFrame(frame, &decoded),
               "numeric-leading AX.25 address decodes"))
        return false;
    return check(decoded.source == QStringLiteral("123456-15") &&
                     decoded.destination == QStringLiteral("654321-1"),
                 "AX.25 address round-trip preserves distinct nonzero SSIDs");
}
}

int main()
{
    return testKissEncodingAndIncrementalDecode() &&
                   testKissMultipleFramesAndFiltering() &&
                   testKissMalformedAndOversizedFrames() &&
                   testAx25UiFrameKnownVectorAndRoundTrip() &&
                   testAx25UiFrameRejectsInvalidAddressesAndFrames() &&
                   testAx25NumericCallsignAndSsidRoundTrip()
               ? 0
               : 1;
}
