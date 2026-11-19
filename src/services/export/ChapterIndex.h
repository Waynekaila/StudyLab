#pragma once

#include "core/domain/Document.h"

#include <QList>
#include <QString>

#include <utility>

namespace studylab {

/// Titres de chapitre d'un document, lus dans son fichier sans l'afficher (sommaire PDF, table des matières EPUB).
/// Sert aux exports hors du lecteur : même règle que les volets (deux premiers niveaux du sommaire).
/// Utilisable depuis un thread de travail.
class ChapterIndex {
public:
    ChapterIndex() = default;
    /// Entrées (unité, titre) : page (index 0) pour un PDF, section du spine pour un EPUB.
    explicit ChapterIndex(QList<std::pair<int, QString>> entries);

    static ChapterIndex forPdf(const QString& path);
    static ChapterIndex forEpub(const QString& path);
    static ChapterIndex forDocument(const QString& path, DocumentFormat format);

    /// Titre de l'entrée la plus proche avant ou sur l'unité ; vide si aucune.
    QString chapterOf(int unit) const;
    bool isEmpty() const { return m_entries.isEmpty(); }
    const QList<std::pair<int, QString>>& entries() const { return m_entries; }

private:
    QList<std::pair<int, QString>> m_entries;  ///< triées par unité
};

}  // namespace studylab
