#pragma once

#include "core/domain/Document.h"
#include "ui/shell/AppContext.h"

#include <QTimer>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace studylab {
struct ImportReport;
}

namespace studylab::ui {

class DocumentDetailPanel;
class LibraryDelegate;
class LibraryModel;
class SegmentedControl;

/// Écran Bibliothèque : grille ou liste filtrable, panneau de détail, import (bouton, glisser-déposer).
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(AppContext& ctx, QWidget* parent = nullptr);

    /// Importe selon le mode choisi dans les paramètres (copie par défaut).
    void importPaths(const QStringList& paths);
    void selectDocument(qint64 id);

    LibraryModel* model() const { return m_model; }
    QListView* view() const { return m_view; }
    QLineEdit* searchEdit() const { return m_search; }
    QComboBox* statusFilter() const { return m_statusFilter; }
    QPushButton* favoritesFilter() const { return m_favorites; }
    SegmentedControl* viewToggle() const { return m_viewToggle; }
    DocumentDetailPanel* detailPanel() const { return m_detail; }
    QLabel* importStatusLabel() const { return m_bannerText; }
    QWidget* emptyState() const { return m_empty; }
    QPushButton* exportButton() const { return m_exportButton; }
    QPushButton* openExportButton() const { return m_openExport; }

    /// Exporte des documents en Markdown (coffre Obsidian + CSV) dans un dossier choisi, en arrière-plan.
    void exportDocuments(const QList<qint64>& ids);
    bool isExporting() const { return m_exporting; }
    QList<qint64> selectedIds() const;
    QList<qint64> visibleIds() const;
    /// Choix du dossier parent de l'export (boîte de dialogue par défaut ; les tests fournissent le chemin).
    void setExportDirectoryChooser(std::function<QString()> chooser) { m_chooseExportDir = std::move(chooser); }

signals:
    /// Fin d'un export (dossier créé, vide en cas d'échec).
    void exportFinished(const QString& folder);
    /// Demande d'ouverture d'un document dans le lecteur.
    void openDocumentRequested(qint64 id);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildHeader();
    QWidget* buildBanner();
    QWidget* buildFilters();
    QWidget* buildEmptyState();
    void applyFilters();
    void refresh();
    void refreshTags();
    void updateHeader();
    void updateEmptyState();
    void setViewMode(bool list);
    void onSelectionChanged();
    void chooseFiles();
    void chooseFolder();
    void onImportStarted();
    void onImportProgress(int done, int total);
    void onImportFinished(const ImportReport& report);
    void resetFilters();
    void showBannerState(const QString& state);
    void onExportFinished(const QString& folder, int documents, int annotations, int comments,
                          const QStringList& errors, bool cancelled, const QString& error);

    AppContext& m_ctx;
    LibraryModel* m_model;
    LibraryDelegate* m_delegate;
    QLabel* m_subtitle = nullptr;
    QWidget* m_banner = nullptr;
    QLabel* m_bannerText = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_details = nullptr;
    QPushButton* m_closeBanner = nullptr;
    QString m_reportDetails;
    QLineEdit* m_search = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QComboBox* m_formatFilter = nullptr;
    QComboBox* m_tagFilter = nullptr;
    QPushButton* m_favorites = nullptr;
    QComboBox* m_sort = nullptr;
    SegmentedControl* m_viewToggle = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QListView* m_view = nullptr;
    QWidget* m_empty = nullptr;
    QLabel* m_emptyTitle = nullptr;
    QLabel* m_emptyText = nullptr;
    QPushButton* m_emptyImport = nullptr;
    QPushButton* m_emptyFolder = nullptr;
    QPushButton* m_emptyReset = nullptr;
    DocumentDetailPanel* m_detail = nullptr;
    QTimer m_searchDebounce;
    qint64 m_selectedId = 0;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_openExport = nullptr;
    QString m_exportFolder;
    bool m_exporting = false;
    std::shared_ptr<std::atomic_bool> m_exportCancel;
    std::function<QString()> m_chooseExportDir;
    bool m_dirty = false;
};

}  // namespace studylab::ui
