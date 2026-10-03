#include "multiuserchatcontext.h"

#include <interfaces/iprotocolaccount.h>
#include <interfaces/iaccountmanager.h>

bool isXmppProtocolAccount(const IProtocolAccount *account)
{
	return account && account->protocolKind() == IProtocolAccount::ProtocolXmpp;
}

bool isXmppAccountForStream(const IAccountManager *accountManager, const Jid &streamJid)
{
	return accountManager && isXmppProtocolAccount(accountManager->accountByStream(streamJid));
}
