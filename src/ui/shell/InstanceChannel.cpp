#include "ui/shell/InstanceChannel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace studylab::ui {

namespace {

QString userName()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    return user;
}

}  // namespace

InstanceChannel::InstanceChannel(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_name(serverName(dataDir))
{
}

InstanceChannel::~InstanceChannel()
{
    close();
}

QString InstanceChannel::serverName(const QString& dataDir)
{
    const QByteArray key = (QDir(dataDir).absolutePath() + QLatin1Char('|') + userName()).toUtf8();
    return QStringLiteral("studylab-")
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16));
}

bool InstanceChannel::sendToRunning(const QString& dataDir, const QStringList& messages, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(serverName(dataDir));
    if (!socket.waitForConnected(timeoutMs)) {
        if (socket.error() != QLocalSocket::ServerNotFoundError && socket.error() != QLocalSocket::ConnectionRefusedError)
            qWarning("Instance déjà ouverte injoignable : %s", qPrintable(socket.errorString()));
        return false;
    }
    QByteArray payload;
    for (const QString& m : messages)
        payload += m.toUtf8().replace('\n', ' ') + '\n';
    if (socket.write(payload) != payload.size())
        return false;
    socket.flush();
    // Sous Windows, une écriture déjà terminée laisse bytesToWrite() à 0 et waitForBytesWritten() renvoie faux.
    while (socket.bytesToWrite() > 0)
        if (!socket.waitForBytesWritten(timeoutMs)) {
            qWarning("Envoi à l'instance ouverte impossible : %s", qPrintable(socket.errorString()));
            return false;
        }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(timeoutMs);
    return true;
}

bool InstanceChannel::listen()
{
    if (m_server && m_server->isListening())
        return true;
    if (!m_server) {
        m_server = new QLocalServer(this);
#ifndef Q_OS_WIN
        // Socket Unix réservé à l'utilisateur. Sous Windows, l'ACL par défaut du tube nommé n'autorise déjà
        // l'écriture qu'au créateur (et aux administrateurs), et UserAccessOption refuse les processus élevés.
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
#endif
        connect(m_server, &QLocalServer::newConnection, this, &InstanceChannel::onNewConnection);
    }
    if (m_server->listen(m_name))
        return true;
    // Canal orphelin (arrêt brutal sous Linux) : personne n'y répond, on le remplace.
    QLocalServer::removeServer(m_name);
    return m_server->listen(m_name);
}

void InstanceChannel::close()
{
    if (m_server)
        m_server->close();
}

bool InstanceChannel::isListening() const
{
    return m_server && m_server->isListening();
}

void InstanceChannel::onNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        auto drain = [this, socket, buffer](bool final) {
            *buffer += socket->readAll();
            qsizetype nl;
            while ((nl = buffer->indexOf('\n')) >= 0) {
                const QString message = QString::fromUtf8(buffer->left(nl)).trimmed();
                buffer->remove(0, nl + 1);
                if (!message.isEmpty())
                    emit messageReceived(message);
            }
            if (final && !buffer->trimmed().isEmpty()) {
                emit messageReceived(QString::fromUtf8(buffer->trimmed()));
                buffer->clear();
            }
        };
        connect(socket, &QLocalSocket::readyRead, this, [drain] { drain(false); });
        connect(socket, &QLocalSocket::disconnected, this, [drain, socket] {
            drain(true);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0)
            drain(false);
    }
}

}  // namespace studylab::ui
