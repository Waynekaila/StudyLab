#pragma once

#include "core/Result.h"
#include "core/domain/Note.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace studylab::db {

class Database;

/// Lien [[…]] sortant d'une note, tel qu'enregistré dans la table links (origin = 'wikilink').
struct NoteLinkRow {
    qint64 id = 0;
    qint64 sourceNoteId = 0;
    QString targetText;     ///< texte du lien, sans « |alias »
    qint64 targetNoteId = 0;  ///< 0 : non résolu
};

/// Ce qu'il faut pour résoudre un [[lien]] : titre, nom de fichier et alias de chaque note active.
struct NoteKeys {
    qint64 id = 0;
    QString title;
    QString slug;
    QStringList aliases;
};

/// Accès aux notes (tables notes, note_aliases, attachments) et à leurs liens [[…]] (table links).
/// Suppression logique (deleted_at) : la corbeille garde la note et ses liens.
class NoteRepository {
public:
    explicit NoteRepository(Database& db);

    /// Insère la note. Renseigne id, uuid, createdAt, updatedAt. Le slug doit être libre.
    Result<void> insert(NoteRecord& note);
    /// Met à jour titre, slug, type, texte, front matter, dossier et épinglage.
    Result<void> update(NoteRecord& note);

    Result<NoteRecord> findById(qint64 id);
    Result<NoteRecord> findByUuid(const QString& uuid);
    /// Notes actives, par dossier puis par titre.
    Result<QList<NoteRecord>> list();
    Result<int> count();
    /// Le slug est-il pris par une autre note (corbeille comprise : il reste réservé) ?
    Result<bool> slugTaken(const QString& slug, qint64 exceptId = 0);

    Result<void> remove(qint64 id);
    Result<void> restore(qint64 id);

    Result<void> setAliases(qint64 noteId, const QStringList& aliases);
    Result<QList<NoteKeys>> keys();

    /// Remplace les liens [[…]] sortants de la note.
    Result<void> replaceWikiLinks(qint64 noteId, const QList<NoteLinkRow>& links);
    Result<QList<NoteLinkRow>> outgoingLinks(qint64 noteId);
    /// Liens dont la cible est la note (rétroliens), depuis des notes actives.
    Result<QList<NoteLinkRow>> incomingLinks(qint64 noteId);
    /// Tous les liens [[…]] de notes, pour une nouvelle résolution après création, renommage ou suppression.
    Result<QList<NoteLinkRow>> allWikiLinks();
    Result<void> setLinkTarget(qint64 linkId, qint64 targetNoteId);

    /// Enregistre une pièce jointe (image collée) copiée dans le dossier attachments/.
    Result<void> addAttachment(qint64 noteId, const QString& uuid, const QString& fileName, const QString& relPath,
                               const QString& mime);

private:
    Database& m_db;
};

}  // namespace studylab::db
