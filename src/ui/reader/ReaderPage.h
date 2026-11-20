#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

class QShortcut;
class QStackedWidget;
class QTabWidget;

namespace studylab::ui {

class ReaderTab;

/// Espace Lecture : documents ouverts dans des onglets, restaurés au démarrage.
class ReaderPage : public QWidget {
    Q_OBJECT
public:
    explicit ReaderPage(AppContext& ctx, QWidget* parent = nullptr);
    ~ReaderPage() override;

    /// Ouvre un document (ou active son onglet s'il est déjà ouvert). PDF ; EPUB, TXT et Markdown dans le lecteur EPUB.
    void openDocument(qint64 documentId);
    /// Ouvre le document et affiche l'annotation ou le commentaire (résultat de recherche).
    void openItem(qint64 documentId, bool comment, qint64 itemId);
    /// Ouvre le document à la page ou la section d'un résultat plein texte, `query` mis en évidence.
    void openPassage(qint64 documentId, int unit, const QString& query);
    void closeTab(int index);
    int tabCount() const;
    ReaderTab* currentTab() const;
    ReaderTab* tabFor(qint64 documentId) const;
    QTabWidget* tabs() const { return m_tabs; }
    /// Enregistre les positions et la liste des onglets (fermeture de l'application).
    void saveSession();

signals:
    void focusModeChanged(bool on);
    void fullScreenRequested();
    void libraryRequested();
    /// Note créée depuis la lecture : à ouvrir dans l'espace de notes.
    void noteCreated(qint64 noteId);

private:
    void restoreSession();
    void updateEmptyState();
    void persistTabs();

    AppContext& m_ctx;
    QStackedWidget* m_stack;
    QTabWidget* m_tabs;
    QWidget* m_empty;
    QShortcut* m_exitFocus;
    bool m_restoring = false;
};

}  // namespace studylab::ui
