#pragma once

#include "core/domain/Organize.h"
#include "ui/shell/AppContext.h"

#include <QList>
#include <QWidget>

class QFrame;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeWidgetItem;
class QVBoxLayout;

namespace studylab::ui {

class DragTree;
class SegmentedControl;

/// Matières (05-maquettes §6) : liste hiérarchique (couleur, compteur, glisser pour réordonner ou ranger,
/// clic droit pour couleur, archivage, suppression) et page de la matière : chiffres clés, documents et notes,
/// sous-matières comprises.
class SubjectsPage : public QWidget {
    Q_OBJECT
public:
    enum class Tab { Overview, Documents, Notes };

    explicit SubjectsPage(AppContext& ctx, QWidget* parent = nullptr);

    /// Crée une matière (sous parentId si non nul) et l'affiche. 0 en cas d'échec.
    qint64 createSubject(const QString& name, qint64 parentId = 0);
    void showSubject(qint64 id);
    qint64 currentSubjectId() const { return m_currentId; }
    void setTab(Tab tab);

    // Accès pour les tests
    DragTree* tree() const { return m_tree; }
    QList<QPushButton*> documentRows() const { return m_documentRows; }
    QList<QPushButton*> noteRows() const { return m_noteRows; }
    QLabel* titleLabel() const { return m_title; }
    QLabel* pathLabel() const { return m_path; }
    QList<QLabel*> statValues() const { return m_statValues; }
    QTreeWidgetItem* itemFor(qint64 id) const;
    /// Menu contextuel d'une matière (rempli, non affiché) : pour les tests.
    QMenu* contextMenuFor(qint64 id);

signals:
    void openDocumentRequested(qint64 documentId);
    void openNoteRequested(qint64 noteId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildNav();
    void buildPage();
    void refreshTree();
    void refreshPage();
    void fillLists();
    void promptNewSubject(qint64 parentId);
    void promptRename(qint64 id);
    void confirmRemove(qint64 id);
    void onItemMoved(QTreeWidgetItem* item, QTreeWidgetItem* newParent, int index);
    QWidget* listCard(const QString& title, QLabel** count, QVBoxLayout** list, QLabel** empty);

    AppContext& m_ctx;
    qint64 m_currentId = 0;
    bool m_showArchived = false;
    bool m_dirty = true;
    Tab m_tab = Tab::Overview;

    DragTree* m_tree = nullptr;
    QToolButton* m_archivedToggle = nullptr;
    QStackedWidget* m_center = nullptr;
    QWidget* m_empty = nullptr;
    QWidget* m_page = nullptr;
    QFrame* m_band = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_path = nullptr;
    QList<QLabel*> m_statValues;
    QProgressBar* m_progress = nullptr;
    SegmentedControl* m_tabs = nullptr;
    QWidget* m_docCard = nullptr;
    QWidget* m_noteCard = nullptr;
    QLabel* m_docCount = nullptr;
    QLabel* m_noteCount = nullptr;
    QVBoxLayout* m_docList = nullptr;
    QVBoxLayout* m_noteList = nullptr;
    QLabel* m_docEmpty = nullptr;
    QLabel* m_noteEmpty = nullptr;
    QList<QPushButton*> m_documentRows;
    QList<QPushButton*> m_noteRows;
    QMenu* m_menu = nullptr;
};

}  // namespace studylab::ui
