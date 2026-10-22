#include "matrixtextmessage.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace
{
bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVariantMap metadata;
    const QJsonObject edit = createMatrixEditMessageContent(
        QStringLiteral("Updated text"), metadata, QStringLiteral("$original-event"));
    const QJsonObject replacement = edit.value(QStringLiteral("m.new_content")).toObject();
    const QJsonObject relation = edit.value(QStringLiteral("m.relates_to")).toObject();

    bool ok = true;
    ok &= expect(edit.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.text"),
        "outer msgtype is m.text");
    ok &= expect(edit.value(QStringLiteral("body")).toString() == QStringLiteral("* Updated text"),
        "outer body carries Matrix replacement fallback");
    ok &= expect(edit.value(QStringLiteral("format")).toString() == QStringLiteral("org.matrix.custom.html"),
        "outer formatted content declares custom HTML");
    ok &= expect(edit.value(QStringLiteral("formatted_body")).toString().startsWith(QStringLiteral("* ")),
        "outer formatted body carries replacement fallback");
    ok &= expect(edit.value(QStringLiteral("m.mentions")).isObject(),
        "outer content includes m.mentions");
    ok &= expect(replacement.value(QStringLiteral("body")).toString() == QStringLiteral("Updated text"),
        "m.new_content keeps the actual edited body");
    ok &= expect(replacement.value(QStringLiteral("format")).toString() == QStringLiteral("org.matrix.custom.html"),
        "m.new_content declares custom HTML");
    ok &= expect(!replacement.value(QStringLiteral("formatted_body")).toString().startsWith(QStringLiteral("* ")),
        "m.new_content formatted body excludes replacement fallback");
    ok &= expect(replacement.value(QStringLiteral("m.mentions")).isObject(),
        "m.new_content includes m.mentions");
    ok &= expect(relation.value(QStringLiteral("rel_type")).toString() == QStringLiteral("m.replace"),
        "relation type identifies an edit");
    ok &= expect(relation.value(QStringLiteral("event_id")).toString() == QStringLiteral("$original-event"),
        "edit targets the original event");

    const MatrixEncryptedRoomEventPayload encrypted =
        splitMatrixEncryptedRoomEventContent(QStringLiteral("!room:example.org"),
            QStringLiteral("m.room.message"), edit);
    const QJsonObject encryptedContent = encrypted.plaintextEvent.value(
        QStringLiteral("content")).toObject();
    ok &= expect(encryptedContent.value(QStringLiteral("m.new_content")).isObject(),
        "encrypted payload retains replacement content");
    ok &= expect(!encryptedContent.contains(QStringLiteral("m.relates_to")),
        "encrypted plaintext does not contain the clear relation");
    ok &= expect(encrypted.outerContent.value(QStringLiteral("m.relates_to")).toObject() == relation,
        "encrypted wire event keeps the relation outside ciphertext");
    ok &= expect(matrixRelationForEvent(encrypted.outerContent, encryptedContent) == relation,
        "encrypted event relation is read from the outer event");
    ok &= expect(matrixRelationForEvent(QJsonObject(), edit) == relation,
        "unencrypted event relation remains readable from message content");

    ok &= expect(createMatrixEditMessageContent(QString(), metadata,
        QStringLiteral("$original-event")).isEmpty(), "empty edits are rejected");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
