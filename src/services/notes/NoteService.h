#pragma once

#include "core/Result.h"
#include "core/domain/Note.h"
#include "data/repositories/NoteRepository.h"

#include <QHash>
#include <QObject>

namespace studylab {

namespace db {
class Database;
}

/// Note qui cite la note courante, avec l'extrait autour du lien.
struct NoteBacklink {
    qint64 noteId = 0;
    QString title;
    NoteType type = NoteType::Personal;
    QString snippet;
};

/// Lien [[…]] sortant d'une note : résolu (noteId > 0) ou non.
struct NoteLink {
    QString target;
    qint64 noteId = 0;
};

/// Espace de notes : création depuis un modèle, enregistrement (front matter, alias, liens [[…]]),
/// dossiers virtuels, corbeille, rétroliens et liens non résolus, images collées.
///
/// Un [[lien]] se résout par titre, nom de fichier ou alias, sans tenir compte de la casse. Les liens sont
/// réévalués à chaque création, renommage, suppression ou restauration : un lien vers une note pas encore
/// écrite se résout dès qu'elle existe. Renommer une note met à jour les [[liens]] qui la citent.
class NoteService : public QObject {
    Q_OBJECT
public:
    NoteService(db::Database& db, QString attachmentsDir, QObject* parent = nullptr);

    /// Crée une note (texte = modèle du type si body est vide). Le titre est rendu unique au besoin.
    Result<NoteRecord> create(const QString& title, NoteType type, const QString& folder = {}, const QString& body = {});
    /// Enregistre le titre et le texte. Renommer met à jour les [[liens]] des autres notes.
    Result<NoteRecord> save(qint64 id, const QString& title, const QString& body);
    Result<NoteRecord> setType(qint64 id, NoteType type);
    Result<NoteRecord> setFolder(qint64 id, const QString& folder);
    Result<NoteRecord> setPinned(qint64 id, bool pinned);
    /// Renomme ou déplace un dossier virtuel et ses sous-dossiers. Retourne le nombre de notes déplacées.
    Result<int> renameFolder(const QString& from, const QString& to);
    /// Mise à la corbeille ; restore() l'annule.
    Result<void> remove(qint64 id);
    Result<NoteRecord> restore(qint64 id);

    Result<NoteRecord> find(qint64 id);
    Result<QList<NoteRecord>> list();
    Result<int> count();

    /// Note désignée par un [[lien]], 0 si aucune.
    Result<qint64> resolve(const QString& target);
    /// Résolution groupée (aperçu en direct) : cible → note, 0 si aucune.
    QHash<QString, qint64> resolveMany(const QStringList& targets);
    Result<QList<NoteBacklink>> backlinks(qint64 id);
    Result<QList<NoteLink>> links(qint64 id);

    /// Copie une image collée dans attachments/ et retourne son chemin relatif (« attachments/… .png »).
    Result<QString> addImage(qint64 noteId, const QByteArray& data, const QString& extension);
    QString attachmentsDir() const { return m_attachmentsDir; }

    /// Dossier normalisé : « A / B/ » → « A/B ».
    static QString normalizeFolder(const QString& folder);

signals:
    void noteAdded(const studylab::NoteRecord& note);
    void noteChanged(const studylab::NoteRecord& note);
    void noteRemoved(qint64 id);
    /// Rétroliens ou liens non résolus modifiés (création, renommage, liens ajoutés…).
    void linksChanged();

private:
    Result<QString> uniqueSlug(const QString& title, qint64 exceptId);
    QHash<QString, qint64> keyMap();
    /// Liens [[…]] et alias de la note d'après son texte.
    Result<void> indexNote(const NoteRecord& note);
    /// Réévalue la cible de tous les liens. Retourne true si l'une a changé.
    Result<bool> relinkAll();
    Result<NoteRecord> modify(qint64 id, const std::function<void(NoteRecord&)>& change);

    db::Database& m_db;
    db::NoteRepository m_repo;
    QString m_attachmentsDir;
};

}  // namespace studylab
