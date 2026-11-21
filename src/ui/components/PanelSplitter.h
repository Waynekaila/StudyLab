#pragma once

#include <QList>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

class QSplitter;
class QToolButton;

namespace studylab {
class SettingsService;
}

namespace studylab::ui {

class ThemeManager;

/// Zone de travail entourée de panneaux latéraux redimensionnables et rétractables.
///
/// - Chaque panneau se redimensionne en tirant la poignée, entre une largeur minimale et maximale.
/// - Il se replie par son bouton, par F9 ou F10 (panneau de gauche ou de droite de l'écran courant), ou en le tirant
///   sous sa largeur minimale. Replié, il laisse une fine bande avec un bouton pour le rouvrir : on ne perd
///   jamais le chemin du retour.
/// - Largeurs et panneaux repliés sont mémorisés par écran (`ui/panels/<id>`) et restaurés au prochain lancement.
class PanelSplitter : public QWidget {
    Q_OBJECT
public:
    PanelSplitter(SettingsService& settings, ThemeManager& theme, const QString& id, QWidget* parent = nullptr);
    ~PanelSplitter() override;

    /// Comment le panneau s'ouvre et se ferme.
    enum class Mode {
        /// Rétractable : bouton « Replier », bande pour le rouvrir, F9 / F10, état mémorisé.
        Collapsible,
        /// Visibilité gérée par l'écran (bouton de barre d'outils, sélection…) : seule la largeur est mémorisée.
        External,
    };
    /// Panneau latéral (à gauche s'il est ajouté avant la zone de travail, à droite sinon).
    /// `title` sert au bouton de la bande repliée (« Afficher : … ») et aux lecteurs d'écran.
    void addPanel(QWidget* panel, const QString& title, int defaultWidth, int minWidth, int maxWidth,
                  Mode mode = Mode::Collapsible);
    /// Zone de travail : prend toute la place restante, ne se replie jamais.
    void addWorkArea(QWidget* area);

    QString id() const { return m_id; }
    QSplitter* splitter() const { return m_split; }
    bool isPanelOpen(QWidget* panel) const;
    void setPanelOpen(QWidget* panel, bool open);
    void togglePanel(QWidget* panel);
    /// Premier panneau à gauche, ou à droite, de la zone de travail (nullptr s'il n'y en a pas).
    QWidget* leadingPanel() const;
    QWidget* trailingPanel() const;
    /// Largeur mémorisée du panneau (ouvert ou non).
    int panelWidth(QWidget* panel) const;
    /// Bouton « Replier » à placer dans l'en-tête d'un panneau.
    QToolButton* makeCollapseButton(QWidget* panel, QWidget* parent);
    /// Panneau « External » : ce que font F9 / F10 (par défaut, l'afficher ou le masquer).
    void setToggleHandler(QWidget* panel, std::function<void(bool open)> handler);
    /// Bande affichée quand le panneau est replié.
    QToolButton* railButton(QWidget* panel) const;

signals:
    void panelToggled(QWidget* panel, bool open);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Panel {
        QPointer<QWidget> widget;
        QString title;
        int width = 0;
        int minWidth = 0;
        int maxWidth = 0;
        bool open = true;
        bool leading = true;
        Mode mode = Mode::Collapsible;
        QWidget* rail = nullptr;
        QToolButton* railButton = nullptr;
        QList<QPointer<QToolButton>> collapseButtons;
        std::function<void(bool)> toggleHandler;
    };
    Panel* find(QWidget* panel);
    const Panel* find(QWidget* panel) const;
    void applySizes();
    void onSplitterMoved();
    void scheduleSave();
    void save();
    void refreshIcons();

    SettingsService& m_settings;
    ThemeManager& m_theme;
    QString m_id;
    QSplitter* m_split;
    QWidget* m_workArea = nullptr;
    QList<Panel> m_panels;
    QTimer m_saveTimer;
    bool m_applied = false;
    bool m_applying = false;
};

}  // namespace studylab::ui
