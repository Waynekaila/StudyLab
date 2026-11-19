#pragma once

#include <QPlainTextEdit>

#include <functional>

class QCompleter;
class QStringListModel;

namespace studylab::ui {

/// Éditeur Markdown d'une note : coller ou déposer une image l'ajoute aux pièces jointes de la note
/// (signal imageInserted) au lieu d'insérer du texte. Taper « [[ » propose les titres des notes et des documents.
class NoteEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit NoteEditor(QWidget* parent = nullptr);

    /// Place le curseur au début de la ligne (0) et la rend visible au tiers haut de l'éditeur.
    void goToLine(int line);

    /// Source des titres proposés après « [[ » (appelée à l'ouverture de la liste).
    void setLinkCompletions(std::function<QStringList()> provider);
    QCompleter* linkCompleter() const { return m_completer; }

signals:
    /// Image collée ou déposée : données encodées et extension (« png », « jpg »…).
    void imageInserted(const QByteArray& data, const QString& extension);

protected:
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// Texte tapé depuis « [[ » sur la ligne du curseur, ou null hors d'un lien.
    QString linkPrefix() const;
    void updateCompletion();
    void insertCompletion(const QString& title);

    std::function<QStringList()> m_provider;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completions = nullptr;
};

}  // namespace studylab::ui
