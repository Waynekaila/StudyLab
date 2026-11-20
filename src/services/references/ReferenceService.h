#pragma once

#include "core/Result.h"
#include "core/domain/Document.h"
#include "core/domain/Reference.h"

#include <QHash>
#include <QList>
#include <QStringList>

namespace studylab {

class NoteService;
namespace db {
class Database;
}

/// Bilan d'un import de références.
struct ReferenceImportReport {
    int added = 0;
    int duplicates = 0;          ///< déjà présentes (même DOI, ISBN ou clé et titre) : ignorées
    int linked = 0;              ///< rattachées à un document de la bibliothèque (DOI, ISBN ou titre)
    int renamed = 0;             ///< clé déjà prise par une autre référence : suffixe ajouté
    QStringList warnings;
    QList<qint64> addedIds;
};

/// Références bibliographiques : saisie, import BibTeX / CSL-JSON, export, citations [[@clé]] dans les notes.
/// Changer la clé d'une référence met à jour les citations des notes qui l'emploient.
class ReferenceService {
public:
    ReferenceService(db::Database& db, NoteService& notes);

    /// Triées par premier auteur, puis année.
    Result<QList<Reference>> list();
    Result<Reference> get(qint64 id);
    Result<Reference> byCitekey(const QString& citekey);
    Result<QList<Reference>> forDocument(qint64 documentId);

    /// Clé générée si vide ; rendue unique si elle est prise.
    Result<Reference> create(Reference ref);
    /// Enregistre la référence ; une clé modifiée est réécrite dans les notes. `rewrittenNotes` : notes mises à jour.
    Result<Reference> update(const Reference& ref, int* rewrittenNotes = nullptr);
    Result<void> remove(qint64 id);

    /// Brouillon tiré des métadonnées d'un document (titre, auteurs, année, éditeur, ISBN, DOI, langue).
    static Reference draftFromDocument(const DocumentRecord& doc);
    /// Référence du document : l'existante, sinon une nouvelle tirée de ses métadonnées.
    Result<Reference> ensureForDocument(qint64 documentId);

    /// Fichier .bib (BibTeX/BibLaTeX) ou .json (CSL-JSON).
    Result<ReferenceImportReport> importFile(const QString& path);
    Result<ReferenceImportReport> importReferences(const QList<Reference>& refs, const QStringList& warnings = {});
    /// Exporte en BibTeX (.bib) ou CSL-JSON (.json) selon l'extension ; `ids` vide : toutes.
    Result<int> exportFile(const QString& path, const QList<qint64>& ids = {});
    Result<QString> bibtex(const QList<qint64>& ids = {});

    /// Références des clés citées (clé en minuscules → référence) ; les clés inconnues sont absentes.
    QHash<QString, Reference> resolveKeys(const QStringList& keys);
    /// « @clé » pour l'autocomplétion après « [[ ».
    QStringList completions();
    /// Notes qui citent cette clé (titre et identifiant).
    Result<QList<QPair<qint64, QString>>> citingNotes(const QString& citekey);

private:
    Result<QSet<QString>> takenKeys(qint64 exceptId = 0);
    Result<QList<Reference>> selected(const QList<qint64>& ids);

    db::Database& m_db;
    NoteService& m_notes;
};

}  // namespace studylab
