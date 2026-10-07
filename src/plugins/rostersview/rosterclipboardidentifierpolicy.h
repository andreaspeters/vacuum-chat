#ifndef ROSTERCLIPBOARDIDENTIFIERPOLICY_H
#define ROSTERCLIPBOARDIDENTIFIERPOLICY_H

#include <interfaces/iprotocolroster.h>

namespace RosterClipboardIdentifierPolicy
{
	enum class Source
	{
		None,
		NativeAccount,
		Conversation
	};

	struct Result
	{
		Source source = Source::None;
		QString label;
		QString value;

		bool isValid() const { return source != Source::None && !value.isEmpty(); }
	};

	inline Result resolve(const ProtocolAccountIdentifier &accountIdentifier, const QString &conversationId)
	{
		if (accountIdentifier.isValid())
			return {Source::NativeAccount, accountIdentifier.label, accountIdentifier.value};

		if (!conversationId.isEmpty())
			return {Source::Conversation, QString(), conversationId};

		return {};
	}
}

#endif // ROSTERCLIPBOARDIDENTIFIERPOLICY_H
