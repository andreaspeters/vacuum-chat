#include "matrix.h"
#include "matrixavatarkeypolicy.h"
#include "matrixdatabase.h"
#include "matrixnetwork.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>
#include <QTimer>

#include <interfaces/iaccountmanager.h>

#include <algorithm>
#include <iostream>

namespace {

void writeEmptyJsonResponse(QTcpSocket *socket)
{
	socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: 2\r\n"
		"Connection: close\r\n\r\n{}"));
	socket->disconnectFromHost();
}

}

class MatrixNetworkTestAccess
{
public:
	static void seedPendingEvent(MatrixNetwork &network, const MatrixTextEvent &event)
	{
		network.FMessageHistory[event.roomId].append(event);
	}
	static bool replacePendingEvent(MatrixNetwork &network, const QString &roomId,
		const QString &transactionId, const MatrixTextEvent &serverEvent)
	{
		return network.replacePendingEvent(roomId, transactionId, serverEvent);
	}
	static void configureLocalMessageSend(MatrixNetwork &network, const QString &roomId)
	{
		network.FAccesToken = QStringLiteral("synthetic-test-token");
		network.FNormalizedServerUrl = QStringLiteral("http://127.0.0.1:1");
		network.FRooms[roomId].isJoined = true;
		network.FRooms[roomId].encryptionStateKnown = true;
	}
	static void configureReadMarkerRequest(MatrixNetwork &network, quint16 port)
	{
		network.FAccesToken = QStringLiteral("synthetic-test-token");
		network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
	}
	static void configureMemberActionRequest(MatrixNetwork &network, quint16 port,
		const QString &roomId, const QString &ownUserId)
	{
		network.FAccesToken = QStringLiteral("synthetic-moderation-token");
		network.setServerUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
		network.FUserId = ownUserId;
		ProtocolRoom &room = network.FRooms[roomId];
		room.id = roomId;
		room.membership = QStringLiteral("join");
		room.isJoined = true;
		room.isAvailable = true;
	}
	static void clearMemberActionRequest(MatrixNetwork &network, const QString &roomId)
	{
		network.FAccesToken.clear();
		network.FUserId.clear();
		network.FRooms.remove(roomId);
	}
	static bool hasJoinedRoomMember(const MatrixNetwork &network, const QString &roomId,
		const QString &memberId)
	{
		const QList<ProtocolRosterEntry> members = network.FRooms.value(roomId).members;
		return std::any_of(members.cbegin(), members.cend(), [&](const ProtocolRosterEntry &member) {
			return member.id == memberId && member.isValid;
		});
	}
	static MatrixTextEvent pendingMessage(const MatrixNetwork &network, const QString &roomId)
	{
		const QList<MatrixTextEvent> events = network.FMessageHistory.value(roomId);
		return events.isEmpty() ? MatrixTextEvent() : events.last();
	}
};

class MatrixSyncBatchTestAccess
{
public:
	static void setHistoryLoading(Matrix &matrix, const QString &roomId)
	{
		matrix.FHistoryLoadingRooms.insert(roomId);
	}
	static void queuePendingMessage(Matrix &matrix, const BasicMessage &message)
	{
		matrix.queueOrEmitHistoryMessage(message);
	}
	static void loadCachedHistory(Matrix &matrix, const QString &roomId,
		const QList<MatrixTimelineEvent> &events)
	{
		matrix.onCachedHistoryLoaded(roomId, events);
	}
	static void processCachedHistoryBatch(Matrix &matrix)
	{
		matrix.emitCachedHistoryBatch();
	}
	static void setConfiguredUserId(Matrix &matrix, const QString &userId)
	{
		matrix.FDatabaseUserId = userId;
		matrix.FNetworkUserId.clear();
	}
	static void setNetworkUserId(Matrix &matrix, const QString &userId)
	{
		matrix.FNetworkUserId = userId;
	}
	static void setProtocolRooms(Matrix &matrix, const QList<ProtocolRoom> &rooms)
	{
		matrix.FProtocolRooms = rooms;
	}
	static QString latestConversationEventId(const Matrix &matrix, const QString &roomId)
	{
		return matrix.FLatestConversationEventIds.value(roomId);
	}
	static bool networkLoggedIn(const Matrix &matrix)
	{
		return matrix.FNetworkLoggedIn && matrix.FMatrixNetwork;
	}
	static void configureMessageSending(Matrix &matrix, MatrixNetwork *network, const QString &userId)
	{
		matrix.FMatrixNetwork = network;
		matrix.FNetworkLoggedIn = true;
		matrix.FNetworkUserId = userId;
	}
	static void setActiveConversation(Matrix &matrix, const QString &roomId)
	{
		matrix.FActiveConversationId = roomId;
	}
	static void setLatestConversationEventId(Matrix &matrix, const QString &roomId,
		const QString &eventId)
	{
		matrix.FLatestConversationEventIds.insert(roomId, eventId);
	}
	static void deliverPendingMessage(Matrix &matrix, const BasicMessage &message)
	{
		matrix.onNetworkMessageReceived(message);
	}
	static void dispatchHistorySnapshot(Matrix &matrix, const QString &roomId,
		const QList<MatrixTextEvent> &events)
	{
		matrix.onMessageHistoryChanged(roomId, events);
	}
	static void dispatchLiveSyncEvents(Matrix &matrix, const QList<MatrixTextEvent> &events)
	{
		matrix.onSyncReceived(events);
	}
	static void loadHistoryPage(Matrix &matrix, const QString &roomId,
		const MatrixHistoryPageResult &result, QObject *callbackContext,
		ProtocolHistoryPageCallback callback)
	{
		Matrix::PendingHistoryPageRequest request;
		request.callbackContext = callbackContext;
		request.callback = callback;
		matrix.FPendingHistoryPageRequests.insert(roomId, request);
		matrix.onHistoryPageLoaded(roomId, result);
	}
	static void connectMemberActionRefreshSignals(Matrix &matrix, MatrixNetwork &network)
	{
		QObject::connect(&network, &MatrixNetwork::roomPowerLevelsChanged, &matrix,
			&Matrix::onRoomPowerLevelsChanged, Qt::UniqueConnection);
		QObject::connect(&network, &MatrixNetwork::roomMetadataChanged, &matrix,
			&Matrix::onRoomMetadataChanged, Qt::UniqueConnection);
		QObject::connect(&network, &MatrixNetwork::rosterChanged, &matrix,
			&Matrix::onRosterChanged, Qt::UniqueConnection);
	}
	static void configureMemberActionRefresh(Matrix &matrix, MatrixNetwork &network,
		IAccount *account, quint16 port, const QString &roomId, const QString &ownUserId,
		const QString &memberId, const QJsonObject &cachedPowerLevels)
	{
		matrix.FMatrixNetwork = &network;
		matrix.FMatrixAccount = account;
		matrix.FNetworkLoggedIn = true;
		matrix.FNetworkInitialSyncComplete = true;
		matrix.FNetworkUserId = ownUserId;
		ProtocolRoom room;
		room.id = roomId;
		room.isJoined = true;
		room.isAvailable = true;
		ProtocolRosterEntry member;
		member.id = memberId;
		member.isValid = true;
		room.members.append(member);
		matrix.FProtocolRooms = {room};
		matrix.FRoomPowerLevels.insert(roomId, cachedPowerLevels);
		MatrixNetworkTestAccess::configureMemberActionRequest(network, port, roomId, ownUserId);
		connectMemberActionRefreshSignals(matrix, network);
	}
	static void configureMemberActionLoading(Matrix &matrix, MatrixNetwork &network,
		IAccount *account, quint16 port, const QString &roomId, const QString &ownUserId)
	{
		matrix.FMatrixNetwork = &network;
		matrix.FMatrixAccount = account;
		matrix.FNetworkLoggedIn = true;
		matrix.FNetworkInitialSyncComplete = true;
		matrix.FNetworkUserId = ownUserId;
		ProtocolRoom room;
		room.id = roomId;
		room.isJoined = true;
		room.isAvailable = true;
		matrix.FProtocolRooms = {room};
		MatrixNetworkTestAccess::configureMemberActionRequest(network, port, roomId, ownUserId);
		connectMemberActionRefreshSignals(matrix, network);
	}
	static void setMemberActionRoster(Matrix &matrix, const QList<ProtocolRoom> &rooms)
	{
		matrix.onRosterChanged(rooms);
	}
	static bool hasMemberActionRoomMember(const Matrix &matrix, const QString &roomId,
		const QString &memberId)
	{
		return std::any_of(matrix.FProtocolRooms.cbegin(), matrix.FProtocolRooms.cend(),
			[&](const ProtocolRoom &room) {
				return room.id == roomId && std::any_of(room.members.cbegin(), room.members.cend(),
					[&](const ProtocolRosterEntry &member) {
						return member.id == memberId && member.isValid;
					});
			});
	}
	static bool hasMemberActionPowerLevels(const Matrix &matrix, const QString &roomId)
	{
		return matrix.FRoomPowerLevels.contains(roomId);
	}
	static void clearMemberActionRefresh(Matrix &matrix, MatrixNetwork &network,
		const QString &roomId)
	{
		matrix.FMatrixNetwork = nullptr;
		matrix.FMatrixAccount = nullptr;
		matrix.FNetworkLoggedIn = false;
		matrix.FNetworkInitialSyncComplete = false;
		matrix.FNetworkUserId.clear();
		matrix.FProtocolRooms.clear();
		matrix.FRoomPowerLevels.remove(roomId);
		matrix.FPendingMemberActionObservers.remove(roomId);
		MatrixNetworkTestAccess::clearMemberActionRequest(network, roomId);
	}
};

class SyntheticMatrixAccount final : public IAccount
{
public:
	explicit SyntheticMatrixAccount(const QUuid &id) : FId(id) {}
	QObject *instance() override { return nullptr; }
	ProtocolKind protocolKind() const override { return ProtocolMatrix; }
	Capabilities capabilities() const override { return Capabilities(CapabilityChat); }
	ConnectionState connectionState() const override { return StateConnected; }
	QUuid accountId() const override { return FId; }
	bool isActive() const override { return true; }
	QString name() const override { return QStringLiteral("Synthetic Matrix account"); }
	OptionsNode optionsNode() const override { return OptionsNode(); }
	bool isValid() const override { return true; }
	void setActive(bool) override {}
	void setName(const QString &) override {}
	Jid streamJid() const override { return Jid(); }
	void setStreamJid(const Jid &) override {}
	QString password() const override { return QString(); }
	void setPassword(const QString &) override {}
	IXmppStream *xmppStream() const override { return nullptr; }
protected:
	void activeChanged(bool) override {}
	void optionsChanged(const OptionsNode &) override {}
private:
	QUuid FId;
};

namespace
{
bool check(bool condition, const char *description)
{
    if (!condition)
        std::cerr << "FAIL: " << description << '\n';
    return condition;
}

struct AsyncMemberActionResponse
{
	QString memberId;
	QJsonObject powerLevels;
	bool powerLevelsRequestValid = false;
	bool joinedMembersRequestValid = false;

	void send(QTcpSocket *socket, const QByteArray &requestLine, bool authorized)
	{
		QByteArray body;
		const bool joinedMembers = requestLine.contains("/joined_members");
		if (joinedMembers) {
			joinedMembersRequestValid = authorized && requestLine.startsWith("GET ");
			body = QJsonDocument(QJsonObject{{QStringLiteral("joined"), QJsonObject{{memberId,
				QJsonObject{{QStringLiteral("display_name"), QStringLiteral("Member")}}}}}})
				.toJson(QJsonDocument::Compact);
		} else if (requestLine.contains("/state/m.room.power_levels/")) {
			powerLevelsRequestValid = authorized && requestLine.startsWith("GET ");
			body = QJsonDocument(powerLevels).toJson(QJsonDocument::Compact);
		} else {
			body = QByteArrayLiteral("{}");
		}
		const auto sendResponse = [socket, body]() {
			QByteArray response = QByteArrayLiteral(
				"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
			response += QByteArray::number(body.size());
			response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
			socket->write(response);
			socket->disconnectFromHost();
		};
		if (joinedMembers)
			QTimer::singleShot(75, socket, sendResponse);
		else
			sendResponse();
	}
};

bool testCachedMemberPowerLevelsRefresh(Matrix &matrix, MatrixNetwork &network)
{
	const QString roomId = QStringLiteral("!moderation-refresh:example.org");
	const QString ownUserId = QStringLiteral("@admin:example.org");
	const QString memberId = QStringLiteral("@member:example.org");
	SyntheticMatrixAccount account(QUuid(QStringLiteral("{d97a9ba4-c1e7-4f3b-8944-57ecf9bc2d0a}")));
	QTcpServer server;
	if (!server.listen(QHostAddress::LocalHost, 0))
		return check(false, "moderation-refresh test server starts");

	QJsonObject stalePowerLevels;
	stalePowerLevels.insert(QStringLiteral("users_default"), 0);
	stalePowerLevels.insert(QStringLiteral("kick"), 50);
	stalePowerLevels.insert(QStringLiteral("ban"), 50);
	QJsonObject currentPowerLevels = stalePowerLevels;
	currentPowerLevels.insert(QStringLiteral("users"), QJsonObject{{ownUserId, 100}});
	MatrixSyncBatchTestAccess::configureMemberActionRefresh(matrix, network, &account,
		server.serverPort(), roomId, ownUserId, memberId, stalePowerLevels);

	bool requestMethodValid = false;
	bool requestPathValid = false;
	bool requestAuthorizationValid = false;
	QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
		QTcpSocket *socket = server.nextPendingConnection();
		auto respond = [&, socket]() {
			QByteArray request = socket->property("requestBytes").toByteArray();
			request += socket->readAll();
			socket->setProperty("requestBytes", request);
			if (!request.contains(QByteArrayLiteral("\r\n\r\n")))
				return;
			const QByteArray requestLine = request.left(request.indexOf('\r'));
			QJsonObject responseObject;
			QByteArray statusLine = QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
			if (requestLine.contains("/state/m.room.power_levels/")) {
				requestMethodValid = requestLine.startsWith("GET ");
				requestPathValid = true;
				requestAuthorizationValid = request.contains(
					"Authorization: Bearer synthetic-moderation-token");
				responseObject = currentPowerLevels;
			} else if (requestLine.contains("/joined_members")) {
				QJsonObject joined;
				joined.insert(ownUserId, QJsonObject());
				joined.insert(memberId, QJsonObject());
				responseObject.insert(QStringLiteral("joined"), joined);
			} else {
				statusLine = QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\n");
				responseObject.insert(QStringLiteral("errcode"), QStringLiteral("M_UNRECOGNIZED"));
			}
			const QByteArray body = QJsonDocument(responseObject).toJson(QJsonDocument::Compact);
			QByteArray response = statusLine + QByteArrayLiteral(
				"Content-Type: application/json\r\nContent-Length: ");
			response += QByteArray::number(body.size());
			response += QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
			socket->write(response);
			socket->disconnectFromHost();
		};
		QObject::connect(socket, &QTcpSocket::readyRead, socket, respond);
		if (socket->bytesAvailable() > 0)
			respond();
	});

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject callbackContext;
	int callbackCount = 0;
	IProtocolRoomMemberActions::MemberActions refreshedActions;
	matrix.observeActionsForMember(account.accountId().toString(), roomId, memberId,
		&callbackContext, [&](IProtocolRoomMemberActions::MemberActions actions) {
			++callbackCount;
			if (actions.testFlag(IProtocolRoomMemberActions::ActionKick) &&
				actions.testFlag(IProtocolRoomMemberActions::ActionBan)) {
				refreshedActions = actions;
				loop.quit();
			}
		});
	timeout.start(2000);
	loop.exec();
	MatrixSyncBatchTestAccess::clearMemberActionRefresh(matrix, network, roomId);
	const bool validMethod = check(requestMethodValid,
		"profile refresh sends an HTTP GET request");
	const bool validPath = check(requestPathValid,
		"profile refresh requests the room power-level state");
	const bool validAuthorization = check(requestAuthorizationValid,
		"profile refresh sends the Matrix authorization header");
	const bool observerUpdated = check(callbackCount >= 2,
		"profile refresh notifies the member-action observer after the initial result");
	const bool kickAvailable = check(refreshedActions.testFlag(IProtocolRoomMemberActions::ActionKick),
		"refreshed profile actions include Kick");
	const bool banAvailable = check(refreshedActions.testFlag(IProtocolRoomMemberActions::ActionBan),
		"refreshed profile actions include Ban");
	return validMethod && validPath && validAuthorization && observerUpdated &&
		kickAvailable && banAvailable;
}

bool testMemberActionsWaitForAsyncRoomData(Matrix &matrix, MatrixNetwork &network)
{
	const QString roomId = QStringLiteral("!moderation-async-members:example.org");
	const QString ownUserId = QStringLiteral("@admin:example.org");
	const QString memberId = QStringLiteral("@member:example.org");
	SyntheticMatrixAccount account(QUuid(QStringLiteral("{b38c96bb-3987-4e92-9ae5-5351a6d41451}")));
	QTcpServer server;
	if (!server.listen(QHostAddress::LocalHost, 0))
		return check(false, "async member-action test server starts");
	MatrixSyncBatchTestAccess::configureMemberActionLoading(matrix, network, &account,
		server.serverPort(), roomId, ownUserId);

	QJsonObject powerLevels;
	powerLevels.insert(QStringLiteral("users_default"), 0);
	powerLevels.insert(QStringLiteral("users"), QJsonObject{{ownUserId, 100}});
	powerLevels.insert(QStringLiteral("kick"), 50);
	powerLevels.insert(QStringLiteral("ban"), 50);
	AsyncMemberActionResponse responseState{memberId, powerLevels};
	int requestCount = 0;
	QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
		QTcpSocket *socket = server.nextPendingConnection();
		auto respond = [&, socket]() {
			QByteArray request = socket->property("requestBytes").toByteArray();
			request += socket->readAll();
			socket->setProperty("requestBytes", request);
			if (!request.contains(QByteArrayLiteral("\r\n\r\n")))
				return;
			++requestCount;
			const QByteArray requestLine = request.left(request.indexOf('\r'));
			const bool authorized = request.contains("Authorization: Bearer synthetic-moderation-token");
			responseState.send(socket, requestLine, authorized);
		};
		QObject::connect(socket, &QTcpSocket::readyRead, socket, respond);
		if (socket->bytesAvailable() > 0)
			respond();
	});

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject callbackContext;
	QList<IProtocolRoomMemberActions::MemberActions> snapshots;
	matrix.observeActionsForMember(account.accountId().toString(), roomId, memberId,
		&callbackContext, [&](IProtocolRoomMemberActions::MemberActions actions) {
			snapshots.append(actions);
			if (actions.testFlag(IProtocolRoomMemberActions::ActionKick) &&
				actions.testFlag(IProtocolRoomMemberActions::ActionBan))
				loop.quit();
		});
	timeout.start(2000);
	loop.exec();
	const bool reachedPermissions = !snapshots.isEmpty() &&
		snapshots.constLast().testFlag(IProtocolRoomMemberActions::ActionKick) &&
		snapshots.constLast().testFlag(IProtocolRoomMemberActions::ActionBan);
	const auto currentActions = matrix.actionsForMember(account.accountId().toString(), roomId, memberId);
	const bool currentPermissions = currentActions.testFlag(IProtocolRoomMemberActions::ActionKick) &&
		currentActions.testFlag(IProtocolRoomMemberActions::ActionBan);
	const bool memberLoaded = MatrixSyncBatchTestAccess::hasMemberActionRoomMember(matrix, roomId, memberId);
	const bool networkMemberLoaded = MatrixNetworkTestAccess::hasJoinedRoomMember(network, roomId, memberId);
	const bool powerLevelsLoaded = MatrixSyncBatchTestAccess::hasMemberActionPowerLevels(matrix, roomId);
	MatrixSyncBatchTestAccess::clearMemberActionRefresh(matrix, network, roomId);
	bool passed = check(requestCount == 2,
		"profile requests both member-list and power-level state exactly once");
	passed &= check(responseState.joinedMembersRequestValid &&
		responseState.powerLevelsRequestValid,
		"profile refresh uses the joined-members and power-level endpoints");
	passed &= check(memberLoaded,
		"joined-members response updates the Matrix adapter roster");
	passed &= check(networkMemberLoaded,
		"joined-members response updates the Matrix network room state");
	passed &= check(powerLevelsLoaded,
		"power-level response updates the Matrix adapter cache");
	passed &= check(currentPermissions,
		"current room snapshot and power levels permit Kick and Ban");
	passed &= check(snapshots.size() >= 2,
		"open profile receives updates after asynchronous room-state changes");
	passed &= check(reachedPermissions,
		"profile actions update after member list and power levels arrive asynchronously");
	return passed;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Matrix matrix;
    MatrixNetwork network;

    if (!testCachedMemberPowerLevelsRefresh(matrix, network))
        return 1;
    if (!testMemberActionsWaitForAsyncRoomData(matrix, network))
        return 1;

    if (!check(matrix.formatEmoticonsForSending(QStringLiteral("hello :-)")) ==
            QStringLiteral("hello 🙂"),
            "classic ASCII smileys convert to Unicode on Matrix send"))
        return 1;
    if (!check(matrix.formatEmoticonsForSending(QStringLiteral("(: :-) :) :-( :( :-D :D ;-) ;) :-P :P :-O :O")) ==
            QStringLiteral("🙃 🙂 🙂 🙁 🙁 😄 😄 😉 😉 😛 😛 😮 😮"),
            "all ASCII emoticon aliases retain their Unicode conversions"))
        return 1;

    // A message arriving while its conversation is already active must advance
    // the outgoing Matrix read receipt without requiring another activation.
    QTcpServer receiptServer;
    if (!receiptServer.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "FAIL: local read-marker test server starts\n";
        return 1;
    }
    MatrixNetworkTestAccess::configureReadMarkerRequest(network, receiptServer.serverPort());
    const QString receiptRoomId = QStringLiteral("!active-read:example.org");
    const QString receiptEventId = QStringLiteral("$arrived-while-active");
    const QString selfUserIdForReceipt = QStringLiteral("@self:example.org");
    MatrixSyncBatchTestAccess::configureMessageSending(matrix, &network, selfUserIdForReceipt);
    MatrixSyncBatchTestAccess::setActiveConversation(matrix, receiptRoomId);
    BasicMessage activeRoomMessage(receiptEventId, receiptRoomId,
        QStringLiteral("@other:example.org"), QString(), QStringLiteral("synthetic"),
        QDateTime::currentDateTimeUtc(), QStringLiteral("matrix"), BasicMessage::Incoming);
    QVariantMap activeMessageMetadata;
    activeMessageMetadata.insert(QStringLiteral("event_type"), QStringLiteral("m.room.message"));
    activeRoomMessage.setMetadata(activeMessageMetadata);
    MatrixSyncBatchTestAccess::deliverPendingMessage(matrix, activeRoomMessage);

    QByteArray receiptRequest;
    QEventLoop receiptLoop;
    QObject::connect(&receiptServer, &QTcpServer::newConnection, &receiptLoop, [&]() {
        QTcpSocket *socket = receiptServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &receiptLoop, [&, socket]() {
            receiptRequest += socket->readAll();
            writeEmptyJsonResponse(socket);
            receiptLoop.quit();
        });
    });
    QTimer::singleShot(1000, &receiptLoop, &QEventLoop::quit);
    receiptLoop.exec();
    if (!check(receiptRequest.contains(
                "POST /_matrix/client/v3/rooms/!active-read:example.org/read_markers HTTP/1.1") &&
            receiptRequest.contains("\"m.read\":\"$arrived-while-active\"") &&
            receiptRequest.contains("\"m.fully_read\":\"$arrived-while-active\""),
            "an incoming event in the active conversation sends m.read to the Matrix "
            "Client-Server API endpoint"))
        return 1;

    const QString openedRoomEventId = QStringLiteral("$latest-before-open");
    QTcpServer openedRoomServer;
    if (!openedRoomServer.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "FAIL: local room-open read-marker test server starts\n";
        return 1;
    }
    MatrixNetworkTestAccess::configureReadMarkerRequest(network, openedRoomServer.serverPort());
    MatrixSyncBatchTestAccess::setLatestConversationEventId(matrix, receiptRoomId, openedRoomEventId);
    if (!check(MatrixSyncBatchTestAccess::latestConversationEventId(matrix, receiptRoomId) == openedRoomEventId,
            "the opened room has a latest event available to mark read"))
        return 1;
    if (!check(MatrixSyncBatchTestAccess::networkLoggedIn(matrix),
            "read-marker network remains available on room activation"))
        return 1;
    matrix.setActiveConversation(receiptRoomId);
    QByteArray openedRoomRequest;
    QEventLoop openedRoomLoop;
    QObject::connect(&openedRoomServer, &QTcpServer::newConnection, &openedRoomLoop, [&]() {
        QTcpSocket *socket = openedRoomServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &openedRoomLoop, [&, socket]() {
            openedRoomRequest += socket->readAll();
            writeEmptyJsonResponse(socket);
            openedRoomLoop.quit();
        });
    });
    QTimer::singleShot(1000, &openedRoomLoop, &QEventLoop::quit);
    openedRoomLoop.exec();
    if (!check(openedRoomRequest.contains(
                "POST /_matrix/client/v3/rooms/!active-read:example.org/read_markers HTTP/1.1") &&
            openedRoomRequest.contains("\"m.read\":\"$latest-before-open\""),
            "opening a conversation sends a read marker for its latest known event"))
        return 1;

    const QString historyLoadedRoom = QStringLiteral("!open-before-history:example.org");
    QTcpServer historyLoadedServer;
    if (!historyLoadedServer.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "FAIL: local history-loaded read-marker test server starts\n";
        return 1;
    }
    MatrixNetworkTestAccess::configureReadMarkerRequest(network, historyLoadedServer.serverPort());
    matrix.setActiveConversation(historyLoadedRoom);
    MatrixTimelineEvent latestHistoricalEvent;
    latestHistoricalEvent.roomId = historyLoadedRoom;
    latestHistoricalEvent.eventId = QStringLiteral("$historical-opened-latest");
    latestHistoricalEvent.sender = QStringLiteral("@other:example.org");
    latestHistoricalEvent.originTs = 4000;
    latestHistoricalEvent.eventType = QStringLiteral("m.room.message");
    latestHistoricalEvent.messageType = QStringLiteral("m.text");
    latestHistoricalEvent.content = QStringLiteral("cached history");
    MatrixSyncBatchTestAccess::loadCachedHistory(matrix, historyLoadedRoom, {latestHistoricalEvent});
    QByteArray historyLoadedRequest;
    QEventLoop historyLoadedLoop;
    QObject::connect(&historyLoadedServer, &QTcpServer::newConnection, &historyLoadedLoop, [&]() {
        QTcpSocket *socket = historyLoadedServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, &historyLoadedLoop, [&, socket]() {
            historyLoadedRequest += socket->readAll();
            writeEmptyJsonResponse(socket);
            historyLoadedLoop.quit();
        });
    });
    QTimer::singleShot(1000, &historyLoadedLoop, &QEventLoop::quit);
    historyLoadedLoop.exec();
    if (!check(historyLoadedRequest.contains(
                "POST /_matrix/client/v3/rooms/!open-before-history:example.org/read_markers HTTP/1.1") &&
            historyLoadedRequest.contains("\"m.read\":\"$historical-opened-latest\""),
            "opening a room before history loads marks its latest cached event read"))
        return 1;

    const QString selfUserId = QStringLiteral("@self:example.org");
    const QString selfAvatarUrl = QStringLiteral("mxc://example.org/self-avatar");
    ProtocolRoom avatarRoom;
    avatarRoom.id = QStringLiteral("!avatar-room:example.org");
    ProtocolRosterEntry selfMember;
    selfMember.id = selfUserId;
    selfMember.avatarUrl = selfAvatarUrl;
    avatarRoom.members.append(selfMember);
    MatrixSyncBatchTestAccess::setNetworkUserId(matrix, selfUserId);
    MatrixSyncBatchTestAccess::setProtocolRooms(matrix, {avatarRoom});
    const QString expectedAvatarKey = MatrixAvatarKeyPolicy::userAvatarKey(
        matrix.accountId(), selfUserId, selfAvatarUrl);
    if (!check(matrix.accountAvatarKey() == expectedAvatarKey,
            "account avatar key comes from the logged-in user's room profile"))
        return 1;
    MatrixSyncBatchTestAccess::setNetworkUserId(matrix, QString());
    MatrixSyncBatchTestAccess::setProtocolRooms(matrix, {});

    QList<MatrixTextEvent> events;
    QStringList expectedIds;
    constexpr int eventCount = 61;
    for (int index = 0; index < eventCount; ++index) {
        MatrixTextEvent event;
        event.roomId = QStringLiteral("!sync-batch:example.invalid");
        event.eventId = QStringLiteral("$sync-%1").arg(index);
        event.userId = QStringLiteral("@sender:example.invalid");
        event.content = QStringLiteral("synthetic sync event %1").arg(index);
        event.timestamp = QString::number(1000 + index);
        event.eventType = QStringLiteral("m.room.message");
        event.messageType = QStringLiteral("m.text");
        events.append(event);
        expectedIds.append(event.eventId);
    }

    int batchSignalCount = 0;
    int perMessageSignalCount = 0;
    QStringList receivedIds;
    QObject::connect(&network, &MatrixNetwork::syncReceived, &network,
        [&batchSignalCount](const QList<MatrixTextEvent> &) { ++batchSignalCount; });
    QObject::connect(&network, &MatrixNetwork::messageReceived, &network,
        [&perMessageSignalCount](const BasicMessage &) { ++perMessageSignalCount; });
    QObject::connect(&network, SIGNAL(syncReceived(QList<MatrixTextEvent>)),
        &matrix, SLOT(onSyncReceived(QList<MatrixTextEvent>)), Qt::DirectConnection);
    QObject::connect(&network, SIGNAL(messageHistoryChanged(QString,QList<MatrixTextEvent>)),
        &matrix, SLOT(onMessageHistoryChanged(QString,QList<MatrixTextEvent>)), Qt::DirectConnection);
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&receivedIds](const BasicMessage &message) { receivedIds.append(message.messageId()); });

    const bool dispatched = QMetaObject::invokeMethod(&network, "dispatchSyncEventBatch",
        Qt::DirectConnection, Q_ARG(QList<MatrixTextEvent>, events));
    if (!check(dispatched, "sync events are dispatched through the single batch signal"))
        return 1;
    if (!check(batchSignalCount == 1 && perMessageSignalCount == 0,
            "one sync batch does not enqueue one cross-thread message signal per event"))
        return 1;

    int turns = 0;
    while (receivedIds.size() < eventCount && turns < eventCount) {
        const int previousCount = receivedIds.size();
        if (!check(QMetaObject::invokeMethod(&matrix, "processNextProtocolEventBatch",
                Qt::DirectConnection),
                "Matrix exposes its bounded sync-event batch processor"))
            return 1;
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "each UI processing turn handles a bounded number of sync events"))
            return 1;
        ++turns;
    }

    if (!check(receivedIds == expectedIds,
            "batched sync delivery preserves every message exactly once and in order"))
        return 1;


    QList<MatrixTextEvent> historyEvents;
    QStringList expectedHistoryIds;
    for (int index = 0; index < eventCount; ++index) {
        MatrixTextEvent event;
        event.roomId = QStringLiteral("!sync-batch:example.invalid");
        event.eventId = QStringLiteral("$history-%1").arg(index);
        event.userId = QStringLiteral("@sender:example.invalid");
        event.content = QStringLiteral("synthetic history event %1").arg(index);
        event.timestamp = QString::number(2000 + index);
        event.eventType = QStringLiteral("m.room.message");
        event.messageType = QStringLiteral("m.text");
        if (index == 5 || index == 6) {
            event.metadata.insert(QStringLiteral("outer_event_type"),
                QStringLiteral("m.room.encrypted"));
            event.metadata.insert(QStringLiteral("decryption_status"),
                index == 5 ? QStringLiteral("failed") : QStringLiteral("decrypted"));
        }
        historyEvents.append(event);
        if (index != 5)
            expectedHistoryIds.append(event.eventId);
    }

    const int historyStart = receivedIds.size();
    network.messageHistoryChanged(QStringLiteral("!sync-batch:example.invalid"), historyEvents);
    turns = 0;
    while (receivedIds.size() - historyStart < expectedHistoryIds.size() && turns < eventCount) {
        const int previousCount = receivedIds.size();
        if (!check(QMetaObject::invokeMethod(&matrix, "processNextProtocolEventBatch",
                Qt::DirectConnection),
                "Matrix exposes its bounded history-event batch processor"))
            return 1;
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "each UI processing turn handles a bounded number of history events"))
            return 1;
        ++turns;
    }

    if (!check(receivedIds.mid(historyStart) == expectedHistoryIds,
            "batched history preserves filtering, ordering and exactly-once delivery"))
        return 1;


    const QString pendingRoom = QStringLiteral("!pending-batch:example.invalid");
    MatrixSyncBatchTestAccess::setHistoryLoading(matrix, pendingRoom);
    MatrixTimelineEvent cachedEvent;
    cachedEvent.roomId = pendingRoom;
    cachedEvent.eventId = QStringLiteral("$cached-first");
    cachedEvent.sender = QStringLiteral("@sender:example.invalid");
    cachedEvent.originTs = 3000;
    cachedEvent.eventType = QStringLiteral("m.room.message");
    cachedEvent.messageType = QStringLiteral("m.text");
    cachedEvent.content = QStringLiteral("cached event");
    cachedEvent.metadata.insert(QStringLiteral("sender_is_self"), true);
    bool cachedSenderIsSelf = false;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&cachedEvent, &cachedSenderIsSelf](const BasicMessage &message) {
            if (message.messageId() == cachedEvent.eventId)
                cachedSenderIsSelf = message.metadata().value(
                    QStringLiteral("sender_is_self")).toBool();
        });
    MatrixSyncBatchTestAccess::loadCachedHistory(matrix, pendingRoom, {cachedEvent});

    QStringList expectedPendingIds{cachedEvent.eventId};
    for (int index = 0; index < 70; ++index) {
        const QString id = QStringLiteral("$pending-%1").arg(index);
        BasicMessage message(id, pendingRoom, QStringLiteral("@sender:example.invalid"),
            QString(), QStringLiteral("pending event %1").arg(index),
            QDateTime::fromMSecsSinceEpoch(4000 + index), QStringLiteral("matrix"),
            BasicMessage::Incoming);
        MatrixSyncBatchTestAccess::queuePendingMessage(matrix, message);
        expectedPendingIds.append(id);
    }

    QStringList historyLoadedRooms;
    QObject::connect(&matrix, &Matrix::protocolHistoryLoaded, &matrix,
        [&historyLoadedRooms](const QString &roomId) { historyLoadedRooms.append(roomId); });
    const int pendingStart = receivedIds.size();
    MatrixSyncBatchTestAccess::processCachedHistoryBatch(matrix);
    const int firstPendingTurnCount = receivedIds.size() - pendingStart;
    if (!check(firstPendingTurnCount > 0 && firstPendingTurnCount <= 25,
            "finishing cached history does not synchronously flush an unbounded pending backlog"))
        return 1;

    turns = 0;
    while (receivedIds.size() - pendingStart < expectedPendingIds.size() && turns < 10) {
        const int previousCount = receivedIds.size();
        MatrixSyncBatchTestAccess::processCachedHistoryBatch(matrix);
        const int processedThisTurn = receivedIds.size() - previousCount;
        if (!check(processedThisTurn > 0 && processedThisTurn <= 25,
                "cached and pending history share bounded processing turns"))
            return 1;
        ++turns;
    }
    if (!check(receivedIds.mid(pendingStart) == expectedPendingIds &&
            historyLoadedRooms == QStringList{pendingRoom},
            "cached history completion preserves order and signals once after pending delivery"))
        return 1;
    if (!check(cachedSenderIsSelf,
            "cached own-message identity survives history loading before login"))
        return 1;


    const QString historyPageRoom = QStringLiteral("!history-page-self:example.invalid");
    MatrixTimelineEvent historyPageEvent = cachedEvent;
    historyPageEvent.roomId = historyPageRoom;
    historyPageEvent.eventId = QStringLiteral("$cached-page-own");
    MatrixHistoryPageResult historyPageResult;
    historyPageResult.success = true;
    historyPageResult.events.append(historyPageEvent);
    ProtocolHistoryPage loadedPage;
    QObject callbackContext;
    MatrixSyncBatchTestAccess::loadHistoryPage(matrix, historyPageRoom,
        historyPageResult, &callbackContext,
        [&loadedPage](ProtocolHistoryPage page) { loadedPage = page; });
    QCoreApplication::processEvents();
    if (!check(loadedPage.success && loadedPage.messages.size() == 1 &&
            loadedPage.messages.first().metadata().value(
                QStringLiteral("sender_is_self")).toBool(),
            "paginated cached history preserves own-message identity before login"))
        return 1;

    const QString configuredUserId = QStringLiteral("@alice:example.invalid");
    MatrixSyncBatchTestAccess::setConfiguredUserId(matrix, configuredUserId);

    const QString legacyRoom = QStringLiteral("!legacy-cache:example.invalid");
    MatrixTimelineEvent legacyOwnEvent;
    legacyOwnEvent.roomId = legacyRoom;
    legacyOwnEvent.eventId = QStringLiteral("$legacy-own");
    legacyOwnEvent.sender = configuredUserId;
    legacyOwnEvent.originTs = 5000;
    legacyOwnEvent.eventType = QStringLiteral("m.room.message");
    legacyOwnEvent.messageType = QStringLiteral("m.text");
    legacyOwnEvent.content = QStringLiteral("legacy cached own message");
    MatrixTimelineEvent legacyRemoteEvent = legacyOwnEvent;
    legacyRemoteEvent.eventId = QStringLiteral("$legacy-remote");
    legacyRemoteEvent.sender = QStringLiteral("@bob:example.invalid");
    legacyRemoteEvent.content = QStringLiteral("legacy cached remote message");
    bool legacyCachedOwnIsSelf = false;
    bool legacyCachedRemoteIsSelf = true;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&legacyCachedOwnIsSelf, &legacyCachedRemoteIsSelf](const BasicMessage &message) {
            if (message.messageId() == QStringLiteral("$legacy-own"))
                legacyCachedOwnIsSelf = message.metadata().value(
                    QStringLiteral("sender_is_self")).toBool();
            else if (message.messageId() == QStringLiteral("$legacy-remote"))
                legacyCachedRemoteIsSelf = message.metadata().value(
                    QStringLiteral("sender_is_self")).toBool();
        });
    MatrixSyncBatchTestAccess::loadCachedHistory(matrix, legacyRoom,
        {legacyOwnEvent, legacyRemoteEvent});
    MatrixSyncBatchTestAccess::processCachedHistoryBatch(matrix);
    if (!check(legacyCachedOwnIsSelf && !legacyCachedRemoteIsSelf,
            "legacy cached history classifies configured own identity before login"))
        return 1;

    const QString legacyPageRoom = QStringLiteral("!legacy-page:example.invalid");
    legacyOwnEvent.roomId = legacyPageRoom;
    legacyRemoteEvent.roomId = legacyPageRoom;
    MatrixHistoryPageResult legacyPageResult;
    legacyPageResult.success = true;
    legacyPageResult.events = {legacyOwnEvent, legacyRemoteEvent};

    ProtocolHistoryPage legacyLoadedPage;
    QObject legacyCallbackContext;
    MatrixSyncBatchTestAccess::loadHistoryPage(matrix, legacyPageRoom,
        legacyPageResult, &legacyCallbackContext,
        [&legacyLoadedPage](ProtocolHistoryPage page) { legacyLoadedPage = page; });
    QCoreApplication::processEvents();
    if (!check(legacyLoadedPage.success && legacyLoadedPage.messages.size() == 2 &&
            legacyLoadedPage.messages.at(0).metadata().value(
                QStringLiteral("sender_is_self")).toBool() &&
            !legacyLoadedPage.messages.at(1).metadata().value(
                QStringLiteral("sender_is_self")).toBool(),
            "legacy paginated history classifies configured own identity before login"))
        return 1;

    const QString liveUserId = QStringLiteral("@alice:example.invalid");
    MatrixSyncBatchTestAccess::setNetworkUserId(matrix, liveUserId);
    MatrixTextEvent liveOwnEvent;
    liveOwnEvent.roomId = QStringLiteral("!live-self:example.invalid");
    liveOwnEvent.eventId = QStringLiteral("$live-own");
    liveOwnEvent.userId = liveUserId;
    liveOwnEvent.content = QStringLiteral("live own sync event");
    liveOwnEvent.timestamp = QStringLiteral("6000");
    liveOwnEvent.eventType = QStringLiteral("m.room.message");
    liveOwnEvent.messageType = QStringLiteral("m.text");
    MatrixTextEvent liveRemoteEvent = liveOwnEvent;
    liveRemoteEvent.eventId = QStringLiteral("$live-remote");
    liveRemoteEvent.userId = QStringLiteral("@bob:example.invalid");
    bool liveOwnIsSelf = false;
    bool liveRemoteIsSelf = true;
    QObject::connect(&matrix, &Matrix::protocolMessageReceived, &matrix,
        [&liveOwnIsSelf, &liveRemoteIsSelf](const BasicMessage &message) {
            if (message.messageId() == QStringLiteral("$live-own"))
                liveOwnIsSelf = message.metadata().value(
                    QStringLiteral("sender_is_self")).toBool();
            else if (message.messageId() == QStringLiteral("$live-remote"))
                liveRemoteIsSelf = message.metadata().value(
                    QStringLiteral("sender_is_self")).toBool();
        });
    MatrixSyncBatchTestAccess::dispatchLiveSyncEvents(matrix,
        {liveOwnEvent, liveRemoteEvent});
    if (!check(QMetaObject::invokeMethod(&matrix, "processNextProtocolEventBatch",
            Qt::DirectConnection), "live sync events use the production batch handler"))
        return 1;
    if (!check(liveOwnIsSelf && !liveRemoteIsSelf,
            "live sync marks the logged-in sender as self and other senders as remote"))
        return 1;

    // A locally sent reply is displayed immediately and is the sole UI event for
    // that transaction. Pending/network, sync, history-snapshot and SQLite copies
    // must not replace its reply rendering or deliver another protocol message.
    MatrixNetwork localEchoNetwork;
    Matrix localEchoMatrix;
    const QString localUser = QStringLiteral("@alice:example.invalid");
    const QString localRoom = QStringLiteral("!local-echo:example.invalid");
    MatrixSyncBatchTestAccess::configureMessageSending(localEchoMatrix,
        &localEchoNetwork, localUser);
    QList<BasicMessage> localEchoDeliveries;
    QObject::connect(&localEchoMatrix, &Matrix::protocolMessageReceived, &localEchoMatrix,
        [&localEchoDeliveries](const BasicMessage &message) {
            localEchoDeliveries.append(message);
        });
    BasicMessage replyToSend(QString(), localRoom, localUser, QString(),
        QStringLiteral("my reply"), QDateTime::fromMSecsSinceEpoch(7000, QTimeZone::utc()),
        QStringLiteral("matrix"), BasicMessage::Outgoing);
    replyToSend.setMetadata({{QStringLiteral("msgtype"), QStringLiteral("m.text")},
        {QStringLiteral("reply_to_event_id"), QStringLiteral("$original-message")}});
    if (!check(localEchoMatrix.sendMessage(replyToSend) && localEchoDeliveries.size() == 1,
            "sending a reply immediately emits exactly one local echo"))
        return 1;
    const BasicMessage localEcho = localEchoDeliveries.first();
    const QString localTransactionId = localEcho.metadata().value(
        QStringLiteral("txn_id")).toString();
    if (!check(!localTransactionId.isEmpty() &&
            localEcho.metadata().value(QStringLiteral("reply_to_event_id")).toString() ==
                QStringLiteral("$original-message"),
            "the immediate local echo keeps its transaction ID and reply target"))
        return 1;

    BasicMessage pendingEcho(localTransactionId, localRoom, localUser, QString(),
        QStringLiteral("my reply"), QDateTime::fromMSecsSinceEpoch(7000, QTimeZone::utc()),
        QStringLiteral("matrix"), BasicMessage::Outgoing);
    pendingEcho.setMetadata({{QStringLiteral("txn_id"), localTransactionId},
        {QStringLiteral("reply_to_event_id"), QStringLiteral("$original-message")}});
    MatrixSyncBatchTestAccess::deliverPendingMessage(localEchoMatrix, pendingEcho);
    if (!check(localEchoDeliveries.size() == 1,
            "the network pending copy of a sent reply is ignored"))
        return 1;

    MatrixTextEvent serverEcho;
    serverEcho.roomId = localRoom;
    serverEcho.eventId = QStringLiteral("$server-reply");
    serverEcho.userId = localUser;
    serverEcho.content = QStringLiteral("my reply");
    serverEcho.timestamp = QStringLiteral("7001");
    serverEcho.eventType = QStringLiteral("m.room.message");
    serverEcho.messageType = QStringLiteral("m.text");
    serverEcho.metadata.insert(QStringLiteral("replaces_txn_id"), localTransactionId);
    serverEcho.metadata.insert(QStringLiteral("reply_to_event_id"), QStringLiteral("$original-message"));
    MatrixTextEvent pendingStored = serverEcho;
    pendingStored.eventId = localTransactionId;
    pendingStored.metadata.clear();
    pendingStored.metadata.insert(QStringLiteral("txn_id"), localTransactionId);
    MatrixTextEvent laterStored = pendingStored;
    laterStored.eventId = QStringLiteral("$later-message");
    laterStored.timestamp = QStringLiteral("8000");
    MatrixNetworkTestAccess::seedPendingEvent(localEchoNetwork, laterStored);
    MatrixNetworkTestAccess::seedPendingEvent(localEchoNetwork, pendingStored);
    int replaceHistorySnapshotCount = 0;
    QObject::connect(&localEchoNetwork, &MatrixNetwork::messageHistoryChanged,
        &localEchoNetwork, [&replaceHistorySnapshotCount](const QString &,
            const QList<MatrixTextEvent> &) { ++replaceHistorySnapshotCount; });
    const bool replacedPending = MatrixNetworkTestAccess::replacePendingEvent(
        localEchoNetwork, localRoom, localTransactionId, serverEcho);
    const QList<MatrixTextEvent> replacedNetworkHistory =
        localEchoNetwork.messageHistory(localRoom);
    if (!check(replacedPending && replacedNetworkHistory.size() == 2 &&
            replacedNetworkHistory.first().eventId == serverEcho.eventId &&
            replacedNetworkHistory.last().eventId == laterStored.eventId &&
            replaceHistorySnapshotCount == 0,
            "server acknowledgement restores chronological event order without replaying history to UI"))
        return 1;
    MatrixSyncBatchTestAccess::dispatchLiveSyncEvents(localEchoMatrix, {serverEcho});
    if (!check(QMetaObject::invokeMethod(&localEchoMatrix, "processNextProtocolEventBatch",
            Qt::DirectConnection) && localEchoDeliveries.size() == 1 &&
            MatrixSyncBatchTestAccess::latestConversationEventId(localEchoMatrix, localRoom) ==
                serverEcho.eventId,
            "the /sync echo is suppressed from UI without skipping Matrix state updates"))
        return 1;

    MatrixSyncBatchTestAccess::dispatchHistorySnapshot(localEchoMatrix, localRoom,
        {serverEcho});
    if (!check(QMetaObject::invokeMethod(&localEchoMatrix, "processNextProtocolEventBatch",
            Qt::DirectConnection) && localEchoDeliveries.size() == 1,
            "the replacement history snapshot does not emit the sent reply again"))
        return 1;

    MatrixTimelineEvent cachedReply;
    cachedReply.roomId = localRoom;
    cachedReply.eventId = serverEcho.eventId;
    cachedReply.sender = localUser;
    cachedReply.originTs = 7001;
    cachedReply.eventType = serverEcho.eventType;
    cachedReply.messageType = serverEcho.messageType;
    cachedReply.content = serverEcho.content;
    for (auto it = serverEcho.metadata.constBegin(); it != serverEcho.metadata.constEnd(); ++it)
        cachedReply.metadata.insert(it.key(), it.value());
    MatrixSyncBatchTestAccess::loadCachedHistory(localEchoMatrix, localRoom,
        {cachedReply});
    MatrixSyncBatchTestAccess::processCachedHistoryBatch(localEchoMatrix);
    if (!check(localEchoDeliveries.size() == 1,
            "the SQLite history copy does not emit the sent reply again"))
        return 1;

    MatrixTextEvent incomingControl = serverEcho;
    incomingControl.eventId = QStringLiteral("$incoming-control");
    incomingControl.userId = QStringLiteral("@bob:example.invalid");
    incomingControl.metadata.clear();
    MatrixSyncBatchTestAccess::dispatchLiveSyncEvents(localEchoMatrix,
        {incomingControl});
    if (!check(QMetaObject::invokeMethod(&localEchoMatrix, "processNextProtocolEventBatch",
            Qt::DirectConnection) && localEchoDeliveries.size() == 2 &&
            localEchoDeliveries.last().messageId() == incomingControl.eventId,
            "an unrelated incoming message still reaches the UI exactly once"))
        return 1;

    const QString imageRoom = QStringLiteral("!sent-image-cache:example.invalid");
    MatrixNetwork outgoingImageNetwork;
    MatrixNetworkTestAccess::configureLocalMessageSend(outgoingImageNetwork, imageRoom);
    const QString imageMxcUrl = QStringLiteral("mxc://media.example.invalid/sent-image");
    const QJsonObject imageInfo{{QStringLiteral("mimetype"), QStringLiteral("image/png")},
        {QStringLiteral("w"), 320}, {QStringLiteral("h"), 240}};
    const QJsonObject imageContent{{QStringLiteral("msgtype"), QStringLiteral("m.image")},
        {QStringLiteral("body"), QStringLiteral("image.png")},
        {QStringLiteral("url"), imageMxcUrl}, {QStringLiteral("info"), imageInfo}};
    outgoingImageNetwork.sendMessageEvent(imageRoom, imageContent,
        QStringLiteral("synthetic-image-txn"));
    const MatrixTextEvent pendingImage = MatrixNetworkTestAccess::pendingMessage(
        outgoingImageNetwork, imageRoom);
    if (!check(pendingImage.messageType == QStringLiteral("m.image") &&
            pendingImage.metadata.value(QStringLiteral("msgtype")).toString() ==
                QStringLiteral("m.image") &&
            pendingImage.metadata.value(QStringLiteral("url")).toString() == imageMxcUrl &&
            pendingImage.metadata.value(QStringLiteral("info")).toMap()
                .value(QStringLiteral("mimetype")).toString() == QStringLiteral("image/png"),
            "a sent image's local pending event retains the media URL and image info for cache replay"))
        return 1;

    std::cout << "PASS: Matrix sync events are delivered in bounded ordered batches\n";
    return 0;
}
