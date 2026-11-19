#pragma once

#include "ui/shell/AppContext.h"

#include <QHash>
#include <QMainWindow>

#include "core/Result.h"

namespace studylab::ui {

class FadeStackedWidget;
class SearchPalette;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);
    ~MainWindow() override;

    void navigateTo(const QString& pageId);
    QString currentPage() const;
    QWidget* page(const QString& pageId) const { return m_pages.value(pageId); }
    void toggleFullScreen();
    /// « Rechercher partout » (Ctrl+K).
    void showSearch();
    SearchPalette* searchPalette() const { return m_search; }
    Sidebar* sidebar() const { return m_sidebar; }

    /// Ouvre un lien « studylab://open?… » (export Markdown) au bon passage.
    Result<void> openLink(const QString& link);
    /// Message d'une autre instance lancée par le système (lien cliqué ou nouveau lancement).
    void handleInstanceMessage(const QString& message);
    /// Ramène la fenêtre au premier plan.
    void bringToFront();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void addPage(const QString& id, QWidget* page);

    AppContext& m_ctx;
    Sidebar* m_sidebar = nullptr;
    FadeStackedWidget* m_stack = nullptr;
    SearchPalette* m_search = nullptr;
    QHash<QString, QWidget*> m_pages;
};

}  // namespace studylab::ui
