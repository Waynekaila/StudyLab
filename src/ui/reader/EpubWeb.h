#pragma once

#include <QHash>
#include <QString>
#include <QWebEngineUrlSchemeHandler>

#include <memory>

class QWebEngineProfile;

namespace studylab {
class EpubPackage;
}

namespace studylab::ui {

/// Schéma interne du lecteur EPUB. Une seule origine pour l'application et les livres :
///   studylab-epub://reader/app/<fichier>          lecteur (ressources Qt, foliate-js)
///   studylab-epub://reader/book/<jeton>/<chemin>  fichiers d'un livre, lus dans le ZIP sans extraction
inline constexpr char kEpubScheme[] = "studylab-epub";

/// Déclare le schéma auprès de Qt WebEngine. À appeler avant la création de QApplication.
void registerReaderSchemes();

class EpubSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit EpubSchemeHandler(QObject* parent = nullptr);
    ~EpubSchemeHandler() override;

    void requestStarted(QWebEngineUrlRequestJob* job) override;

    /// Rend un livre accessible sous un jeton ; il le reste jusqu'à unregisterBook.
    void registerBook(const QString& token, std::shared_ptr<EpubPackage> package);
    void unregisterBook(const QString& token);

    static QByteArray mimeTypeFor(const QString& path);

private:
    struct Book {
        std::shared_ptr<EpubPackage> package;
        QHash<QString, qint64> sizes;
    };
    QHash<QString, Book> m_books;
};

/// Profil Web du lecteur : hors ligne, sans cookies ni cache disque, schéma installé, requêtes vers
/// tout autre schéma refusées. Créé à la première ouverture d'un EPUB (WebEngine coûte ~100 Mo).
QWebEngineProfile* readerProfile();
EpubSchemeHandler* readerSchemeHandler();

}  // namespace studylab::ui
