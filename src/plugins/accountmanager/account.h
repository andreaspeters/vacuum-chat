#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <interfaces/iaccountmanager.h>
#include <interfaces/iprotocolaccount.h>
#include <interfaces/ixmppstreams.h>
#include <interfaces/ioptionsmanager.h>
#include <utils/options.h>

class Account :
		public QObject,
		public IAccount
{
	Q_OBJECT
	Q_INTERFACES(IAccount)
public:
	Account(IXmppStreams *AXmppStreams, const OptionsNode &AOptionsNode, QObject *AParent);
	~Account();

	// IAccount implementation
	virtual QObject *instance() { return this; }
	virtual ProtocolKind protocolKind() const;
	virtual Capabilities capabilities() const;
	virtual ConnectionState connectionState() const;
	virtual bool isValid() const;
	virtual QUuid accountId() const;
	virtual bool isActive() const;
	virtual void setActive(bool AActive);
	virtual QString name() const;
	virtual void setName(const QString &AName);
	virtual Jid streamJid() const;
	virtual void setStreamJid(const Jid &AJid);
	virtual QString password() const;
	virtual void setPassword(const QString &APassword);
	virtual OptionsNode optionsNode() const;
	virtual IXmppStream *xmppStream() const;

signals:
	void activeChanged(bool AActive);
	void optionsChanged(const OptionsNode &ANode);

protected slots:
	void onXmppStreamClosed();
	void onOptionsChanged(const OptionsNode &ANode);

private:
	IXmppStream *FXmppStream;
	IXmppStreams *FXmppStreams;
	OptionsNode FOptionsNode;
	bool FActive;
};

#endif // ACCOUNT_H