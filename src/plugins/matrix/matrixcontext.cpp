#include "matrixcontext.h"

bool isMatrixAccountContext(const QString &selectedStreamId, const QString &boundStreamId,
	const IProtocolAccount *selectedAccount, const IProtocolAccount *boundAccount)
{
	return !selectedStreamId.isEmpty() && selectedStreamId == boundStreamId &&
		selectedAccount && selectedAccount == boundAccount &&
		selectedAccount->protocolKind() == IProtocolAccount::ProtocolMatrix;
}
