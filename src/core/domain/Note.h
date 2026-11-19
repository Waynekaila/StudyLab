#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

/// Types de notes (colonne notes.note_type, 04-base-de-donnees). Chacun a son icône et son modèle.
enum class NoteType { Reading, Course, Concept, Formula, Revision, Research, Personal, Report };

QString noteTypeKey(NoteType t);  ///< "reading", "course"…
std::optional<NoteType> noteTypeFromKey(const QString& key);
QString noteTypeLabel(NoteType t);  ///< « Note de lecture », « Fiche de cours »…
QString noteTypeIcon(NoteType t);   ///< nom d'icône du thème
QList<NoteType> allNoteTypes();
/// Contenu initial d'une note de ce type (front matter et squelette de sections).
QString noteTemplate(NoteType t, const QString& title);

/// Note telle que stockée. body contient le texte Markdown complet, front matter YAML compris :
/// c'est ce que l'utilisateur écrit et ce qui sera exporté tel quel.
struct NoteRecord {
    qint64 id = 0;
    QString uuid;
    QString title;
    QString slug;                 ///< nom de fichier à l'export, unique
    NoteType type = NoteType::Personal;
    QString body;
    QJsonObject frontMatter;      ///< front matter lu dans body (colonne frontmatter, dérivée)
    QString folder;               ///< dossier virtuel « Thermodynamique/Cours », vide = racine
    bool pinned = false;
    QString createdAt;
    QString updatedAt;
};

/// Front matter YAML d'une note : sous-ensemble lu par Obsidian (clé: valeur, listes [a, b] ou « - a »).
struct FrontMatter {
    QJsonObject values;
    int bodyStart = 0;    ///< position du texte après le bloc « --- … --- » (0 s'il n'y en a pas)
    int lineCount = 0;    ///< lignes occupées par le bloc
    bool present = false;
};
FrontMatter parseFrontMatter(const QString& markdown);
/// Texte sans le bloc de front matter.
QString stripFrontMatter(const QString& markdown);

/// Tags d'une note : clé « tags » (ou « tag ») du front matter et #tags du texte, sans « # », sans doublon.
QStringList noteTags(const QString& markdown);
/// Alias de la note : clé « aliases » (ou « alias ») du front matter.
QStringList noteAliases(const QString& markdown);

/// Titre du plan d'une note.
struct OutlineEntry {
    int level = 1;
    QString text;
    int line = 0;   ///< ligne (0) dans le texte complet
};
/// Titres « # … » hors front matter et blocs de code.
QList<OutlineEntry> noteOutline(const QString& markdown);

/// Nom de fichier sûr sur Windows et Linux, accents conservés : « Cycle de Carnot ».
QString noteSlug(const QString& title);
/// Clé de résolution d'un [[lien]] : sans « #section », « ^bloc », extension .md ni casse.
QString wikiLinkKey(const QString& target);

/// Extrait de texte autour de la première occurrence d'un [[lien]] vers l'une des clés (rétroliens).
QString wikiLinkSnippet(const QString& markdown, const QStringList& keys, int radius = 60);

}  // namespace studylab

#include <QMetaType>
Q_DECLARE_METATYPE(studylab::NoteRecord)
