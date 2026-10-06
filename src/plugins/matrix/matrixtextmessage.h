#ifndef MATRIXTEXTMESSAGE_H
#define MATRIXTEXTMESSAGE_H

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

struct MatrixTextMessagePayload
{
	QJsonObject content;
	QVariantMap localEchoMetadata;
};

MatrixTextMessagePayload createMatrixTextMessagePayload(
	const QString &body, const QVariantMap &metadata);

#endif // MATRIXTEXTMESSAGE_H
