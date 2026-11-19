#pragma once

#include "core/Result.h"
#include "core/domain/Document.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab::db {

class Database;

/// Critères de la liste de la bibliothèque. Les champs vides ne filtrent pas.
struct DocumentQuery {
    enum class Sort { RecentlyAdded, RecentlyOpened, Title, Author };

    QString text;                          ///< recherche plein texte (titre, auteurs, éditeur, tags, ISBN, DOI)
    std::optional<ReadingStatus> status;
    std::optional<DocumentFormat> format;
    bool favoritesOnly = false;
    QString tag;                           ///< nom exact du tag (insensible à la casse)
    Sort sort = Sort::RecentlyAdded;
    int limit = -1;
    int offset = 0;
};

/// Accès aux documents de la bibliothèque (tables documents, authors, tags, fts_documents).
/// Toutes les écritures passent par une transaction et maintiennent l'index plein texte.
class DocumentRepository {
public:
    explicit DocumentRepository(Database& db);

    /// Insère un document. Renseigne id, uuid (si vide), sortTitle, addedAt et updatedAt.
    Result<void> insert(DocumentRecord& record);
    /// Met à jour les métadonnées, auteurs et tags (le fichier et l'empreinte ne changent pas).
    Result<void> update(DocumentRecord& record);

    Result<DocumentRecord> findById(qint64 id);
    Result<std::optional<DocumentRecord>> findByHash(const QString& sha256);
    Result<QList<DocumentRecord>> list(const DocumentQuery& query = {});
    Result<int> count();
    /// Taille cumulée des fichiers des documents actifs (octets).
    Result<qint64> totalFileSize();

    Result<void> setStatus(qint64 id, ReadingStatus status);
    Result<void> setFavorite(qint64 id, bool favorite);
    Result<void> markOpened(qint64 id);
    /// Retrait logique (deleted_at) ; le document disparaît des listes et de l'index.
    Result<void> remove(qint64 id);

    /// Tags utilisés par au moins un document actif, triés.
    Result<QStringList> documentTags();
    /// Empreintes SHA-256 des documents actifs (détection des doublons à l'import).
    Result<QSet<QString>> activeHashes();

    /// Expression MATCH FTS5 sûre à partir d'une saisie libre (préfixes, ET implicite). Vide si rien à chercher.
    static QString toFtsQuery(const QString& text);

private:
    Result<void> writeAuthors(qint64 id, const QStringList& authors);
    Result<void> writeTags(qint64 id, const QStringList& tags);
    Result<void> writeFts(const DocumentRecord& record);
    Result<void> simpleUpdate(const QString& sql, qint64 id, const QString& value);

    Database& m_db;
};

}  // namespace studylab::db
