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

/// Cible d'un [[lien]] : une note (prioritaire) ou un document de la bibliothèque, sinon rien.
struct NoteLinkTarget {
    enum class Kind { None, Note, Document };
    Kind kind = Kind::None;
    qint64 id = 0;
    bool resolved() const { return kind != Kind::None; }
    bool operator==(const NoteLinkTarget&) const = default;
};

/// Lien [[…]] sortant d'une note : vers une note (noteId), un document (documentId) ou non résolu.
struct NoteLink {
    QString target;
    qint64 noteId = 0;
    qint64 documentId = 0;
    bool resolved() const { return noteId > 0 || documentId > 0; }
};

/// Source d'une note tirée de la lecture, prête à afficher (panneau de contexte, « ouvrir le passage »).
using NoteSource = db::NoteSourceRow;
/// Référence à une source au moment de la création : (« annotation » | « comment » | « document », id).
using NoteSourceRef = QPair<QString, qint64>;

/// Espace de notes : création depuis un modèle, enregistrement (front matter, alias, liens [[…]]),
/// dossiers virtuels, corbeille, rétroliens et liens non résolus, images collées, notes tirées de la lecture.
///
/// Un [[lien]] se résout par titre, nom de fichier ou alias de note, sans tenir compte de la casse, ou à défaut
/// par titre de document. Les liens sont
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

    /// Crée une note tirée de la lecture et enregistre ses sources (provenance), en une transaction.
    Result<NoteRecord> createDerived(const QString& title, NoteType type, const QString& folder, const QString& body,
                                     const QList<NoteSourceRef>& sources);
    Result<QList<NoteSource>> sources(qint64 id);
    /// Notes tirées des documents (export d'une sélection de la bibliothèque).
    Result<QList<NoteRecord>> derivedFrom(const QList<qint64>& documentIds);

    /// Note désignée par un [[lien]], 0 si aucune.
    Result<qint64> resolve(const QString& target);
    /// Note, ou à défaut document, désigné par un [[lien]].
    NoteLinkTarget resolveTarget(const QString& target);
    /// Résolution groupée (aperçu en direct).
    QHash<QString, NoteLinkTarget> resolveMany(const QStringList& targets);
    /// Réévalue les [[liens]] (documents ajoutés, renommés ou retirés de la bibliothèque depuis).
    Result<bool> refreshLinks();
    /// Titres proposés par l'autocomplétion des [[liens]] : notes puis documents, sans doublon.
    QStringList linkCompletions();
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
    QHash<QString, qint64> documentMap();
    static NoteLinkTarget pick(const QString& target, const QHash<QString, qint64>& notes, const QHash<QString, qint64>& docs);
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
