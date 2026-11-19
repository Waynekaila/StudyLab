#pragma once

#include <QFrame>
#include <QList>

class QButtonGroup;
class QLabel;
class QPushButton;
class QToolButton;

namespace studylab::ui {

class ThemeManager;

/// Navigation latérale principale. N'affiche que les espaces réellement disponibles.
class Sidebar : public QFrame {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        QString icon;
        QString label;
    };

    Sidebar(ThemeManager& theme, const QList<Entry>& entries, QWidget* parent = nullptr);

    void setCurrent(const QString& id);
    QString current() const;
    QToolButton* button(const QString& id) const;
    QPushButton* searchButton() const { return m_search; }

signals:
    void navigated(const QString& id);
    void searchRequested();

private:
    void refreshIcons();

    ThemeManager& m_theme;
    QList<Entry> m_entries;
    QList<QToolButton*> m_buttons;
    QButtonGroup* m_group;
    QLabel* m_logo;
    QPushButton* m_search;
    QLabel* m_footerIcon;
};

}  // namespace studylab::ui
