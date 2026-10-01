#ifndef ACCOUNTOPTIONS_H
#define ACCOUNTOPTIONS_H

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>
#include <definitions/version.h>
#include <definitions/optionvalues.h>
#include <interfaces/iaccountmanager.h>
#include <interfaces/ioptionsmanager.h>
#include <interfaces/ipluginmanager.h>
#include <utils/jid.h>
#include <utils/options.h>
#include <ui_accountoptions.h>

class AccountOptions :
			public QWidget,
			public IOptionsWidget
{
	Q_OBJECT;
	Q_INTERFACES(IOptionsWidget);
public:
	AccountOptions(IAccountManager *AManager, IPluginManager *APluginManager, const QUuid &AAccountId, QWidget *AParent);
	~AccountOptions();
	virtual QWidget* instance() { return this; }
public slots:
	virtual void apply();
	virtual void reset();
signals:
	void modified();
	void childApply();
	void childReset();

	void matrixVerificationRequested(const QUuid &accountId, const QString &userId,
		const QString &deviceId);
	void matrixSsssRecoveryRequested(const QUuid &accountId);
	void matrixRoomKeyImportRequested(const QUuid &accountId);
private:
	Ui::AccountOptionsClass ui;
private:
	IAccountManager *FManager;
	IPluginManager *FPluginManager;
private:
	QUuid FAccountId;
	IAccount *FAccount;
	QComboBox *FAccountType;
	QWidget *FMatrixFields;
	QLineEdit *FMatrixInstance;
	QLineEdit *FMatrixUsername;
	QLineEdit *FMatrixPassword;
	QLineEdit *FMatrixDeviceId;
	QComboBox *FMatrixEmojiPack;

	QPushButton *FMatrixSsssButton;
	QPushButton *FMatrixRoomKeyImportButton;
	QPushButton *FMatrixNewDeviceButton;
	QLineEdit *FMatrixTargetUser;
	QLineEdit *FMatrixTargetDevice;
	QPushButton *FMatrixVerifyButton;
};

#endif // ACCOUNTOPTIONS_H
