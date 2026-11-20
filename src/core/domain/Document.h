#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

enum class ReadingStatus { ToRead, Reading, Paused, Finished, ToReread, Archived };
enum class DocumentFormat { Pdf, Epub, Txt, Markdown, Docx, Image };
enum class StorageMode { Managed, Linked };

QString readingStatusKey(ReadingStatus s);            ///< Valeur stockée : "to_read", "reading"…
std::optional<ReadingStatus> readingStatusFromKey(const QString& key);
QString readingStatusLabel(ReadingStatus s);          ///< Libellé affiché : « À lire », « En cours »…
QList<ReadingStatus> allReadingStatuses();

QString documentFormatKey(DocumentFormat f);          ///< "pdf", "epub", "txt", "md", "docx", "image"
std::optional<DocumentFormat> documentFormatFromKey(const QString& key);
QString documentFormatLabel(DocumentFormat f);        ///< "PDF", "EPUB", "TXT", "Markdown", "DOCX", "Image"
QList<DocumentFormat> allDocumentFormats();
/// Document à pages fixes (PDF, image) : lu par le lecteur PDF, repéré par page et rectangles. Une image est
/// affichée à travers un PDF d'une page par image, construit à la demande (LibraryService::readableFile).
inline bool isPaged(DocumentFormat f) { return f == DocumentFormat::Pdf || f == DocumentFormat::Image; }
/// Texte recomposable (EPUB, TXT, Markdown, DOCX) : lu par le lecteur EPUB, repéré par section et CFI.
inline bool isReflowable(DocumentFormat f) { return !isPaged(f); }

/// Document de la bibliothèque, tel que stocké dans la table « documents » (+ auteurs et tags).
struct DocumentRecord {
    qint64 id = 0;
    QString uuid;
    QString title;
    QString sortTitle;
    QString subtitle;
    QString publisher;
    int year = 0;  ///< 0 = inconnue
    QString isbn;
    QString doi;
    QString language;
    QString description;
    DocumentFormat format = DocumentFormat::Pdf;
    StorageMode storageMode = StorageMode::Managed;
    QString filePath;  ///< relatif au dossier library/ si Managed, absolu si Linked
    QString fileHash;  ///< SHA-256 hexadécimal
    qint64 fileSize = 0;
    int pageCount = 0;
    QString coverPath;  ///< relatif au dossier covers/, vide si aucune couverture
    ReadingStatus status = ReadingStatus::ToRead;
    bool favorite = false;
    int rating = 0;
    double progress = 0.0;  ///< 0..1, depuis reading_progress
    QString addedAt;
    QString updatedAt;
    QString lastOpenedAt;
    QStringList authors;
    QStringList tags;
};

/// Clé de tri d'un titre : minuscules, sans accents ni article initial (« L'Entropie » → « entropie »).
QString makeSortTitle(const QString& title);
/// Clé de comparaison : minuscules, sans accents (« Çengel » → « cengel »).
QString foldForSort(const QString& text);
/// Nom de tri d'un auteur : « Yunus A. Çengel » → « Çengel, Yunus A. » ; « Çengel, Y. » inchangé.
QString makeAuthorSortName(const QString& name);

}  // namespace studylab

#include <QMetaType>
Q_DECLARE_METATYPE(const studylab::DocumentRecord*)
