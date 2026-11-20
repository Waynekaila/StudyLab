#pragma once

#include <QList>
#include <QRectF>
#include <QString>

namespace studylab {

/// Mot reconnu par l'OCR : texte et boîte normalisée (0..1) dans la page non tournée.
struct OcrWord {
    enum Break { None = 0, Line = 1, Paragraph = 2 };
    QString text;
    QRectF box;
    int breakBefore = None;  ///< début de ligne ou de paragraphe (sauf le premier mot)
};

/// Texte reconnu d'une page : les mots dans l'ordre de lecture.
struct OcrPage {
    QList<OcrWord> words;

    bool isEmpty() const { return words.isEmpty(); }
    /// Texte de la page : mots séparés par une espace, retour à la ligne entre les lignes, ligne vide entre les
    /// paragraphes. Les positions des mots dans ce texte sont données par offsets().
    QString text() const;
    /// Position du premier caractère de chaque mot dans text().
    QList<int> offsets() const;

    /// Format compact de la colonne page_text.words_json : [[texte, x, y, l, h, saut], …], 4 décimales.
    QString toJson() const;
    static OcrPage fromJson(const QString& json);
};

}  // namespace studylab
