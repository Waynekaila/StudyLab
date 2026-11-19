#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;

namespace studylab::ui {

/// Une seule instance de StudyLab par dossier de données : un second lancement (clic sur un lien
/// « studylab:// » d'un export, raccourci du bureau) transmet ses arguments à l'instance ouverte puis s'arrête.
/// Canal local (socket Unix ou tube nommé Windows), propre à l'utilisateur et au dossier de données.
class InstanceChannel : public QObject {
    Q_OBJECT
public:
    explicit InstanceChannel(const QString& dataDir, QObject* parent = nullptr);
    ~InstanceChannel() override;

    /// Nom du canal, dérivé du dossier de données et de l'utilisateur.
    static QString serverName(const QString& dataDir);
    /// Transmet les messages à l'instance ouverte. Faux si aucune instance n'écoute.
    static bool sendToRunning(const QString& dataDir, const QStringList& messages, int timeoutMs = 2000);

    /// Commence à écouter (remplace un canal orphelin laissé par un arrêt brutal).
    bool listen();
    /// Cesse d'écouter (avant un redémarrage, pour que la nouvelle instance prenne le relais).
    void close();
    bool isListening() const;

signals:
    void messageReceived(const QString& message);

private:
    void onNewConnection();

    QString m_name;
    QLocalServer* m_server = nullptr;
};

}  // namespace studylab::ui
