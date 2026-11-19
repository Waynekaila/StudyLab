#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "core/domain/Note.h"
#include "services/export/ChapterIndex.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace studylab {

class AnnotationService;
class CommentService;
class LibraryService;

/// Ce qu'il faut pour exporter un document, lu en base sur le thread principal.
struct DocumentExportData {
    DocumentRecord doc;
    QString file;  ///< chemin du fichier (sommaire) ; peut ne plus exister
    QList<AnnotationRecord> annotations;
    QList<CommentRecord> comments;
};

struct LibraryExportResult {
    QString folder;           ///< « …/StudyLab Export 2026-11-19 »
    int documents = 0;
    int annotations = 0;      ///< hors signets
    int comments = 0;
    int notes = 0;
    int attachments = 0;      ///< images des notes copiées dans _attachments/
    QStringList errors;       ///< un message par document en échec
    bool cancelled = false;
};

/// Export de plusieurs documents (sélection ou bibliothèque entière) en coffre Obsidian
/// (06-arborescence, « Structure d'exportation ») :
///   StudyLab Export <date>/
///     README.md            mode d'emploi et index des documents
///     annotations.csv      toutes les annotations, une ligne chacune
///     Bibliothèque/<Auteur> - <Titre> (<année>)/…   comme l'export d'un document
///     Notes/<dossier>/<nom>.md                        notes (front matter, [[liens]] et #tags conservés)
///     _attachments/                                   images des notes
class LibraryExporter {
public:
    struct Options {
        QDate date = QDate::currentDate();
        QString appVersion;
        bool appLinks = true;
        /// Notes à joindre : toutes pour la bibliothèque entière, celles tirées des documents pour une sélection.
        QList<NoteRecord> notes;
        QString attachmentsDir;  ///< dossier des images collées dans les notes
    };
    /// Appelée après chaque document ; renvoyer false annule l'export.
    using Progress = std::function<bool(int done, int total)>;

    /// Lit les documents, annotations et commentaires (thread principal).
    static Result<QList<DocumentExportData>> collect(LibraryService& library, AnnotationService& annotations,
                                                     CommentService& comments, const QList<qint64>& documentIds);

    static QString folderName(const QDate& date);
    /// Noms de dossier uniques : un second « Dupont - Cours (2020) » devient « Dupont - Cours (2020) (2) ».
    static QStringList uniqueBaseNames(const QList<DocumentExportData>& docs);
    static QString csvField(const QString& value);
    static QString csvHeader();
    static QStringList csvRows(const DocumentExportData& data, const ChapterIndex& chapters);
    static QString readme(const QList<DocumentExportData>& docs, const QStringList& baseNames, const Options& options,
                          int notes = 0);
    /// Chemin de la note dans l'export, relatif au dossier racine : « Notes/Thermo/Cours/Cycle de Carnot.md ».
    static QString notePath(const NoteRecord& note);
    /// Texte exporté : front matter complété (type, uuid), images redirigées vers ../_attachments/.
    static QString noteMarkdown(const NoteRecord& note);
    /// Images « attachments/… » citées par la note.
    static QStringList noteAttachments(const QString& markdown);

    /// Écrit l'export dans parentDir ; utilisable depuis un thread de travail (aucun accès à la base).
    /// Un document en échec n'arrête pas l'export : il est listé dans errors.
    static Result<LibraryExportResult> write(const QString& parentDir, const QList<DocumentExportData>& docs,
                                             const Options& options, const Progress& progress = {});
};

}  // namespace studylab
