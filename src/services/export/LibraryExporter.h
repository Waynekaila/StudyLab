#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"
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
    QStringList errors;       ///< un message par document en échec
    bool cancelled = false;
};

/// Export de plusieurs documents (sélection ou bibliothèque entière) en coffre Obsidian
/// (06-arborescence, « Structure d'exportation ») :
///   StudyLab Export <date>/
///     README.md            mode d'emploi et index des documents
///     annotations.csv      toutes les annotations, une ligne chacune
///     Bibliothèque/<Auteur> - <Titre> (<année>)/…   comme l'export d'un document
class LibraryExporter {
public:
    struct Options {
        QDate date = QDate::currentDate();
        QString appVersion;
        bool appLinks = true;
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
    static QString readme(const QList<DocumentExportData>& docs, const QStringList& baseNames, const Options& options);

    /// Écrit l'export dans parentDir ; utilisable depuis un thread de travail (aucun accès à la base).
    /// Un document en échec n'arrête pas l'export : il est listé dans errors.
    static Result<LibraryExportResult> write(const QString& parentDir, const QList<DocumentExportData>& docs,
                                             const Options& options, const Progress& progress = {});
};

}  // namespace studylab
