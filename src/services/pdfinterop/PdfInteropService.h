#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "engines/pdf/PdfAnnotationIO.h"

#include <QColor>
#include <QCoreApplication>
#include <QString>

#include <array>

namespace studylab {

class AnnotationService;
class CommentService;
class LibraryService;
struct DocumentRecord;

/// Couleurs des annotations écrites dans un PDF (fond d'un surlignage ou d'un marqueur, traits et icônes),
/// indexées par AnnotationColor. Fournies par l'interface, qui détient la palette.
struct PdfColorScheme {
    std::array<QColor, 5> fill;
    std::array<QColor, 5> stroke;
};

/// Annotations d'un PDF qui n'existent pas encore dans StudyLab.
struct PdfImportPreview {
    int fresh = 0;        ///< à importer
    int duplicates = 0;   ///< déjà présentes (même page, même type, mêmes zones)
    int unsupported = 0;  ///< sans équivalent (page tournée, pièce jointe…)
};

struct PdfImportReport {
    int imported = 0;
    int duplicates = 0;
    int unsupported = 0;
    /// Toutes les annotations du fichier sont maintenant dans StudyLab, sans perte : le dessin de celles du
    /// fichier peut être désactivé pour ne pas les voir en double.
    bool fileFullyImported = false;
};

struct PdfExportReport {
    int annotations = 0;     ///< annotations écrites
    int comments = 0;        ///< commentaires de page ou de passage écrits comme notes
    int alreadyPresent = 0;  ///< déjà dans le PDF d'origine : non dupliquées
    int skipped = 0;         ///< page tournée ou absente
};

/// Interopérabilité PDF (V2-5) : import des annotations créées dans un autre logiciel, export d'une copie annotée
/// lisible par tout lecteur PDF. Le fichier de la bibliothèque n'est jamais modifié.
class PdfInteropService {
    Q_DECLARE_TR_FUNCTIONS(PdfInteropService)
public:
    PdfInteropService(LibraryService& library, AnnotationService& annotations, CommentService& comments);

    /// Lecture seule du fichier, sans base de données : peut s'exécuter hors du fil principal.
    static Result<pdf::PdfAnnotationScan> readFile(const QString& path);

    /// Ce qu'un import ajouterait (comparaison avec les annotations existantes du document).
    Result<PdfImportPreview> preview(qint64 documentId, const pdf::PdfAnnotationScan& scan);
    Result<PdfImportPreview> preview(qint64 documentId);

    /// Importe les annotations absentes du document, avec le texte couvert et la couleur la plus proche.
    Result<PdfImportReport> importAnnotations(qint64 documentId, const PdfColorScheme& colors);

    /// Écrit une copie du PDF contenant les annotations du document (et ses commentaires de page ou de passage
    /// si `withComments`). `target` doit être différent du fichier de la bibliothèque.
    Result<PdfExportReport> exportAnnotatedCopy(qint64 documentId, const QString& target, const PdfColorScheme& colors,
                                                bool withComments = true);

    /// Couleur StudyLab la plus proche (teinte d'abord, puis distance dans l'espace RVB).
    static AnnotationColor nearestColor(const QColor& color, const PdfColorScheme& scheme);
    /// « Titre (annoté).pdf », sans caractère interdit dans un nom de fichier.
    static QString suggestedFileName(const DocumentRecord& document);

private:
    Result<QString> pdfPath(qint64 documentId);

    LibraryService& m_library;
    AnnotationService& m_annotations;
    CommentService& m_comments;
};

}  // namespace studylab
