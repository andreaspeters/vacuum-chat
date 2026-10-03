#ifndef MATRIXCONTEXT_H
#define MATRIXCONTEXT_H

#include <interfaces/iprotocolaccount.h>
#include <QString>

bool isMatrixAccountContext(const QString &selectedStreamId, const QString &boundStreamId,
	const IProtocolAccount *selectedAccount, const IProtocolAccount *boundAccount);

#endif // MATRIXCONTEXT_H
