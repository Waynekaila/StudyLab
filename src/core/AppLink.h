#pragma once

#include <QString>

#include <optional>

namespace studylab {

/// Lien « studylab://open?doc=<uuid>[&annotation=<uuid>|&comment=<uuid>] » écrit dans les exports Markdown :
/// un clic dans Obsidian (ou ailleurs) rouvre le document au passage dans StudyLab.
struct AppLink {
    QString documentUuid;
    QString annotationUuid;
    QString commentUuid;

    QString toString() const;
    /// Lien valide (schéma, action « open », UUID bien formés) ; nullopt sinon.
    static std::optional<AppLink> parse(const QString& text);
    /// Vrai si l'argument de ligne de commande ressemble à un lien StudyLab.
    static bool looksLikeLink(const QString& argument);
};

}  // namespace studylab
