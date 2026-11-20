#pragma once

#include "core/Result.h"
#include "core/domain/Note.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace studylab {

/// Note lue dans un coffre, prête à importer. Les images locales sont remplacées dans `body` par un jeton,
/// associé au fichier à copier dans attachments/.
struct NoteImport {
    QString sourcePath;          ///< chemin relatif au coffre (« Thermo/Cours/Entropie.md »)
    QString title;               ///< nom du fichier, comme dans Obsidian
    QString folder;              ///< dossier virtuel (préfixe + dossiers du coffre)
    NoteType type = NoteType::Personal;
    QString uuid;                ///< « uuid: » du front matter (export StudyLab), sinon vide
    QString body;
    QList<QPair<QString, QString>> images;  ///< (jeton, fichier image absolu)
};

/// Contenu d'un coffre Obsidian ou d'un dossier de fichiers Markdown.
struct VaultScan {
    QList<NoteImport> notes;
    QStringList bibFiles;        ///< fichiers .bib trouvés (références à importer)
    QStringList warnings;
    int missingImages = 0;
    /// Dossier exporté par StudyLab (README.md, Bibliothèque/, Notes/) : seules les notes de Notes/ sont lues,
    /// les fiches de documents de Bibliothèque/ sont des exports, pas des notes.
    bool studylabExport = false;
};

/// Lecture d'un coffre (06-arborescence, services/io « MarkdownImporter ») : un fichier .md par note, titre = nom
/// du fichier, dossiers du coffre → dossiers virtuels, front matter conservé (type: et uuid: reconnus).
/// Images : ![[image.png]], ![[image.png|300]] et ![texte](chemin) relatifs, cherchés dans le coffre.
/// Dossiers cachés (.obsidian, .trash…) ignorés. Les lignes « type: » (clé StudyLab) et « uuid: » ajoutées par l'export
/// sont retirées du front matter : elles sont rendues dans NoteImport. Aucun accès à la base.
class VaultImporter {
public:
    static Result<VaultScan> scan(const QString& root, const QString& folderPrefix = {});
    /// Jeton qui remplace la i-ème image d'une note.
    static QString imageToken(int index);
};

}  // namespace studylab
