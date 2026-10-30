#ifndef MATRIXEVENTCONTENTMETADATA_H
#define MATRIXEVENTCONTENTMETADATA_H

#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

namespace MatrixEventContentMetadata
{
inline void copyMediaFields(const QJsonObject &content, QVariantMap &metadata)
{
	static const QStringList mediaKeys = {
		QStringLiteral("url"), QStringLiteral("file"), QStringLiteral("info"),
		QStringLiteral("thumbnail_url"), QStringLiteral("thumbnail_file"),
		QStringLiteral("thumbnail_info"), QStringLiteral("filename")};
	for (const QString &key : mediaKeys)
		if (content.contains(key))
			metadata.insert(key, content.value(key).toVariant());
}
}

#endif // MATRIXEVENTCONTENTMETADATA_H
