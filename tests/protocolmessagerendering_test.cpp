#include "protocolmessagerendering.h"

#include <QVariantMap>

#include <iostream>

namespace
{
bool check(bool condition, const char *description)
{
	if (!condition)
		std::cerr << description << " failed\n";
	return condition;
}
}

bool testLocalImageLoadAction()
{
	using ProtocolMessageRendering::ImageLoadAction;
	bool passed = true;
	passed &= check(ProtocolMessageRendering::imageLoadAction(false, true, true) ==
		ImageLoadAction::LoadLocalFile, "a local file is decoded when the scheduler is available");
	passed &= check(ProtocolMessageRendering::imageLoadAction(false, true, false) ==
		ImageLoadAction::ShowLoadLink, "a local file gets a retry link when the scheduler is unavailable");
	passed &= check(ProtocolMessageRendering::imageLoadAction(false, false, true) ==
		ImageLoadAction::ShowLoadLink, "a missing local file gets a retry link");
	passed &= check(ProtocolMessageRendering::imageLoadAction(true, true, false) ==
		ImageLoadAction::ShowDecodedImage, "a decoded image is displayed regardless of scheduler state");
	return passed;
}

bool testFailedLocalImageMetadata()
{
	QVariantMap metadata;
	metadata.insert(QStringLiteral("file_path"), QStringLiteral("/cache/corrupt-image"));
	metadata.insert(QStringLiteral("decoded_image"), QStringLiteral("stale-image"));
	metadata.insert(QStringLiteral("image_resource_url"), QStringLiteral("vacuum-image:/stale"));
	metadata.insert(QStringLiteral("url"), QStringLiteral("mxc://example.invalid/media"));
	metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.image"));
	metadata.insert(QStringLiteral("historical"), true);
	const QVariantMap fallback = ProtocolMessageRendering::metadataAfterLocalImageLoadFailure(metadata);
	return check(!fallback.contains(QStringLiteral("file_path")) &&
		!fallback.contains(QStringLiteral("decoded_image")) &&
		!fallback.contains(QStringLiteral("image_resource_url")),
		"failed local image metadata drops only stale local render state") &&
		check(fallback.value(QStringLiteral("url")).toString() ==
			QStringLiteral("mxc://example.invalid/media") &&
			fallback.value(QStringLiteral("msgtype")).toString() == QStringLiteral("m.image") &&
			fallback.value(QStringLiteral("historical")).toBool(),
			"failed local image metadata preserves the remote retry identity");
}

bool testFailedLocalImageRetry()
{
	QVariantMap metadata;
	metadata.insert(QStringLiteral("url"), QStringLiteral("mxc://example.invalid/media"));
	metadata.insert(QStringLiteral("msgtype"), QStringLiteral("m.image"));
	return check(ProtocolMessageRendering::shouldRetryImageAfterLocalLoadFailure(metadata, true),
		"a failed local decode retries through a capable protocol adapter") &&
		check(!ProtocolMessageRendering::shouldRetryImageAfterLocalLoadFailure(metadata, false),
			"a failed local decode does not call an unsupported media adapter") &&
			check(!ProtocolMessageRendering::shouldRetryImageAfterLocalLoadFailure(QVariantMap(), true),
				"a failed local decode without remote media identity is not retried");
}

bool testRebuildAfterReplaceAttempt()
{
	return check(!ProtocolMessageRendering::shouldRebuildAfterReplaceAttempt(false, false, false), "no rebuild when no replace requested") &&
		check(!ProtocolMessageRendering::shouldRebuildAfterReplaceAttempt(true, true, true), "no rebuild when replacement succeeds") &&
		check(!ProtocolMessageRendering::shouldRebuildAfterReplaceAttempt(true, false, false), "no rebuild when message not rendered yet") &&
		check(ProtocolMessageRendering::shouldRebuildAfterReplaceAttempt(true, false, true), "rebuild when replace requested, failed, and already rendered");
}

int main()
{
	bool passed = true;
	passed &= check(ProtocolMessageRendering::shouldAppendAfterReplaceAttempt(
		false, false, false), "a new message is appended");
	passed &= check(!ProtocolMessageRendering::shouldAppendAfterReplaceAttempt(
		true, true, true), "a successful replacement is not appended again");
	passed &= check(ProtocolMessageRendering::shouldAppendAfterReplaceAttempt(
		true, false, false), "an unseen event may append if replacement misses");
	passed &= check(!ProtocolMessageRendering::shouldAppendAfterReplaceAttempt(
		true, false, true), "a previously rendered event is not appended after replacement misses");
	
	passed &= testLocalImageLoadAction();
	passed &= testFailedLocalImageMetadata();
	passed &= testFailedLocalImageRetry();
	passed &= testRebuildAfterReplaceAttempt();
	return passed ? 0 : 1;
}
