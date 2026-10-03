#include "matrixcontext.h"

#include <QCoreApplication>
#include <QDebug>
#include <utils/options.h>

class FakeProtocolAccount : public IProtocolAccount
{
public:
    explicit FakeProtocolAccount(ProtocolKind kind) : FKind(kind) {}
    QObject *instance() override { return nullptr; }
    ProtocolKind protocolKind() const override { return FKind; }
    Capabilities capabilities() const override { return CapabilityNone; }
    ConnectionState connectionState() const override { return StateDisconnected; }
    QUuid accountId() const override { return QUuid(); }
    bool isActive() const override { return false; }
    QString name() const override { return QStringLiteral("fixture"); }
    OptionsNode optionsNode() const override { return OptionsNode(); }
private:
    ProtocolKind FKind;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    FakeProtocolAccount matrix(IProtocolAccount::ProtocolMatrix);
    FakeProtocolAccount xmpp(IProtocolAccount::ProtocolXmpp);
    FakeProtocolAccount meshcore(IProtocolAccount::ProtocolMeshCore);
    const QString streamId = QStringLiteral("alice@matrix.example");
    const QString otherStreamId = QStringLiteral("bob@matrix.example");
    if (!isMatrixAccountContext(streamId, streamId, &matrix, &matrix)) {
        qCritical() << "bound Matrix account root was rejected";
        return 1;
    }
    if (isMatrixAccountContext(otherStreamId, streamId, &matrix, &matrix) ||
        isMatrixAccountContext(streamId, streamId, &xmpp, &xmpp) ||
        isMatrixAccountContext(streamId, streamId, &meshcore, &meshcore) ||
        isMatrixAccountContext(streamId, streamId, &matrix, nullptr)) {
        qCritical() << "wrong stream, protocol, or unbound account was accepted";
        return 1;
    }
    qInfo() << "Matrix context protocol gate test passed";
    return 0;
}
