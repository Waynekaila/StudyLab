#pragma once

#include "ui/reader/EpubView.h"
#include "ui/reader/ReaderTab.h"

#include <QFrame>
#include <QTimer>

#include <memory>
#include <optional>

class QFrame;
class QLabel;
class QLineEdit;
class QSlider;
class QComboBox;
class QStandardItemModel;
class QTreeView;

namespace studylab {
class EpubPackage;
}

namespace studylab::ui {

class SegmentedControl;

/// Panneau « Affichage du texte » : police, taille, interligne, marges, mise en page, thème.
class TypographyPanel : public QFrame {
    Q_OBJECT
public:
    TypographyPanel(ThemeManager& theme, QWidget* parent);
    void setSettings(const EpubSettings& settings);
    EpubSettings settings() const { return m_settings; }

    QComboBox* fontCombo() const { return m_font; }
    QSlider* sizeSlider() const { return m_size; }
    SegmentedControl* lineHeight() const { return m_lineHeight; }
    SegmentedControl* margins() const { return m_margins; }
    SegmentedControl* layoutMode() const { return m_layout; }
    QToolButton* themeButton(ReadingTheme theme) const;

signals:
    void settingsChanged(const studylab::ui::EpubSettings& settings);

private:
    void emitChange();
    ThemeManager& m_theme;
    EpubSettings m_settings;
    QComboBox* m_font;
    QSlider* m_size;
    SegmentedControl* m_lineHeight;
    SegmentedControl* m_margins;
    SegmentedControl* m_layout;
    QList<QToolButton*> m_themes;
};

/// Un EPUB ouvert dans un onglet : sommaire, vue foliate-js, affichage du texte, recherche, annotations.
/// Qt WebEngine n'est démarré qu'au premier affichage de l'onglet.
class EpubReaderTab : public ReaderTab {
    Q_OBJECT
public:
    enum class Tool { Select, Highlight, Underline, Strikeout };

    EpubReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent = nullptr);
    ~EpubReaderTab() override;

    Result<void> ensureLoaded();
    bool isLoaded() const override { return m_loaded; }
    bool isOpened() const { return m_view && m_view->isOpened(); }
    void setFocusMode(bool on) override;
    void savePosition() override;

    EpubView* view() const { return m_view; }
    QTreeView* tocView() const { return m_tocView; }
    QLabel* chapterLabel() const { return m_chapterLabel; }
    QLabel* progressLabel() const { return m_progressLabel; }
    QLabel* pageLabel() const { return m_pageLabel; }
    QLineEdit* searchEdit() const { return m_searchEdit; }
    QLabel* searchCountLabel() const { return m_searchCount; }
    TypographyPanel* typographyPanel() const { return m_typo; }
    QToolButton* typographyButton() const { return m_typoButton; }
    QToolButton* toolButtonFor(Tool tool) const;
    QToolButton* bookmarkButton() const { return m_bookmarkButton; }
    EpubSettings displaySettings() const { return m_display; }
    void setDisplaySettings(const EpubSettings& settings);
    const std::optional<EpubSelection>& selection() const { return m_selection; }

    void showSearch();
    void findNext();
    void findPrevious();
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    Result<QList<AnnotationRecord>> annotateSelection(AnnotationType type, AnnotationColor color) override;
    void toggleBookmark() override;
    void revealAnnotation(qint64 id) override;
    QString chapterOf(int section) const override;
    /// Signet sur la vue courante (0 : aucun).
    qint64 visibleBookmark() const { return m_visibleBookmark; }

protected:
    void showEvent(QShowEvent* event) override;
    void pushMarks() override;
    void copySelection() override;
    std::optional<CommentTarget> selectionTarget() const override;
    CommentTarget positionTarget() const override;
    void revealLocator(const QJsonObject& locator) override;
    void showEditPopup(qint64 id) override;
    void updateBookmarkButton() override;
    bool isReady() const override { return isOpened(); }

private:
    QWidget* buildToolbar();
    QWidget* buildSidePanel();
    QWidget* buildSearchBar();
    void refreshIcons();
    void onOpened();
    void onRelocated(const EpubLocation& location);
    void onSelection(const EpubSelection& selection);
    void updateSearchCount();
    void fail(const QString& message);
    void goToChapter(int delta);
    void applyFooterColors();

    bool m_loaded = false;
    bool m_loadFailed = false;
    QString m_token;
    std::shared_ptr<EpubPackage> m_package;
    EpubSettings m_display;
    Tool m_tool = Tool::Select;
    std::optional<EpubSelection> m_selection;
    QRect m_lastAnnotationRect;
    qint64 m_visibleBookmark = 0;
    int m_searchIndex = -1;

    EpubView* m_view = nullptr;
    QWidget* m_viewHost = nullptr;
    QWidget* m_toolbar = nullptr;
    QFrame* m_side = nullptr;
    QTreeView* m_tocView = nullptr;
    QStandardItemModel* m_tocModel = nullptr;
    QLabel* m_noToc = nullptr;
    QLabel* m_chapterLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLabel* m_pageLabel = nullptr;
    QWidget* m_footer = nullptr;
    QWidget* m_searchBar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchCount = nullptr;
    QWidget* m_errorBox = nullptr;
    TypographyPanel* m_typo = nullptr;
    QToolButton* m_tocToggle = nullptr;
    QToolButton* m_back = nullptr;
    QToolButton* m_forward = nullptr;
    QToolButton* m_prevChapter = nullptr;
    QToolButton* m_nextChapter = nullptr;
    QToolButton* m_typoButton = nullptr;
    QToolButton* m_searchButton = nullptr;
    QToolButton* m_focusButton = nullptr;
    QToolButton* m_fullButton = nullptr;
    QToolButton* m_bookmarkButton = nullptr;
    QHash<int, QToolButton*> m_toolButtons;
    QTimer m_saveTimer;
    QTimer m_searchDebounce;
};

}  // namespace studylab::ui
