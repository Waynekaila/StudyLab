#pragma once

#include "core/domain/Annotation.h"

#include <QFrame>
#include <QList>

class QToolButton;
class QPushButton;

namespace studylab::ui {

class ThemeManager;

/// Barre flottante au-dessus d'une sélection (créer une annotation) ou d'une annotation existante
/// (changer sa couleur ou son type, écrire une note, la supprimer).
class AnnotationPopup : public QFrame {
    Q_OBJECT
public:
    enum class Mode { Create, Edit };

    AnnotationPopup(ThemeManager& theme, QWidget* parent);

    /// Affiche la barre au-dessus (ou au-dessous, faute de place) du rectangle donné, en coordonnées du parent.
    void showFor(Mode mode, const QRect& anchor, AnnotationColor color = AnnotationColor::Yellow,
                 AnnotationType type = AnnotationType::Highlight, bool hasNote = false);
    Mode mode() const { return m_mode; }
    /// Bouton d'une couleur (tests et accessibilité).
    QToolButton* colorButton(AnnotationColor c) const;
    QToolButton* typeButton(AnnotationType t) const;
    QPushButton* noteButton() const { return m_note; }
    QPushButton* copyButton() const { return m_copy; }
    QPushButton* deleteButton() const { return m_delete; }

signals:
    void colorPicked(studylab::AnnotationColor color);
    void typePicked(studylab::AnnotationType type);
    void noteRequested();
    void copyRequested();
    void deleteRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refreshIcons();

    ThemeManager& m_theme;
    Mode m_mode = Mode::Create;
    QList<QToolButton*> m_colors;
    QToolButton* m_underline;
    QToolButton* m_strike;
    QToolButton* m_highlight;
    QPushButton* m_note;
    QPushButton* m_copy;
    QPushButton* m_delete;
    QFrame* m_sep;
};

/// Saisie de la note d'une annotation.
class NoteDialog {
public:
    /// Renvoie le texte saisi, ou std::nullopt si l'utilisateur annule.
    static std::optional<QString> edit(QWidget* parent, ThemeManager& theme, const QString& quote,
                                       const QString& current);
};

}  // namespace studylab::ui
