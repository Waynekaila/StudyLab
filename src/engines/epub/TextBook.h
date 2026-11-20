#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace studylab {

/// Fichier texte (.txt) ou Markdown (.md, .markdown) mis en forme de livre EPUB 3, en mémoire, pour le lecteur
/// EPUB (foliate-js) : annotations repérées par CFI, recherche par section, sommaire et export comme un EPUB.
///
/// La conversion est déterministe : le même fichier donne toujours les mêmes sections et le même DOM, donc les
/// mêmes CFI. Markdown : titres, paragraphes, emphase, code, citations, listes, tableaux, liens, images locales ;
/// front matter retiré (« title: » donne le titre) ; [[liens]] rendus comme texte. Le livre est découpé aux titres
/// du niveau le plus haut qui se répète ; un texte brut, en parties d'environ 40 000 caractères.
class TextBook {
public:
    /// .txt, .text, .md, .markdown (sans tenir compte de la casse).
    static bool isTextFile(const QString& path);
    static bool isMarkdownFile(const QString& path);
    /// Archive EPUB (ZIP) construite à partir du fichier.
    static Result<QByteArray> build(const QString& path);
    /// Texte du fichier : UTF-8 (BOM retiré) ou, s'il n'est pas valide, Windows-1252 ; fins de ligne « \n ».
    static Result<QString> readText(const QString& path);
    /// Corps XHTML d'un texte Markdown (exposé pour les tests ; images non résolues).
    static QString markdownToXhtml(const QString& markdown);
};

}  // namespace studylab
