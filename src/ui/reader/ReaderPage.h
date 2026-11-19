#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

class QShortcut;
class QStackedWidget;
class QTabWidget;

namespace studylab::ui {

class PdfReaderTab;

/// Espace Lecture : documents ouverts dans des onglets, restaurés au démarrage.
class ReaderPage : public QWidget {
    Q_OBJECT
public:
    explicit ReaderPage(AppContext& ctx, QWidget* parent = nullptr);
    ~ReaderPage() override;

    /// Ouvre un document (ou active son onglet s'il est déjà ouvert). PDF uniquement pour l'instant.
    void openDocument(qint64 documentId);
    void closeTab(int index);
    int tabCount() const;
    PdfReaderTab* currentTab() const;
    PdfReaderTab* tabFor(qint64 documentId) const;
    QTabWidget* tabs() const { return m_tabs; }
    /// Enregistre les positions et la liste des onglets (fermeture de l'application).
    void saveSession();

signals:
    void focusModeChanged(bool on);
    void fullScreenRequested();
    void libraryRequested();

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
