#include "matrixnetwork.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <iostream>

class MatrixNetworkTestAccess
{
public:
    static void setSession(MatrixNetwork &network, const QString &userId, const QString &token)
    {
        network.FUserId = userId;
        network.FAccesToken = token;
    }

    static void setSessionGeneration(MatrixNetwork &network, quint64 generation)
    {
        network.FActiveLoginGeneration = generation;
    }
};

namespace {
struct HttpRequest
{
    QByteArray method;
    QByteArray target;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

class AvatarTestServer : public QTcpServer
{
public:
    struct Reply
    {
        int status = 200;
        QByteArray body = QByteArrayLiteral("{}");
    };

    bool start()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                FBuffers.insert(socket, QByteArray());
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { process(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                if (socket->bytesAvailable() > 0)
                    process(socket);
            }
        });
        return listen(QHostAddress::LocalHost, 0);
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }

    QList<HttpRequest> requests() const { return FRequests; }
    void setReplies(const QList<Reply> &replies) { FReplies = replies; }

private:
    void process(QTcpSocket *socket)
    {
        if (!FBuffers.contains(socket))
            return;
        QByteArray &requestBytes = FBuffers[socket];
        requestBytes += socket->readAll();
        const int headerEnd = requestBytes.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QList<QByteArray> lines = requestBytes.left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        if (requestLine.size() < 2) {
            socket->abort();
            return;
        }

        HttpRequest request;
        request.method = requestLine.at(0);
        request.target = requestLine.at(1);
        int contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            request.headers.insert(key, value);
            if (key == QByteArrayLiteral("content-length"))
                contentLength = value.toInt();
        }
        if (requestBytes.size() < headerEnd + 4 + contentLength)
            return;
        request.body = requestBytes.mid(headerEnd + 4, contentLength);
        FRequests.append(request);

        const int requestIndex = FRequests.size() - 1;
        const Reply reply = requestIndex < FReplies.size() ? FReplies.at(requestIndex) : Reply();
        const QByteArray status = reply.status == 200 ? QByteArrayLiteral("200 OK") :
            QByteArray::number(reply.status) + QByteArrayLiteral(" Error");
        QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + status + QByteArrayLiteral("\r\n");
        response += QByteArrayLiteral("Content-Type: application/json\r\n");
        response += QByteArrayLiteral("Content-Length: ") + QByteArray::number(reply.body.size()) + QByteArrayLiteral("\r\n");
        response += QByteArrayLiteral("Connection: close\r\n\r\n") + reply.body;
        FBuffers.remove(socket);
        socket->write(response);
        socket->disconnectFromHost();
    }

    QHash<QTcpSocket *, QByteArray> FBuffers;
    QList<HttpRequest> FRequests;
    QList<Reply> FReplies;
};

QByteArray syntheticPng()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return QByteArray();
    return bytes;
}

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

struct Completion
{
    bool finished = false;
    bool success = false;
    QString userId;
    QString avatarUrl;
    QString error;
};

Completion setAvatarAndWait(MatrixNetwork &network, const QByteArray &imageData)
{
    Completion completion;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const QMetaObject::Connection resultConnection = QObject::connect(
        &network, &MatrixNetwork::accountAvatarUpdateFinished, &loop,
        [&completion, &loop](const QString &userId, bool success,
            const QString &avatarUrl, const QString &error) {
            completion.finished = true;
            completion.success = success;
            completion.userId = userId;
            completion.avatarUrl = avatarUrl;
            completion.error = error;
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    network.setOwnAvatar(imageData);
    if (!completion.finished) {
        timeout.start(5000);
        loop.exec();
    }
    QObject::disconnect(resultConnection);
    return completion;
}

bool testUploadThenSetProfileAvatar()
{
    AvatarTestServer server;
    if (!check(server.start(), "local avatar HTTP server did not start"))
        return false;
    server.setReplies({AvatarTestServer::Reply{200,
        QByteArrayLiteral("{\"content_uri\":\"mxc://media.example.org/avatar-test\"}")},
        AvatarTestServer::Reply{200, QByteArrayLiteral("{}")}});

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    const QString userId = QStringLiteral("@avatar-test:example.org");
    MatrixNetworkTestAccess::setSession(network, userId, QStringLiteral("synthetic-token"));
    const QByteArray imageData = syntheticPng();
    if (!check(!imageData.isEmpty(), "could not generate synthetic PNG data"))
        return false;

    const Completion completion = setAvatarAndWait(network, imageData);
    if (!check(completion.finished, "avatar request did not finish before timeout") ||
        !check(completion.success, "upload and profile update were not reported successful") ||
        !check(completion.userId == userId, "completion was not bound to the updating Matrix user") ||
        !check(completion.avatarUrl == QStringLiteral("mxc://media.example.org/avatar-test"),
            "completion did not report the uploaded MXC URI"))
        return false;

    const QList<HttpRequest> requests = server.requests();
    if (!check(requests.size() == 2, "avatar update must make exactly one upload and one profile request"))
        return false;
    if (!check(requests.at(0).method == QByteArrayLiteral("POST") &&
        requests.at(0).target == QByteArrayLiteral("/_matrix/media/v3/upload"),
        "first request must POST to the Matrix media upload endpoint"))
        return false;
    if (!check(requests.at(0).headers.value(QByteArrayLiteral("authorization")) ==
        QByteArrayLiteral("Bearer synthetic-token"), "media upload omitted the access token"))
        return false;
    if (!check(requests.at(0).body == imageData, "media upload body did not preserve the image bytes"))
        return false;

    if (!check(requests.at(1).method == QByteArrayLiteral("PUT") &&
        requests.at(1).target == QByteArrayLiteral(
            "/_matrix/client/v3/profile/%40avatar-test%3Aexample.org/avatar_url"),
        "second request must update the URL-encoded user's avatar profile"))
        return false;
    if (!check(requests.at(1).headers.value(QByteArrayLiteral("authorization")) ==
        QByteArrayLiteral("Bearer synthetic-token"), "profile update omitted the access token"))
        return false;
    const QJsonObject profile = QJsonDocument::fromJson(requests.at(1).body).object();
    return check(profile.value(QStringLiteral("avatar_url")).toString() == completion.avatarUrl,
        "profile update did not use the uploaded MXC URI");
}

bool testClearOwnAvatar()
{
    AvatarTestServer server;
    if (!check(server.start(), "local avatar HTTP server did not start for clear request"))
        return false;

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    MatrixNetworkTestAccess::setSession(network, QStringLiteral("@avatar-test:example.org"),
        QStringLiteral("synthetic-token"));

    const Completion completion = setAvatarAndWait(network, QByteArray());
    if (!check(completion.finished, "avatar clear did not finish before timeout") ||
        !check(completion.success, "avatar clear was not reported successful") ||
        !check(completion.avatarUrl.isEmpty(), "avatar clear returned a non-empty URL"))
        return false;

    const QList<HttpRequest> requests = server.requests();
    if (!check(requests.size() == 1, "avatar clear must make one profile request and no upload"))
        return false;
    if (!check(requests.first().method == QByteArrayLiteral("PUT") &&
        requests.first().target == QByteArrayLiteral(
            "/_matrix/client/v3/profile/%40avatar-test%3Aexample.org/avatar_url"),
        "avatar clear must PUT the URL-encoded user's profile"))
        return false;
    const QJsonObject profile = QJsonDocument::fromJson(requests.first().body).object();
    return check(profile.value(QStringLiteral("avatar_url")).toString().isEmpty(),
        "avatar clear did not send an empty avatar_url");
}

bool testUploadFailureDoesNotUpdateProfile()
{
    AvatarTestServer server;
    if (!check(server.start(), "local avatar HTTP server did not start for upload failure"))
        return false;
    server.setReplies({AvatarTestServer::Reply{500,
        QByteArrayLiteral("{\"errcode\":\"M_UNKNOWN\",\"error\":\"upload failed\"}")}});

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    MatrixNetworkTestAccess::setSession(network, QStringLiteral("@avatar-test:example.org"),
        QStringLiteral("synthetic-token"));
    const Completion completion = setAvatarAndWait(network, syntheticPng());
    if (!check(completion.finished, "failed avatar upload did not finish before timeout") ||
        !check(!completion.success, "failed media upload was reported successful") ||
        !check(!completion.error.isEmpty(), "failed media upload returned no error"))
        return false;

    const QList<HttpRequest> requests = server.requests();
    return check(requests.size() == 1 && requests.first().method == QByteArrayLiteral("POST"),
        "failed media upload must not issue a profile update");
}

struct DisplayNameCompletion
{
    bool finished = false;
    bool success = false;
    QString userId;
    QString displayName;
    QString error;
};

DisplayNameCompletion setDisplayNameAndWait(MatrixNetwork &network, const QString &displayName,
	const std::function<void()> &afterRequest = std::function<void()>())
{
    DisplayNameCompletion completion;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const QMetaObject::Connection resultConnection = QObject::connect(
        &network, &MatrixNetwork::accountDisplayNameUpdateFinished, &loop,
        [&completion, &loop](const QString &userId, bool success,
            const QString &displayName, const QString &error) {
            completion.finished = true;
            completion.success = success;
            completion.userId = userId;
            completion.displayName = displayName;
            completion.error = error;
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    network.setOwnDisplayName(displayName);
    if (afterRequest)
        afterRequest();
    if (!completion.finished) {
        timeout.start(5000);
        loop.exec();
    }
    QObject::disconnect(resultConnection);
    return completion;
}

bool testSetOwnDisplayName()
{
    AvatarTestServer server;
    if (!check(server.start(), "local display-name HTTP server did not start"))
        return false;

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    const QString userId = QStringLiteral("@display-name-test:example.org");
    MatrixNetworkTestAccess::setSession(network, userId, QStringLiteral("synthetic-token"));
    const QString displayName = QStringLiteral("New Display Name");
    const DisplayNameCompletion completion = setDisplayNameAndWait(network, displayName);
    if (!check(completion.finished, "display-name request did not finish before timeout") ||
        !check(completion.success, "display-name update was not reported successful") ||
        !check(completion.userId == userId, "completion was not bound to the updating Matrix user") ||
        !check(completion.displayName == displayName, "completion returned the wrong display name"))
        return false;

    const QList<HttpRequest> requests = server.requests();
    if (!check(requests.size() == 1, "display-name update must issue exactly one request"))
        return false;
    if (!check(requests.first().method == QByteArrayLiteral("PUT") &&
        requests.first().target == QByteArrayLiteral(
            "/_matrix/client/v3/profile/%40display-name-test%3Aexample.org/displayname"),
        "display-name update must PUT the URL-encoded user's profile"))
        return false;
    if (!check(requests.first().headers.value(QByteArrayLiteral("authorization")) ==
        QByteArrayLiteral("Bearer synthetic-token"), "display-name update omitted the access token"))
        return false;
    const QJsonObject profile = QJsonDocument::fromJson(requests.first().body).object();
    return check(profile.value(QStringLiteral("displayname")).toString() == displayName,
        "display-name update did not send the requested value");
}

bool testStaleDisplayNameUpdateCannotReportSuccess()
{
    AvatarTestServer server;
    if (!check(server.start(), "local display-name HTTP server did not start for stale-session test"))
        return false;

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    const QString userId = QStringLiteral("@display-name-test:example.org");
    MatrixNetworkTestAccess::setSession(network, userId, QStringLiteral("synthetic-token"));
    MatrixNetworkTestAccess::setSessionGeneration(network, 1);
    const DisplayNameCompletion completion = setDisplayNameAndWait(network,
        QStringLiteral("Stale Name"), [&network]() {
            MatrixNetworkTestAccess::setSessionGeneration(network, 2);
        });
    if (!check(completion.finished, "stale display-name request did not finish before timeout") ||
        !check(!completion.success, "stale display-name update was reported successful") ||
        !check(!completion.error.isEmpty(), "stale display-name update returned no error"))
        return false;
    return check(server.requests().size() == 1,
        "stale display-name request did not reach the deterministic local server");
}

bool testDisplayNameHttpFailureIsNotReportedSuccessful()
{
    AvatarTestServer server;
    if (!check(server.start(), "local display-name HTTP server did not start for failure test"))
        return false;
    server.setReplies({AvatarTestServer::Reply{403,
        QByteArrayLiteral("{\"errcode\":\"M_FORBIDDEN\",\"error\":\"denied\"}")}});

    MatrixNetwork network;
    network.setServerUrl(server.baseUrl().toString());
    const QString userId = QStringLiteral("@display-name-test:example.org");
    MatrixNetworkTestAccess::setSession(network, userId, QStringLiteral("synthetic-token"));
    const DisplayNameCompletion completion = setDisplayNameAndWait(network,
        QStringLiteral("Rejected Name"));
    if (!check(completion.finished, "failed display-name request did not finish before timeout") ||
        !check(!completion.success, "HTTP failure was reported as a successful display-name update") ||
        !check(!completion.error.isEmpty(), "HTTP failure did not produce an error"))
        return false;
    const QList<HttpRequest> requests = server.requests();
    return check(requests.size() == 1 && requests.first().method == QByteArrayLiteral("PUT"),
        "failed display-name update did not issue exactly one PUT");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    bool success = testUploadThenSetProfileAvatar();
    success &= testClearOwnAvatar();
    success &= testUploadFailureDoesNotUpdateProfile();
    success &= testSetOwnDisplayName();
    success &= testStaleDisplayNameUpdateCannotReportSuccess();
    success &= testDisplayNameHttpFailureIsNotReportedSuccessful();
    return success ? 0 : 1;
}
