#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>

namespace studylab {

/// Export Markdown compatible Obsidian (06-arborescence, « Structure d'exportation »).
/// Fonctions pures + écriture d'un dossier par document :
///   <Auteur> - <Titre> (<année>)/
///     <Auteur> - <Titre> (<année>).md   fiche document (YAML)
///     Annotations.md                     groupées par chapitre, citations en callouts
///     Commentaires.md                    commentaires libres puis par chapitre (s'il y en a)
///     metadata.json                      métadonnées et annotations, lisibles par une machine
class MarkdownExporter {
public:
    /// Titre de chapitre d'une page (vide : regroupement par page).
    using ChapterResolver = std::function<QString(int page)>;

    struct Options {
        QDate date = QDate::currentDate();
        ChapterResolver chapterOf;
        QList<CommentRecord> comments;  ///< commentaires du document (Commentaires.md)
        QString baseName;               ///< nom du dossier et de la fiche ; vide : documentBaseName()
        bool appLinks = false;          ///< liens « studylab:// » vers le passage (gestionnaire installé)
    };

    /// Nom de base effectif (Options::baseName ou documentBaseName()).
    static QString baseName(const DocumentRecord& doc, const Options& options);
    /// « studylab://open?doc=<uuid> » ; avec annotation ou commentaire : « …&annotation=<uuid> ».
    static QString appLink(const QString& documentUuid, const QString& itemKey = {}, const QString& itemUuid = {});

    /// « Çengel - Thermodynamique (2019) », sans caractères interdits dans un nom de fichier.
    static QString documentBaseName(const DocumentRecord& doc);
    /// Nom de fichier sûr sous Windows et Linux (caractères réservés remplacés, longueur bornée).
    static QString safeFileName(const QString& name);
    /// Chaîne YAML entre guillemets, échappée.
    static QString yamlString(const QString& s);
    /// Référence de page affichée : numéro imprimé s'il est connu, sinon numéro physique.
    static QString pageRef(const AnnotationRecord& a);

    static QString documentNote(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                const Options& options);
    static QString annotationsNote(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                   const Options& options);
    static QString commentsNote(const DocumentRecord& doc, const QList<CommentRecord>& comments, const Options& options);
    static QJsonObject metadata(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                const Options& options);

    /// Écrit le dossier du document dans targetDir (remplace les fichiers d'un export précédent).
    /// Renvoie le chemin du dossier créé.
    static Result<QString> exportDocument(const QString& targetDir, const DocumentRecord& doc,
                                          const QList<AnnotationRecord>& annotations, const Options& options);
};

}  // namespace studylab
