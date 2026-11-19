#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace studylab {

/// Métadonnées lues dans un fichier (PDF Info/XMP, OPF EPUB), avant enregistrement en base.
struct DocumentMetadata {
    QString title;
    QString subtitle;
    QStringList authors;
    QString publisher;
    int year = 0;
    QString language;
    QString description;
    QString isbn;
    QString doi;
    QStringList keywords;
    int pageCount = 0;           ///< PDF : pages ; EPUB : éléments du spine
    bool passwordProtected = false;
    QImage cover;                ///< nulle si aucune couverture
};

/// Largeur des couvertures enregistrées (px) : nette en grille sur écran HiDPI.
inline constexpr int kCoverWidth = 360;

/// ISBN-10/13 normalisé (chiffres et X) si le texte en contient un valide, sinon vide.
QString extractIsbn(const QString& text);
/// DOI (« 10.xxxx/… ») si le texte en contient un, sinon vide.
QString extractDoi(const QString& text);
/// Titre lisible depuis un nom de fichier (« cengel_thermo-2019.pdf » → « cengel thermo 2019 »).
QString titleFromFileName(const QString& path);

}  // namespace studylab
