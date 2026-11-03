#ifndef PROTOCOLMESSAGERENDERING_H
#define PROTOCOLMESSAGERENDERING_H

#include <QVariantMap>

namespace ProtocolMessageRendering
{
enum class ImageLoadAction
{
	ShowDecodedImage,
	LoadLocalFile,
	ShowLoadLink
};

inline ImageLoadAction imageLoadAction(bool AImageDecoded, bool AHasLocalFile,
	bool ASchedulerAvailable)
{
	if (AImageDecoded)
		return ImageLoadAction::ShowDecodedImage;
	return AHasLocalFile && ASchedulerAvailable
		? ImageLoadAction::LoadLocalFile : ImageLoadAction::ShowLoadLink;
}

inline QVariantMap metadataAfterLocalImageLoadFailure(const QVariantMap &AMetadata)
{
	QVariantMap fallback = AMetadata;
	fallback.remove(QStringLiteral("file_path"));
	fallback.remove(QStringLiteral("decoded_image"));
	fallback.remove(QStringLiteral("image_resource_url"));
	return fallback;
}

inline bool shouldRetryImageAfterLocalLoadFailure(const QVariantMap &AMetadata,
	bool ASupportsConversationMedia)
{
	return ASupportsConversationMedia &&
		(!AMetadata.value(QStringLiteral("url")).toString().isEmpty() ||
			AMetadata.contains(QStringLiteral("file")));
}

inline bool shouldAppendAfterReplaceAttempt(bool AReplaceRequested,
	bool AReplacementSucceeded, bool AMessageAlreadyRendered)
{
	return !AReplaceRequested || (!AReplacementSucceeded && !AMessageAlreadyRendered);
}

inline bool shouldRebuildAfterReplaceAttempt(bool AReplaceRequested,
	bool AReplacementSucceeded, bool AMessageAlreadyRendered)
{
	return AReplaceRequested && !AReplacementSucceeded && AMessageAlreadyRendered;
}
}

#endif // PROTOCOLMESSAGERENDERING_H
