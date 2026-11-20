#pragma once

#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderTab.h"
#include "services/pdfinterop/PdfInteropService.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QListView;
class QPdfBookmarkModel;
class QPdfDocument;
class QPdfSearchModel;
class QStackedWidget;
class QButtonGroup;
class QToolButton;
class QTreeView;

namespace studylab {
class AnnotationService;
class LibraryService;
class PdfRenderQueue;
class SettingsService;
struct AppContext;
}

namespace studylab::ui {

class AnnotationPopup;
class AnnotationsPane;
class SegmentedControl;
class ThemeManager;
class ThumbnailModel;

/// Un document PDF ouvert dans un onglet : barre d'outils, miniatures/sommaire, vue, recherche.
/// Le fichier n'est chargé qu'au premier affichage de l'onglet (restauration rapide de nombreux onglets).
class PdfReaderTab : public ReaderTab {
    Q_OBJECT
public:
    PdfReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent = nullptr);
    ~PdfReaderTab() override;

    /// Charge le fichier si ce n'est pas déjà fait. Demande le mot de passe si nécessaire.
    Result<void> ensureLoaded();
    bool isLoaded() const override { return m_loaded; }

    PdfView* view() const { return m_view; }
    QLineEdit* pageEdit() const { return m_pageEdit; }
    QLineEdit* searchEdit() const { return m_searchEdit; }
    QLabel* searchCountLabel() const { return m_searchCount; }
    QListView* thumbnails() const { return m_thumbs; }
    QTreeView* outline() const { return m_outline; }
    QComboBox* zoomCombo() const { return m_zoomCombo; }
    QWidget* sidePanel() const { return m_side; }

    void showSearch();
    /// Relit le texte reconnu (OCR) du document, peu après (une page vient d'être reconnue).
    void reloadOcrText();
    QLabel* ocrBadge() const { return m_ocrBadge; }
    void findNext();
    void findPrevious();
    /// Mode sans distraction : seule la page reste visible.
    void setFocusMode(bool on) override;
    /// Enregistre immédiatement la position (fermeture de l'onglet ou de l'application).
    void savePosition() override;

    /// Mot de passe fourni à l'avance (tests) ; sinon une boîte de dialogue le demande.
    void setPasswordProvider(std::function<QString(int attempt)> provider) { m_passwordProvider = std::move(provider); }

    // Annotations
    /// Annote la sélection courante (une annotation par page couverte) puis efface la sélection.
    Result<QList<AnnotationRecord>> annotateSelection(AnnotationType type, AnnotationColor color) override;
    Result<AnnotationRecord> addArea(int page, const QRectF& normalizedRect);
    Result<AnnotationRecord> addNote(int page, const QPointF& normalizedPoint, const QString& text);
    void toggleBookmark() override;
    void revealAnnotation(qint64 id) override;
    /// Titre du chapitre (sommaire) contenant la page, vide si le document n'a pas de sommaire.
    QString chapterOf(int page) const override;

    // Interopérabilité PDF (V2-5)
    /// Importe les annotations du fichier absentes de StudyLab ; le bandeau affiche le résultat.
    Result<PdfImportReport> importFileAnnotations();
    /// Écrit une copie annotée du PDF ; le bandeau affiche le résultat.
    Result<PdfExportReport> exportAnnotatedCopy(const QString& path);
    void exportAnnotatedCopyWithDialog();
    /// Dessin des annotations enregistrées dans le fichier (mémorisé par document).
    void setFileAnnotationsVisible(bool visible);
    bool fileAnnotationsVisible() const;
    QMenu* moreMenu() const { return m_moreMenu; }
    QFrame* fileBanner() const { return m_fileBanner; }
    QLabel* fileBannerText() const { return m_fileBannerText; }
    QPushButton* fileBannerImportButton() const { return m_fileBannerImport; }
    QPushButton* fileBannerIgnoreButton() const { return m_fileBannerIgnore; }

    QToolButton* toolButtonFor(PdfView::Tool tool) const;
    QToolButton* bookmarkButton() const { return m_bookmarkButton; }

signals:
    /// Lecture des annotations du fichier terminée (à l'ouverture) : `fresh` annotations à importer.
    void fileAnnotationsScanned(int fresh);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildToolbar();
    QWidget* buildSidePanel();
    QWidget* buildSearchBar();
    void updatePageIndicator();
    void loadOcrText();
    void updateZoomCombo();
    void updateHistoryButtons();
    void updateSearchCount();
    void restoreState();
    void refreshIcons();
    void pushMarks() override;
    void copySelection() override;
    std::optional<CommentTarget> selectionTarget() const override;
    CommentTarget positionTarget() const override;
    void revealLocator(const QJsonObject& locator) override;
    void showPassage(int unit, const QString& query) override;
    void showEditPopup(qint64 id) override;
    void updateBookmarkButton() override;
    void buildChapters();
    QWidget* buildFileBanner();
    void startFileAnnotationScan();
    void showFileBanner(const QString& text, const QString& state, bool offerImport);
    PdfInteropService interop();
    void onSelectionFinished();

    bool m_loaded = false;
    bool m_loadFailed = false;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<PdfRenderQueue> m_queue;
    QPdfSearchModel* m_searchModel = nullptr;
    QPdfBookmarkModel* m_bookmarks = nullptr;
    ThumbnailModel* m_thumbModel = nullptr;
    std::function<QString(int)> m_passwordProvider;

    PdfView* m_view = nullptr;
    QToolButton* m_moreButton = nullptr;
    QMenu* m_moreMenu = nullptr;
    QAction* m_fileAnnotationsAction = nullptr;
    QFrame* m_fileBanner = nullptr;
    QLabel* m_fileBannerText = nullptr;
    QPushButton* m_fileBannerImport = nullptr;
    QPushButton* m_fileBannerIgnore = nullptr;
    QWidget* m_toolbar = nullptr;
    QFrame* m_side = nullptr;
    SegmentedControl* m_sideTabs = nullptr;
    QStackedWidget* m_sideStack = nullptr;
    QListView* m_thumbs = nullptr;
    QTreeView* m_outline = nullptr;
    QLabel* m_noOutline = nullptr;
    QWidget* m_searchBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchCount = nullptr;
    QLineEdit* m_pageEdit = nullptr;
    QLabel* m_pageTotal = nullptr;
    QLabel* m_ocrBadge = nullptr;
    QTimer m_ocrReload;
    QComboBox* m_zoomCombo = nullptr;
    QToolButton* m_sideToggle = nullptr;
    QToolButton* m_back = nullptr;
    QToolButton* m_forward = nullptr;
    QToolButton* m_zoomOut = nullptr;
    QToolButton* m_zoomIn = nullptr;
    QToolButton* m_rotate = nullptr;
    QToolButton* m_double = nullptr;
    QToolButton* m_searchButton = nullptr;
    QToolButton* m_focusButton = nullptr;
    QToolButton* m_fullButton = nullptr;
    QWidget* m_errorBox = nullptr;
    QTimer m_saveTimer;
    QTimer m_searchDebounce;
    bool m_findPending = false;

    // Annotations
    QList<QPair<int, QString>> m_chapters;  ///< (page, titre), triés par page
    QHash<int, QToolButton*> m_toolButtons;  ///< par PdfView::Tool
    QToolButton* m_bookmarkButton = nullptr;
};

}  // namespace studylab::ui
