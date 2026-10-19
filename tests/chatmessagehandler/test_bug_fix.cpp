#include <QTest>
#include <QVariantMap>
#include <QImage>
#include "protocolmessagehistory.h"

class ProtocolMessageHistoryTest : public QObject
{
    Q_OBJECT

private slots:
    void testMediaHydrationUpdateWithFilePathButNoDecodedImage();
};

void ProtocolMessageHistoryTest::testMediaHydrationUpdateWithFilePathButNoDecodedImage()
{
    // Create an initial message with file_path but no decoded_image (like a historical cache event)
    BasicMessage existingMessage(QStringLiteral("$old-image"), QStringLiteral("!room:test"),
        QStringLiteral("@other:test"), QString(), QStringLiteral("photo.png"),
        QDateTime::fromMSecsSinceEpoch(3000, QTimeZone::utc()),
        QStringLiteral("matrix"), BasicMessage::Incoming);
    existingMessage.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.image")},
        {QStringLiteral("historical"), true},
        {QStringLiteral("file_path"), QStringLiteral("/cache/photo.bin")}});

    // Create an updated message with decoded_image (hydration)
    BasicMessage updatedMessage = existingMessage;
    updatedMessage.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.image")},
        {QStringLiteral("historical"), true},
        {QStringLiteral("file_path"), QStringLiteral("/cache/photo.bin")},
        {QStringLiteral("decoded_image"), QImage()}});

    // This should NOT be detected as a hydration update because:
    // - hasMediaPayload returns true for both due to file_path being non-empty
    // - isMediaHydrationUpdate checks: !hasMediaPayload(AExisting) && hasMediaPayload(AUpdated)
    // - But since existing has file_path, it hasMediaPayload() returns true -> not a hydration update
    QVERIFY(!ProtocolMessageHistory::isMediaHydrationUpdate(existingMessage, updatedMessage));

    // If the new message had no file_path and only a decoded_image, then it would be detected correctly
    BasicMessage updatedMessageWithOnlyDecoded = existingMessage;
    updatedMessageWithOnlyDecoded.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.image")},
        {QStringLiteral("historical"), true},
        {QStringLiteral("decoded_image"), QImage()}});

    QVERIFY(ProtocolMessageHistory::isMediaHydrationUpdate(existingMessage, updatedMessageWithOnlyDecoded));
}

QTEST_APPLESS_MAIN(ProtocolMessageHistoryTest)
#include "protocolmessagehistory_test.moc"