#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Document.h"
#include "ui/reader/PdfView.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
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
class PdfReaderTab : public QWidget {
    Q_OBJECT
public:
    PdfReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent = nullptr);
    ~PdfReaderTab() override;

    qint64 documentId() const { return m_documentId; }
    QString title() const { return m_record.title; }
    /// Charge le fichier si ce n'est pas déjà fait. Demande le mot de passe si nécessaire.
    Result<void> ensureLoaded();
    bool isLoaded() const { return m_loaded; }

    PdfView* view() const { return m_view; }
    QLineEdit* pageEdit() const { return m_pageEdit; }
    QLineEdit* searchEdit() const { return m_searchEdit; }
    QLabel* searchCountLabel() const { return m_searchCount; }
    QListView* thumbnails() const { return m_thumbs; }
    QTreeView* outline() const { return m_outline; }
    QComboBox* zoomCombo() const { return m_zoomCombo; }
    QWidget* sidePanel() const { return m_side; }

    void showSearch();
    void findNext();
    void findPrevious();
    /// Mode sans distraction : seule la page reste visible.
    void setFocusMode(bool on);
    /// Enregistre immédiatement la position (fermeture de l'onglet ou de l'application).
    void savePosition();

    /// Mot de passe fourni à l'avance (tests) ; sinon une boîte de dialogue le demande.
    void setPasswordProvider(std::function<QString(int attempt)> provider) { m_passwordProvider = std::move(provider); }

    // Annotations
    using NoteEditor = std::function<std::optional<QString>(const QString& quote, const QString& current)>;
    /// Saisie des notes fournie à l'avance (tests) ; sinon une boîte de dialogue.
    void setNoteEditor(NoteEditor editor) { m_noteEditor = std::move(editor); }
    /// Annote la sélection courante (une annotation par page couverte) puis efface la sélection.
    Result<QList<AnnotationRecord>> annotateSelection(AnnotationType type, AnnotationColor color);
    Result<AnnotationRecord> addArea(int page, const QRectF& normalizedRect);
    Result<AnnotationRecord> addNote(int page, const QPointF& normalizedPoint, const QString& text);
    void toggleBookmark();
    void deleteAnnotation(qint64 id);
    /// Rétablit la dernière annotation supprimée (Ctrl+Z).
    bool undoDelete();
    void editNote(qint64 id);
    void revealAnnotation(qint64 id);
    /// Exporte les annotations du document en Markdown dans le dossier donné ; renvoie le dossier créé.
    Result<QString> exportMarkdown(const QString& directory);
    const QList<AnnotationRecord>& annotations() const { return m_records; }
    AnnotationColor currentColor() const { return m_color; }
    void setCurrentColor(AnnotationColor color);
    /// Titre du chapitre (sommaire) contenant la page, vide si le document n'a pas de sommaire.
    QString chapterOf(int page) const;

    AnnotationsPane* annotationsPane() const { return m_pane; }
    AnnotationPopup* popup() const { return m_popup; }
    QToolButton* toolButtonFor(PdfView::Tool tool) const;
    QToolButton* bookmarkButton() const { return m_bookmarkButton; }
    QToolButton* colorButton(AnnotationColor color) const;
    QToolButton* paneToggle() const { return m_paneToggle; }

signals:
    void focusModeToggled(bool on);
    void fullScreenToggled();
    void loadFailed(const QString& message);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildToolbar();
    QWidget* buildSidePanel();
    QWidget* buildSearchBar();
    void updatePageIndicator();
    void updateZoomCombo();
    void updateHistoryButtons();
    void updateSearchCount();
    void restoreState();
    void refreshIcons();
    void reloadAnnotations();
    void pushMarks();
    void buildChapters();
    void onSelectionFinished();
    void showEditPopup(qint64 id);
    void showAnnotationMenu(qint64 id, QPoint globalPos);
    void exportWithDialog();
    void setPaneVisible(bool on);
    void updateBookmarkButton();
    void reportError(const Error& error);
    std::optional<QString> askNote(const QString& quote, const QString& current);

    AppContext& m_ctx;
    LibraryService& m_library;
    AnnotationService& m_annotations;
    SettingsService& m_settings;
    ThemeManager& m_theme;
    qint64 m_documentId;
    DocumentRecord m_record;
    bool m_loaded = false;
    bool m_loadFailed = false;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<PdfRenderQueue> m_queue;
    QPdfSearchModel* m_searchModel = nullptr;
    QPdfBookmarkModel* m_bookmarks = nullptr;
    ThumbnailModel* m_thumbModel = nullptr;
    std::function<QString(int)> m_passwordProvider;

    PdfView* m_view = nullptr;
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
    bool m_focusMode = false;
    bool m_findPending = false;

    // Annotations
    QList<AnnotationRecord> m_records;
    QList<qint64> m_deleted;  ///< pile d'annulation
    AnnotationColor m_color = AnnotationColor::Yellow;
    NoteEditor m_noteEditor;
    QList<QPair<int, QString>> m_chapters;  ///< (page, titre), triés par page
    AnnotationsPane* m_pane = nullptr;
    AnnotationPopup* m_popup = nullptr;
    qint64 m_popupAnnotation = 0;
    QHash<int, QToolButton*> m_toolButtons;  ///< par PdfView::Tool
    QList<QToolButton*> m_colorButtons;
    QToolButton* m_bookmarkButton = nullptr;
    QToolButton* m_paneToggle = nullptr;
};

}  // namespace studylab::ui
