#pragma once

#include <QPlainTextEdit>

namespace studylab::ui {

/// Éditeur Markdown d'une note : coller ou déposer une image l'ajoute aux pièces jointes de la note
/// (signal imageInserted) au lieu d'insérer du texte.
class NoteEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit NoteEditor(QWidget* parent = nullptr);

    /// Place le curseur au début de la ligne (0) et la rend visible au tiers haut de l'éditeur.
    void goToLine(int line);

signals:
    /// Image collée ou déposée : données encodées et extension (« png », « jpg »…).
    void imageInserted(const QByteArray& data, const QString& extension);

protected:
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;
};

}  // namespace studylab::ui
