#pragma once

#include "ui/shell/Navigation.h"

#include <QFrame>
#include <QHash>
#include <QList>

class QButtonGroup;
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

signals:
    void navigated(const QString& id);
    void searchRequested();

private:
    void refreshIcons();
    void rebuild();

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
};

}  // namespace studylab::ui
