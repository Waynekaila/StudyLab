#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QRectF>
#include <QString>

class QPdfDocument;

namespace studylab::pdf {

/// Annotation lue dans un PDF (créée par un autre logiciel ou par un export StudyLab).
/// Les rectangles sont normalisés (0..1, origine en haut à gauche) dans la zone visible de la page (CropBox,
/// sinon MediaBox), comme les ancres PdfLocator.
struct PdfAnnotation {
    int page = 0;
    AnnotationType type = AnnotationType::Highlight;
    QByteArray subtype;  ///< sous-type PDF d'origine (« Highlight », « Squiggly », « Ink »…)
    QColor color;        ///< /C ; invalide si absente
    QList<QRectF> rects;
    QString contents;    ///< /Contents, réponses (/IRT) ajoutées à la suite
    QString author;      ///< /T
    QString name;        ///< /NM
    QString text;        ///< passage couvert, rempli par extractQuotes()
};

struct PdfAnnotationScan {
    QList<PdfAnnotation> annotations;
    int unsupported = 0;  ///< annotations ignorées : page tournée, type sans équivalent (pièce jointe, son…)
    int approximated = 0; ///< importables mais simplifiées (tracé, tampon, ellipse… réduits à un marqueur)
    bool hasForms = false;  ///< champs de formulaire (dessinés comme des annotations)
    /// Vrai si StudyLab peut afficher toutes les annotations du fichier sans perte une fois importées.
    bool fullyRepresentable() const { return unsupported == 0 && approximated == 0 && !hasForms; }
};

/// Lit les annotations de toutes les pages. Les liens, champs de formulaire et fenêtres contextuelles sont ignorés.
/// Échoue sur un PDF chiffré ou illisible.
Result<PdfAnnotationScan> readPdfAnnotations(const QByteArray& data);

/// Renseigne `text` des annotations de texte (surlignage, soulignement, barré) à partir du texte des pages.
void extractQuotes(QPdfDocument& document, QList<PdfAnnotation>& annotations);

/// Annotation à écrire dans une copie annotée.
struct PdfAnnotationOut {
    int page = 0;
    AnnotationType type = AnnotationType::Highlight;  ///< Bookmark n'est pas exporté
    QColor fill;    ///< surlignage, intérieur d'un marqueur
    QColor stroke;  ///< traits, contour, icône de note
    QList<QRectF> rects;  ///< normalisés comme PdfAnnotation::rects ; une note : un point (centre du premier)
    QString contents;
    QString id;     ///< identifiant stable (/NM)
    QDateTime modified;
};

struct PdfWriteReport {
    QByteArray data;
    int written = 0;
    int skipped = 0;  ///< page absente ou tournée
    QList<int> skippedIndexes;  ///< positions dans la liste reçue des annotations non écrites
};

/// Copie du PDF avec les annotations ajoutées par une mise à jour incrémentale : les octets d'origine sont conservés
/// tels quels et suivis des nouveaux objets (annotations avec leur apparence, pages révisées) et d'une table des
/// références (classique, ou flux si le fichier d'origine en utilise un). Refuse un PDF chiffré ou abîmé.
Result<PdfWriteReport> writeAnnotatedCopy(const QByteArray& original, const QList<PdfAnnotationOut>& annotations);

/// Vrai si deux annotations désignent la même chose : même page, même type et mêmes zones (à ~0,3 % près).
bool sameAnnotationGeometry(int pageA, AnnotationType typeA, const QList<QRectF>& rectsA, int pageB,
                            AnnotationType typeB, const QList<QRectF>& rectsB);

}  // namespace studylab::pdf
