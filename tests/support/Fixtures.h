#pragma once

#include <QString>
#include <QStringList>

namespace studylab::test {

struct PdfSpec {
    QString title;
    int pages = 2;
    QString bodyText = QStringLiteral("Le second principe de la thermodynamique.");
    /// Page numérisée : le texte est dessiné dans une image (aucun texte dans le PDF), pour l'OCR.
    bool scanned = false;
};

/// Génère un PDF réel (QPdfWriter) avec un titre dans le dictionnaire Info.
bool writePdf(const QString& path, const PdfSpec& spec);

struct EpubSpec {
    QString title;
    QStringList authors;
    QString language = QStringLiteral("fr");
    QString isbn;
    QString date = QStringLiteral("2019-05-01");
    QStringList subjects;
    QStringList chapters = {QStringLiteral("<p>Chapitre un.</p>"), QStringLiteral("<p>Chapitre deux.</p>")};
    bool epub3 = true;
    bool withCover = true;
    bool drm = false;
    /// Titres des chapitres pour la table des matières (nav EPUB 3 ou NCX EPUB 2) ; vide : pas de sommaire.
    QStringList tocTitles;
};

/// Génère un EPUB 2 ou 3 minimal mais conforme (mimetype non compressé en premier).
bool writeEpub(const QString& path, const EpubSpec& spec);

/// Génère un document Word (.docx) d'exemple : titre, titres de niveaux 1 et 2, mise en forme, lien, note de bas de
/// page, listes imbriquées, tableau (cellules fusionnées), image PNG, citation, texte supprimé et inséré (suivi des
/// modifications), propriétés (titre « Physique des fluides », deux auteurs, langue fr). `bodyText` est repris dans
/// le premier paragraphe du chapitre « Statique ».
bool writeDocx(const QString& path, const QString& bodyText = QStringLiteral("La pression est une force par unité de surface."));

/// Génère une image (PNG, JPEG… selon l'extension) : fond clair, texte noir en gros caractères, `dpi` enregistré
/// dans le fichier.
bool writeImage(const QString& path, const QString& text, int width = 1200, int height = 900, int dpi = 150);

}  // namespace studylab::test
