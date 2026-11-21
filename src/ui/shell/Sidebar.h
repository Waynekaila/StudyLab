#pragma once

#include "ui/shell/Navigation.h"

#include <QFrame>
#include <QHash>
#include <QList>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace studylab::ui {

class ThemeManager;

/// Navigation latérale principale. N'affiche que les espaces réellement disponibles.
class Sidebar : public QFrame {
    Q_OBJECT
public:
    using Entry = Navigation::Entry;

    Sidebar(ThemeManager& theme, const QList<Entry>& entries, QWidget* parent = nullptr);

    /// Remplace les entrées affichées (ordre et entrées masquées choisis dans les paramètres).
    void setEntries(const QList<Entry>& entries);
    QStringList entryIds() const;

    void setCurrent(const QString& id);
    QString current() const;
    QToolButton* button(const QString& id) const;
    /// Pastille lavande à droite de l'entrée (révisions dues) ; masquée à 0.
    void setBadge(const QString& id, int count);
    QLabel* badge(const QString& id) const;
    QPushButton* searchButton() const { return m_search; }
    /// Mode réduit : une colonne d'icônes (libellés en infobulle) qui laisse la place au travail.
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }
    QToolButton* compactButton() const { return m_compactButton; }
    /// Raccourci affiché dans l'infobulle du bouton de réduction.
    void setCompactShortcut(const QString& text);

signals:
    void navigated(const QString& id);
    void searchRequested();
    void compactToggled(bool compact);

private:
    void refreshIcons();
    void rebuild();
    void applyCompact();

    ThemeManager& m_theme;
    QList<Entry> m_entries;
    QList<QToolButton*> m_buttons;
    QList<QLabel*> m_badges;
    QHash<QString, int> m_badgeCounts;
    QString m_current;
    QButtonGroup* m_group;
    QVBoxLayout* m_navLayout;
    QLabel* m_logo;
    QPushButton* m_search;
    QLabel* m_footerIcon;
    QLabel* m_brand = nullptr;
    QLabel* m_footerText = nullptr;
    QHBoxLayout* m_brandRow = nullptr;
    QHBoxLayout* m_footer = nullptr;
    QToolButton* m_compactButton = nullptr;
    QString m_compactShortcut;
    bool m_compact = false;
};

}  // namespace studylab::ui
