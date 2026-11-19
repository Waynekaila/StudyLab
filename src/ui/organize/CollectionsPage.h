#pragma once

#include "core/domain/Organize.h"
#include "ui/shell/AppContext.h"

#include <QList>
#include <QWidget>

#include <functional>

class QLabel;
class QTreeWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace studylab::ui {

/// Collections (05-maquettes §6) : collections manuelles, listes de lecture et projets (documents et notes, dans
/// l'ordre choisi), collections intelligentes (documents qui respectent des règles, recalculées à l'affichage).
class CollectionsPage : public QWidget {
    Q_OBJECT
public:
    explicit CollectionsPage(AppContext& ctx, QWidget* parent = nullptr);

    /// Crée une collection manuelle, liste de lecture ou projet, et l'affiche.
    qint64 createCollection(const QString& name, CollectionKind kind);
    void showCollection(qint64 id);
    qint64 currentCollectionId() const { return m_currentId; }
    /// Ouvre l'éditeur de règles (nouvelle collection si id = 0). Remplaçable pour les tests.
    void editSmartCollection(qint64 id);
    void setSmartEditor(std::function<qint64(qint64 id)> editor) { m_smartEditor = std::move(editor); }

    // Accès pour les tests
    QTreeWidget* list() const { return m_list; }
    QList<QPushButton*> itemRows() const { return m_rows; }
    QLabel* titleLabel() const { return m_title; }
    QLabel* rulesLabel() const { return m_rules; }
    QToolButton* newButton() const { return m_new; }
    /// Menu d'un élément de la collection (rempli, non affiché) : pour les tests.
    QMenu* itemMenu(int row);

signals:
    void openDocumentRequested(qint64 documentId);
    void openNoteRequested(qint64 noteId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildNav();
    void buildPage();
    void refreshList();
    void refreshPage();
    void promptNew(CollectionKind kind);
    void promptRename(qint64 id);
    void confirmRemove(qint64 id);
    void moveItem(int row, int delta);

    AppContext& m_ctx;
    qint64 m_currentId = 0;
    bool m_dirty = true;
    std::function<qint64(qint64)> m_smartEditor;
    QList<CollectionItem> m_items;

    QToolButton* m_new = nullptr;
    QTreeWidget* m_list = nullptr;
    QStackedWidget* m_center = nullptr;
    QWidget* m_empty = nullptr;
    QWidget* m_page = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_kind = nullptr;
    QLabel* m_rules = nullptr;
    QPushButton* m_editRules = nullptr;
    QToolButton* m_more = nullptr;
    QLabel* m_count = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    QLabel* m_itemsEmpty = nullptr;
    QList<QPushButton*> m_rows;
    QMenu* m_menu = nullptr;
};

}  // namespace studylab::ui
