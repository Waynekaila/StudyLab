#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Document.h"

#include <QList>
#include <QWidget>

#include <functional>
#include <optional>

class QHBoxLayout;
class QToolButton;

namespace studylab {
class AnnotationService;
class LibraryService;
class SettingsService;
struct AppContext;
}

namespace studylab::ui {

class AnnotationPopup;
class AnnotationsPane;
class ThemeManager;

/// Document ouvert dans un onglet de l'espace Lecture (PDF ou EPUB).
/// Porte ce que les lecteurs ont en commun : volet Annotations, barre flottante, couleur courante,
/// suppression annulable, menu contextuel d'une annotation, export Markdown, mode sans distraction.
class ReaderTab : public QWidget {
    Q_OBJECT
public:
    ReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent = nullptr);
    ~ReaderTab() override;

    qint64 documentId() const { return m_documentId; }
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

signals:
    void focusModeToggled(bool on);
    void fullScreenToggled();
    void loadFailed(const QString& message);

protected:
    /// À appeler une fois m_pane et m_popup créés : relie volet, barre flottante, service et raccourcis.
    void initAnnotations();
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
    bool m_focusMode = false;

    QList<AnnotationRecord> m_records;
    QList<qint64> m_deleted;  ///< pile d'annulation
    AnnotationColor m_color = AnnotationColor::Yellow;
    NoteEditor m_noteEditor;
    AnnotationsPane* m_pane = nullptr;
    AnnotationPopup* m_popup = nullptr;
    qint64 m_popupAnnotation = 0;
    QList<QToolButton*> m_colorButtons;
    QToolButton* m_paneToggle = nullptr;
};

}  // namespace studylab::ui
