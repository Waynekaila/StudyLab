#pragma once

#include "ui/shell/AppContext.h"

#include <QHash>
#include <QMainWindow>

namespace studylab::ui {

class FadeStackedWidget;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);

    void navigateTo(const QString& pageId);
    QString currentPage() const;
    QWidget* page(const QString& pageId) const { return m_pages.value(pageId); }
    void toggleFullScreen();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void addPage(const QString& id, QWidget* page);

    AppContext& m_ctx;
    Sidebar* m_sidebar = nullptr;
    FadeStackedWidget* m_stack = nullptr;
    QHash<QString, QWidget*> m_pages;
};

}  // namespace studylab::ui
