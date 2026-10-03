#ifndef MULTIUSERCHATCONTEXT_H
#define MULTIUSERCHATCONTEXT_H

class IAccountManager;
class IProtocolAccount;
class Jid;

bool isXmppProtocolAccount(const IProtocolAccount *account);
bool isXmppAccountForStream(const IAccountManager *accountManager, const Jid &streamJid);

#endif // MULTIUSERCHATCONTEXT_H
