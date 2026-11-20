#pragma once

#include "core/domain/Note.h"
#include "services/references/ReferenceService.h"
#include "ui/shell/AppContext.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace studylab::ui {

class FlowLayout;
class MarkdownView;
class NoteEditor;
class SegmentedControl;

/// Espace de notes (05-maquettes §5) : arborescence par dossiers virtuels et tags, éditeur Markdown avec
/// aperçu (Source / Partagé / Aperçu), panneau de contexte (sources, rétroliens, plan, liens non résolus).
/// Le texte est enregistré 0,7 s après la dernière frappe, en changeant de note et à la fermeture.
class NotesPage : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Split, Preview };

    explicit NotesPage(AppContext& ctx, QWidget* parent = nullptr);
    ~NotesPage() override;

    /// Ouvre une note (enregistre d'abord la note en cours).
    bool openNote(qint64 id);
    /// Crée une note du type donné dans le dossier courant et l'ouvre, titre sélectionné.
    qint64 newNote(NoteType type, const QString& title = {});
    /// Ouvre la note désignée par un [[lien]] ; la crée si elle n'existe pas encore.
    qint64 followLink(const QString& target);
    /// Enregistre tout de suite le texte en attente. false si l'enregistrement a échoué.
    bool saveNow();

    qint64 currentNoteId() const { return m_currentId; }
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    bool isDirty() const { return m_dirty; }

    // Accès pour les tests
    QTreeWidget* tree() const { return m_tree; }
    NoteEditor* editor() const { return m_editor; }
    QLineEdit* titleEdit() const { return m_title; }
    QLineEdit* filterEdit() const { return m_filter; }
    MarkdownView* preview() const { return m_preview; }
    QToolButton* typeButton() const { return m_typeButton; }
    QLabel* saveStateLabel() const { return m_saveState; }
    QList<QPushButton*> backlinkButtons() const { return m_backlinkButtons; }
    QList<QPushButton*> outlineButtons() const { return m_outlineButtons; }
    QList<QPushButton*> createLinkButtons() const { return m_createButtons; }
    QList<QPushButton*> sourceButtons() const { return m_sourceButtons; }
    QList<QToolButton*> tagButtons() const { return m_tagButtons; }
    QLabel* backlinkCountLabel() const { return m_backlinkCount; }
    /// Notes visibles dans l'arborescence (après filtre), dans l'ordre d'affichage.
    QList<qint64> visibleNoteIds() const;
    /// Insère une image comme si elle venait d'être collée.
    bool insertImage(const QByteArray& bytes, const QString& extension);
    /// Recherche les notes d'un tag (clic sur un tag) ; vide = tous.
    void setTagFilter(const QString& tag);
    void deleteNote(qint64 id);
    /// Sous-menus « Matières » et « Collections » d'une note (cocher rattache, décocher détache).
    void addOrganizeMenus(QMenu* menu, qint64 noteId);
    void undoDelete();
    QFrame* undoBar() const { return m_undoBar; }

signals:
    void noteOpened(qint64 id);
    /// Ouvrir un document de la bibliothèque dans le lecteur ([[lien]] vers un document, source).
    void openDocumentRequested(qint64 documentId);
    /// Ouvrir le passage d'une source (annotation ou commentaire).
    void openItemRequested(qint64 documentId, bool comment, qint64 itemId);
    /// Lien « studylab://open?… » cliqué dans l'aperçu.
    void appLinkActivated(const QString& link);
    /// Citation [[@clé]] cliquée dans l'aperçu.
    void citationActivated(const QString& citekey);

protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildNav();
    void buildEditor();
    void buildContext();
    void refreshTree();
    void refreshTags();
    void refreshContext();
    void refreshOutline();
    void refreshPreview();
    void pushPreviewTheme();
    void showEmpty(bool empty);
    void setSaveState(const QString& text, bool error = false);
    void markDirty();
    void loadNote(const NoteRecord& note);
    QString currentFolder() const;
    void showTreeMenu(const QPoint& pos);
    void moveNote(qint64 id);
    void renameFolder(const QString& folder);
    void jumpToLine(int line);
    QJsonObject previewPayload() const;
    QString imageData(const QString& relPath) const;

    AppContext& m_ctx;
    mutable ReferenceService m_refs;  // lectures depuis previewPayload() const
    qint64 m_currentId = 0;
    NoteRecord m_current;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_splitRestored = false;
    Mode m_mode = Mode::Split;
    QString m_tagFilter;
    QSet<QString> m_collapsedFolders;
    qint64 m_deletedId = 0;
    mutable QHash<QString, QString> m_imageCache;

    QTimer m_saveTimer;
    QTimer m_previewTimer;
    QTimer m_contextTimer;
    QTimer m_undoTimer;

    // Navigation
    QFrame* m_nav = nullptr;
    QToolButton* m_newButton = nullptr;
    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_tagsTitle = nullptr;
    QWidget* m_tagHost = nullptr;
    FlowLayout* m_tagFlow = nullptr;
    QList<QToolButton*> m_tagButtons;
    QFrame* m_undoBar = nullptr;
    QLabel* m_undoText = nullptr;

    // Éditeur
    QStackedWidget* m_center = nullptr;
    QWidget* m_emptyState = nullptr;
    QWidget* m_editorPane = nullptr;
    QToolButton* m_typeButton = nullptr;
    QLabel* m_saveState = nullptr;
    SegmentedControl* m_modeControl = nullptr;
    QLineEdit* m_title = nullptr;
    QSplitter* m_split = nullptr;
    NoteEditor* m_editor = nullptr;
    MarkdownView* m_preview = nullptr;

    // Contexte
    QFrame* m_context = nullptr;
    QLabel* m_backlinkCount = nullptr;
    QVBoxLayout* m_backlinkList = nullptr;
    QWidget* m_sourcesSection = nullptr;
    QVBoxLayout* m_sourceList = nullptr;
    QVBoxLayout* m_outlineList = nullptr;
    QVBoxLayout* m_unresolvedList = nullptr;
    QLabel* m_backlinkEmpty = nullptr;
    QLabel* m_outlineEmpty = nullptr;
    QLabel* m_unresolvedTitle = nullptr;
    QList<QPushButton*> m_backlinkButtons;
    QList<QPushButton*> m_outlineButtons;
    QList<QPushButton*> m_createButtons;
    QList<QPushButton*> m_sourceButtons;
    QList<QWidget*> m_contextRows;
};

}  // namespace studylab::ui
