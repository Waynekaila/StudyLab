#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

namespace studylab {
class NoteService;
class ReferenceService;
}

namespace studylab::ui {

/// Image « attachments/… » d'une note en URI data: (vide si absente, invalide ou > 20 Mo).
QString attachmentDataUri(const QString& attachmentsDir, const QString& relPath);

/// Données de l'aperçu d'une note pour StudyLab.setNote / setNotes (studylab-markdown.js) : titre, texte sans front
/// matter, puces (dossier, #tags), liens non résolus ou vers des documents, images, citations et bibliographie.
/// Partagé par l'aperçu de l'éditeur et l'export HTML / PDF.
QJsonObject notePreviewPayload(NoteService& notes, ReferenceService& refs, const QString& title, const QString& folder,
                               const QString& text, const std::function<QString(const QString&)>& imageData);

}  // namespace studylab::ui
