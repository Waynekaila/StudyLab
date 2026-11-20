#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace studylab::book {

/// Section du livre : un fichier XHTML du spine.
struct Section {
    QString html;   ///< contenu du <body>
    QString title;
};

/// Entrée du sommaire (document nav).
struct TocItem {
    QString label;
    QString href;   ///< « s2.xhtml#t3 »
    int depth = 0;
};

/// Image embarquée (chemin dans le dossier OEBPS, ex. « images/i1.png »).
struct Image {
    QString name;
    QByteArray data;
};

/// Livre EPUB 3 construit en mémoire à partir d'un autre format (texte, Markdown, DOCX).
struct Spec {
    QString title;
    QStringList authors;
    QString lang;
    QByteArray identifierSeed;  ///< contenu dont l'empreinte sert d'identifiant stable
    QList<Section> sections;
    QList<TocItem> toc;
    QList<Image> images;
    bool plainText = false;     ///< classe « txt » sur <body> (paragraphes de texte brut)
};

/// Échappement XML du texte et des attributs.
QString esc(QString s);
/// Retire les caractères interdits en XML 1.0.
QString xmlSafe(QString s);
/// Type MIME d'une image d'après son extension (png, jpg, gif, svg, webp…).
QString imageMime(const QString& name);

/// Archive EPUB 3 (mimetype non compressé en premier, nav, OPF). Le même `Spec` donne toujours les mêmes octets.
Result<QByteArray> write(const Spec& spec);

}  // namespace studylab::book
