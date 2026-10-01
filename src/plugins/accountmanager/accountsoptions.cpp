#include "accountsoptions.h"

#include <QMessageBox>
#include <QHeaderView>
#include <QTextDocument>

#define COL_NAME                0
#define COL_ACCOUNT             1

AccountsOptions::AccountsOptions(AccountManager *AManager, QWidget *AParent) : QWidget(AParent)
{
	ui.setupUi(this);
	FManager = AManager;

	ui.trwAccounts->setHeaderLabels(QStringList() << tr("Name") << tr("Account/Address"));
	ui.trwAccounts->header()->setSectionResizeMode(COL_NAME, QHeaderView::ResizeToContents);
	ui.trwAccounts->header()->setSectionResizeMode(COL_ACCOUNT, QHeaderView::Stretch);
	ui.trwAccounts->sortByColumn(COL_NAME,Qt::AscendingOrder);
	connect(ui.trwAccounts,SIGNAL(itemChanged(QTreeWidgetItem *, int)),SIGNAL(modified()));

	connect(ui.pbtAdd,SIGNAL(clicked(bool)),SLOT(onAddButtonClicked(bool)));
	connect(ui.pbtRemove,SIGNAL(clicked(bool)),SLOT(onRemoveButtonClicked(bool)));
	connect(ui.trwAccounts,SIGNAL(itemDoubleClicked(QTreeWidgetItem *,int)),SLOT(onItemDoubleClicked(QTreeWidgetItem *,int)));

	connect(FManager->instance(),SIGNAL(changed(IAccount *, const OptionsNode &)),SLOT(onAccountOptionsChanged(IAccount *, const OptionsNode &)));

	reset();
}

AccountsOptions::~AccountsOptions()
{
	foreach(QUuid accountId, FAccountItems.keys())
		if (FManager->accountById(accountId) == NULL)
			removeAccount(accountId);
}

void AccountsOptions::apply()
{
	FPendingAccounts.clear();
	QList<IAccount *> curAccounts;
	for (QMap<QUuid, QTreeWidgetItem *>::const_iterator it = FAccountItems.constBegin(); it!=FAccountItems.constEnd(); ++it)
	{
		IAccount *account = FManager->accountById(it.key());
		if (account)
		{
			account->setActive(it.value()->checkState(COL_NAME) == Qt::Checked);
			it.value()->setCheckState(COL_NAME, account->isActive() ? Qt::Checked : Qt::Unchecked);
			curAccounts.append(account);
		}
		else
		{
			FPendingAccounts.append(it.key());
		}
	}

	foreach(IAccount *account, FManager->accounts())
		if (!curAccounts.contains(account))
			FManager->destroyAccount(account->accountId());

	emit childApply();
}

void AccountsOptions::reset()
{
	QList<QUuid> curAccounts;
	foreach(IAccount *account, FManager->accounts())
	{
		QTreeWidgetItem *item = appendAccount(account->accountId(),account->name());
		item->setCheckState(COL_NAME,account->isActive() ? Qt::Checked : Qt::Unchecked);
		QString addressText = accountIdentifier(account);
		item->setText(COL_ACCOUNT, addressText);
		curAccounts.append(account->accountId());
	}

	foreach(QUuid accountId, FAccountItems.keys())
		if (!curAccounts.contains(accountId))
			removeAccount(accountId);

	emit childReset();
}

QTreeWidgetItem *AccountsOptions::appendAccount(const QUuid &AAccountId, const QString &AName)
{
	QTreeWidgetItem *item = FAccountItems.value(AAccountId,NULL);
	if (item == NULL)
	{
		item = new QTreeWidgetItem(ui.trwAccounts);
		item->setText(COL_NAME,AName);
		item->setCheckState(COL_NAME,Qt::Checked);

		IAccount *account = FManager->accountById(AAccountId);
		if (account)
		{
			QString addressText = accountIdentifier(account);
			item->setText(COL_ACCOUNT,addressText);
		}

		FAccountItems.insert(AAccountId,item);
		FManager->openAccountOptionsNode(AAccountId,AName);
	}
	return item;
}

void AccountsOptions::removeAccount(const QUuid &AAccountId)
{
	FManager->closeAccountOptionsNode(AAccountId);
	delete FAccountItems.take(AAccountId);
}

QString AccountsOptions::accountIdentifier(IAccount *AAccount) const
{
	if (!AAccount)
		return QString();

	// XMPP accounts use streamJid.uFull()
	if (AAccount->protocolKind() != IProtocolAccount::ProtocolMatrix)
		return AAccount->streamJid().uFull();

	// Matrix accounts use username@instance
	const QString matrixInstance = AAccount->optionsNode().value("matrix.instance").toString();
	const QString matrixUsername = AAccount->optionsNode().value("matrix.username").toString();

	if (!matrixUsername.isEmpty() && !matrixInstance.isEmpty())
		return matrixUsername + "@" + matrixInstance;

	return matrixUsername.isEmpty() ? matrixInstance : matrixUsername;
}

void AccountsOptions::onAddButtonClicked(bool)
{
	QUuid accountId = QUuid::createUuid();
	appendAccount(accountId,tr("New Account"));
	FManager->showAccountOptionsDialog(accountId);
	emit modified();
}

void AccountsOptions::onRemoveButtonClicked(bool)
{
	QTreeWidgetItem *item = ui.trwAccounts->currentItem();
	if (item)
	{
		QMessageBox::StandardButton res = QMessageBox::warning(this,
		                                  tr("Confirm removal of an account"),
		                                  tr("You are assured that wish to remove an account <b>%1</b>?<br>All settings will be lost.").arg(item->text(0).toHtmlEscaped()),
		                                  QMessageBox::Ok | QMessageBox::Cancel);

		if (res == QMessageBox::Ok)
		{
			removeAccount(FAccountItems.key(item));
			emit modified();
		}
	}
}

void AccountsOptions::onItemDoubleClicked(QTreeWidgetItem *AItem, int AColumn)
{
	Q_UNUSED(AColumn);
	if (AItem)
	{
		FManager->showAccountOptionsDialog(FAccountItems.key(AItem));
	}
}

void AccountsOptions::onAccountOptionsChanged(IAccount *AAcount, const OptionsNode &ANode)
{
	QTreeWidgetItem *item = FAccountItems.value(AAcount->accountId());
	if (item)
	{
		const QString path = AAcount->optionsNode().childPath(ANode);
		if (path == "name")
			item->setText(COL_NAME,AAcount->name());
		if (path == "name" || path == "type" || path == "streamJid"
				|| path == "matrix.instance" || path == "matrix.username")
			item->setText(COL_ACCOUNT,accountIdentifier(AAcount));

		if (FPendingAccounts.contains(AAcount->accountId()) && AAcount->isValid())
		{
			AAcount->setActive(item->checkState(COL_NAME) == Qt::Checked);
			item->setCheckState(COL_NAME,AAcount->isActive() ? Qt::Checked : Qt::Unchecked);
			FPendingAccounts.removeAll(AAcount->accountId());
		}
	}
}
