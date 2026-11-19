#pragma once

#include "services/search/SearchService.h"

#include <QFrame>
#include <QTimer>

class QLabel;
class QLineEdit;
class QListWidget;

namespace studylab::ui {

class ThemeManager;

/// « Rechercher partout » (Ctrl+K) : panneau flottant au-dessus de la fenêtre. Recherche minimale du
/// MVP dans les métadonnées des documents, les annotations et les commentaires, résultats groupés.
/// Entrée ouvre le résultat sélectionné ; Échap ferme.
class SearchPalette : public QFrame {
    Q_OBJECT
public:
    SearchPalette(SearchService& search, ThemeManager& theme, QWidget* window);

    /// Affiche le panneau en haut de la fenêtre, champ sélectionné.
    void popup();
    void search(const QString& text);
    const SearchResults& results() const { return m_results; }

    QLineEdit* input() const { return m_input; }
    QListWidget* list() const { return m_list; }
    QLabel* status() const { return m_status; }

signals:
    void hitActivated(const studylab::SearchHit& hit);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void run();
    void place();
    void activate(int row);
    void moveSelection(int delta);
    QString snippetHtml(const QString& snippet) const;

    SearchService& m_search;
    ThemeManager& m_theme;
    QLineEdit* m_input;
    QListWidget* m_list;
    QLabel* m_status;
    QTimer m_debounce;
    SearchResults m_results;
    QList<SearchHit> m_rows;  ///< résultat de chaque ligne (titre de groupe : kind Document, itemId 0)
};

}  // namespace studylab::ui

Q_DECLARE_METATYPE(studylab::SearchHit)
