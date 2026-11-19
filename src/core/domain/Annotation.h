#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>

namespace studylab {

/// Types d'annotation (colonne annotations.type).
/// « Marqueur » = zone rectangulaire (figure, formule, tableau) ; « Note » = note épinglée en un point.
enum class AnnotationType { Highlight, Underline, Strikeout, Note, Marker, Bookmark };
enum class AnnotationColor { Yellow, Green, Blue, Pink, Purple };

QString annotationTypeKey(AnnotationType t);  ///< "highlight", "underline"…
std::optional<AnnotationType> annotationTypeFromKey(const QString& key);
QString annotationTypeLabel(AnnotationType t);  ///< « Surlignage », « Soulignement »…
QList<AnnotationType> allAnnotationTypes();

QString annotationColorKey(AnnotationColor c);  ///< "yellow", "green"…
std::optional<AnnotationColor> annotationColorFromKey(const QString& key);
QString annotationColorLabel(AnnotationColor c);  ///< « Jaune », « Vert »…
QList<AnnotationColor> allAnnotationColors();

/// Ancre d'une annotation PDF (02-architecture §2.6) : sélecteurs redondants.
/// Les rectangles sont normalisés (0..1) dans l'espace de la page non tournée : ils ne dépendent
/// ni du zoom, ni de la taille de la fenêtre, ni de la rotation d'affichage.
struct PdfLocator {
    int page = 0;                 ///< index 0
    QList<QRectF> rects;          ///< zones couvertes (lignes du passage, zone d'un marqueur, point d'une note)
    QString exact;                ///< texte cité
    QString prefix;               ///< contexte avant (≤ 32 caractères)
    QString suffix;               ///< contexte après (≤ 32 caractères)
    int start = -1;               ///< position du passage dans le texte de la page (-1 : inconnue)
    int end = -1;

    QJsonObject toJson() const;
    static std::optional<PdfLocator> fromJson(const QJsonObject& o);
    /// Boîte englobante normalisée de toutes les zones.
    QRectF bounds() const;
    /// Position verticale relative (0..1) du début de l'ancre dans la page, pour l'ordre de lecture.
    double relativeY() const;
};

/// Ancre d'une annotation EPUB (02-architecture §2.6) : CFI de plage (standard EPUB), citation avec
/// contexte et position dans la section, pour l'ordre de lecture et un réancrage de secours.
struct EpubLocator {
    QString cfi;          ///< « epubcfi(/6/4!/4/2,/1:0,/1:24) » ; plage réduite à un point pour un signet
    int section = 0;      ///< index dans le spine
    double fraction = 0;  ///< position du début dans la section (0..1)
    double progress = 0;  ///< position dans le livre (0..1)
    QString exact;
    QString prefix;
    QString suffix;

    QJsonObject toJson() const;
    static std::optional<EpubLocator> fromJson(const QJsonObject& o);
};

/// Annotation telle que stockée (table annotations + commentaire lié servant de note).
struct AnnotationRecord {
    qint64 id = 0;
    QString uuid;
    qint64 documentId = 0;
    AnnotationType type = AnnotationType::Highlight;
    AnnotationColor color = AnnotationColor::Yellow;
    int unitIndex = 0;          ///< page (PDF) ou chapitre (EPUB)
    QString pageLabel;          ///< numéro imprimé si connu
    QJsonObject locator;
    double sortKey = 0.0;       ///< unitIndex + position relative
    QString selectedText;
    QString title;
    QString anchorStatus = QStringLiteral("ok");
    QString docHash;
    QString createdAt;
    QString updatedAt;
    QString note;               ///< texte de la note associée (commentaire lié), vide si aucune
};

/// Clé d'ordre de lecture : page + position relative dans la page.
double annotationSortKey(int unitIndex, double relativeY);

/// Emplacement lisible : « p. 389 » (PDF) ou « 42 % » (EPUB).
QString annotationLocation(const AnnotationRecord& r);
/// Titre de groupe quand le document n'a pas de sommaire : « Page 389 » ou « Section 3 ».
QString annotationGroupFallback(const AnnotationRecord& r);

/// Texte d'une annotation normalisé pour l'affichage et l'export (espaces et coupures de ligne fusionnés,
/// césures « -\n » recollées).
QString normalizeQuote(const QString& text);

}  // namespace studylab

#include <QMetaType>
Q_DECLARE_METATYPE(studylab::AnnotationRecord)
