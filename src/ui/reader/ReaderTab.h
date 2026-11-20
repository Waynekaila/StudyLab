#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "ui/reader/CommentsPane.h"

#include <QList>
#include <QWidget>

#include <functional>
#include <optional>

class QHBoxLayout;
class QSplitter;
class QToolButton;

namespace studylab {
class AnnotationService;
class CommentService;
class LibraryService;
class SettingsService;
struct AppContext;
}

namespace studylab::ui {

class AnnotationPopup;
class AnnotationsPane;
class ReadingClock;
class ThemeManager;

/// Document ouvert dans un onglet de l'espace Lecture (PDF ou EPUB).
/// Porte ce que les lecteurs ont en commun : volets Annotations et Commentaires, barre flottante,
/// couleur courante, suppression annulable, menus contextuels, export Markdown, mode sans distraction.
class ReaderTab : public QWidget {
    Q_OBJECT
public:
    ReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent = nullptr);
    ~ReaderTab() override;

    qint64 documentId() const { return m_documentId; }
    /// Temps de lecture et pages vues de l'onglet (V2-4).
    ReadingClock* readingClock() const { return m_clock; }
    QString title() const { return m_record.title; }
    virtual bool isLoaded() const = 0;
    /// Mode sans distraction : seule la page reste visible.
    virtual void setFocusMode(bool on) = 0;
    bool focusMode() const { return m_focusMode; }
    /// Enregistre immédiatement la position (fermeture de l'onglet ou de l'application).
    virtual void savePosition() = 0;

    // Annotations
    using NoteEditor = std::function<std::optional<QString>(const QString& quote, const QString& current)>;
    /// Saisie des notes fournie à l'avance (tests) ; sinon une boîte de dialogue.
    void setNoteEditor(NoteEditor editor) { m_noteEditor = std::move(editor); }
    /// Annote la sélection courante puis efface la sélection.
    virtual Result<QList<AnnotationRecord>> annotateSelection(AnnotationType type, AnnotationColor color) = 0;
    virtual void toggleBookmark() = 0;
    virtual void revealAnnotation(qint64 id) = 0;
    /// Titre du chapitre (sommaire) d'une page (PDF) ou d'une section (EPUB), vide sans sommaire.
    virtual QString chapterOf(int unit) const = 0;
    void deleteAnnotation(qint64 id);
    /// Rétablit la dernière annotation supprimée (Ctrl+Z).
    bool undoDelete();
    void editNote(qint64 id);
    // Commentaires
    /// Commente la sélection courante (passage, sans annotation visible).
    void commentSelection();
    /// Ouvre l'éditeur sur le commentaire de l'annotation (le crée s'il n'existe pas).
    void commentAnnotation(qint64 annotationId);
    /// Nouveau commentaire rattaché à la page (PDF) ou à la position (EPUB) courante.
    void commentHere();
    void editComment(qint64 id);
    void deleteComment(qint64 id);
    void revealComment(qint64 id);
    const QList<CommentRecord>& comments() const { return m_commentRecords; }
    /// Affiche une annotation ou un commentaire dès que le document est prêt (résultat de recherche).
    void revealItem(bool comment, qint64 id);
    /// Va à la page (PDF) ou à la section (EPUB) et y met en évidence `query` dans la barre de recherche
    /// (résultat de la recherche plein texte). Différé jusqu'à ce que le document soit prêt.
    void revealPassage(int unit, const QString& query);
    CommentsPane* commentsPane() const { return m_comments; }
    QSplitter* sidePanel() const { return m_sidePanel; }

    /// Exporte les annotations du document en Markdown dans le dossier donné ; renvoie le dossier créé.
    Result<QString> exportMarkdown(const QString& directory);
    const QList<AnnotationRecord>& annotations() const { return m_records; }
    AnnotationColor currentColor() const { return m_color; }
    void setCurrentColor(AnnotationColor color);

    AnnotationsPane* annotationsPane() const { return m_pane; }
    AnnotationPopup* popup() const { return m_popup; }
    QToolButton* colorButton(AnnotationColor color) const;
    QToolButton* paneToggle() const { return m_paneToggle; }

    /// Bouton de barre d'outils du lecteur (style « readerTool »).
    static QToolButton* makeToolButton(const QString& tooltip, QWidget* parent, bool checkable = false);

    /// Notes tirées de la lecture (V1) : créées dans « Notes de lecture » avec leurs sources. 0 si échec.
    qint64 createNoteFromAnnotations(const QList<qint64>& annotationIds);
    qint64 createNoteFromComment(qint64 commentId);
    qint64 createReadingNote();

signals:
    /// Une note vient d'être créée depuis la lecture : l'ouvrir dans l'espace de notes.
    void noteCreated(qint64 noteId);
    /// « Créer une carte… » sur une annotation : carte mémoire préremplie avec le passage.
    void cardRequested(qint64 annotationId, const QString& front, const QString& back);
    void focusModeToggled(bool on);
    /// Un commentaire ou une annotation a été créé, modifié ou supprimé.
    void contentChanged();
    void fullScreenToggled();
    void loadFailed(const QString& message);

protected:
    /// Volets latéraux (Annotations au-dessus, Commentaires au-dessous, séparés par une poignée).
    QWidget* createSidePanes(bool unitIsChapter);
    /// À appeler une fois les volets et m_popup créés : relie volets, barre flottante, services et raccourcis.
    void initAnnotations();
    /// Cible d'un commentaire sur la sélection courante (aucune : pas de sélection).
    virtual std::optional<CommentTarget> selectionTarget() const { return std::nullopt; }
    /// Cible d'un commentaire sur la page ou la position courante.
    virtual CommentTarget positionTarget() const = 0;
    /// Affiche le passage ou la page d'un commentaire.
    virtual void revealLocator(const QJsonObject& locator) = 0;
    void reloadComments();
    /// Document affiché et navigable (l'EPUB s'ouvre de façon asynchrone).
    virtual bool isReady() const { return isLoaded(); }
    /// À appeler quand le document devient prêt : applique revealItem() en attente.
    void flushPendingReveal();
    /// Implémentation de revealPassage(), document prêt.
    virtual void showPassage(int unit, const QString& query) = 0;
    void submitComment(qint64 editingId, const CommentTarget& target, CommentKind kind, const QString& body);
    void showCommentMenu(qint64 id, QPoint globalPos);
    /// Pastilles de couleur et bouton du volet, ajoutés à la barre d'outils.
    void addColorButtons(QHBoxLayout* layout, QWidget* parent);
    QToolButton* addPaneToggle(QHBoxLayout* layout, QWidget* parent);
    void refreshColorIcons();

    /// Redessine les annotations dans la vue et met à jour le volet.
    virtual void pushMarks() = 0;
    virtual void copySelection() = 0;
    virtual void showEditPopup(qint64 id) = 0;
    virtual void updateBookmarkButton() {}
    /// Une annotation vient d'être supprimée : la vue retire la barre flottante si besoin.
    virtual void annotationRemoved(qint64) {}

    void reloadAnnotations();
    void reportError(const Error& error);
    std::optional<QString> askNote(const QString& quote, const QString& current);
    void setPaneVisible(bool on);
    void showAnnotationMenu(qint64 id, QPoint globalPos);
    void exportWithDialog();

    AppContext& m_ctx;
    LibraryService& m_library;
    AnnotationService& m_annotations;
    SettingsService& m_settings;
    ThemeManager& m_theme;
    qint64 m_documentId;
    DocumentRecord m_record;
    ReadingClock* m_clock = nullptr;
    bool m_focusMode = false;

    QList<AnnotationRecord> m_records;
    struct Deleted {
        bool comment = false;
        qint64 id = 0;
    };
    QList<Deleted> m_deleted;  ///< pile d'annulation (annotations et commentaires)
    CommentService& m_commentService;
    QList<CommentRecord> m_commentRecords;
    CommentsPane* m_comments = nullptr;
    QSplitter* m_sidePanel = nullptr;
    std::optional<std::pair<bool, qint64>> m_pendingReveal;
    std::optional<std::pair<int, QString>> m_pendingPassage;
    AnnotationColor m_color = AnnotationColor::Yellow;
    NoteEditor m_noteEditor;
    AnnotationsPane* m_pane = nullptr;
    AnnotationPopup* m_popup = nullptr;
    qint64 m_popupAnnotation = 0;
    QList<QToolButton*> m_colorButtons;
    QToolButton* m_paneToggle = nullptr;
};

}  // namespace studylab::ui
