#pragma once

#include "core/domain/Comment.h"

#include <QFrame>
#include <QJsonObject>
#include <QList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace studylab {
class SettingsService;
}

namespace studylab::ui {

class MarkdownView;
class SegmentedControl;
class ThemeManager;

/// Ce sur quoi porte un nouveau commentaire (voir CommentAnchor).
struct CommentTarget {
    CommentAnchor anchor = CommentAnchor::Free;
    qint64 annotationId = 0;
    QJsonObject locator;      ///< passage ou page
    int unitIndex = -1;
    QString quote;            ///< passage cité
    QString where;            ///< « p. 12 », « 42 % »
    AnnotationColor color = AnnotationColor::Yellow;

    QJsonObject toJson() const;
    static CommentTarget fromJson(const QJsonObject& o);
};

/// Volet Commentaires du lecteur (05-maquettes §3 et §4) : cartes rendues en Markdown avec formules
/// KaTeX, filtres (tous, questions, explications, page ou chapitre courant), recherche, et éditeur
/// Markdown « Écrire / Aperçu » avec type de commentaire et rattachement (document, page, passage,
/// annotation). Le brouillon est enregistré 2 s après la dernière frappe et à la perte du focus :
/// il survit à une fermeture brutale.
class CommentsPane : public QFrame {
    Q_OBJECT
public:
    enum class Filter { All, Questions, Explanations, CurrentUnit };

    CommentsPane(ThemeManager& theme, SettingsService& settings, qint64 documentId, QWidget* parent = nullptr);
    ~CommentsPane() override;

    void setComments(const QList<CommentRecord>& comments);
    void setCurrentUnit(int unit);
    void setUnitIsChapter(bool chapter);
    void setActive(qint64 id, bool scroll = true);
    qint64 active() const { return m_active; }

    /// Prépare un nouveau commentaire sur la cible et place le curseur dans l'éditeur.
    void startComment(const CommentTarget& target);
    /// Ouvre un commentaire existant dans l'éditeur.
    void editComment(const CommentRecord& comment);
    /// Abandonne la saisie en cours (brouillon effacé).
    void cancelEditing();
    qint64 editingId() const { return m_editingId; }
    const CommentTarget& target() const { return m_target; }
    CommentKind kind() const;
    void setKind(CommentKind kind);
    QString text() const;
    /// Fournit la cible « cette page / ce chapitre » (bouton de l'éditeur).
    void setPositionTargetProvider(std::function<CommentTarget()> provider) { m_positionTarget = std::move(provider); }

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    void showUndo(const QString& message);
    void hideUndo();

    /// Enregistre le brouillon immédiatement (appelé aussi à la fermeture).
    void saveDraft();
    static QString draftKey(qint64 documentId);

    // Accès pour les tests
    MarkdownView* listView() const { return m_view; }
    MarkdownView* previewView() const { return m_preview; }
    QPlainTextEdit* editor() const { return m_editor; }
    SegmentedControl* modeControl() const { return m_mode; }
    SegmentedControl* kindControl() const { return m_kind; }
    QPushButton* submitButton() const { return m_submit; }
    QPushButton* cancelButton() const { return m_cancel; }
    QLabel* targetLabel() const { return m_targetLabel; }
    QToolButton* detachButton() const { return m_detach; }
    QToolButton* pageButton() const { return m_pageTarget; }
    QToolButton* filterButton(Filter f) const;
    QLineEdit* searchEdit() const { return m_search; }
    QToolButton* searchToggle() const { return m_searchToggle; }
    QLabel* countLabel() const { return m_count; }
    QPushButton* undoButton() const { return m_undo; }
    /// Identifiants des commentaires affichés après filtrage, dans l'ordre.
    QList<qint64> visibleIds() const { return m_visible; }
    /// Crée la vue Web de la liste si elle n'existe pas encore (affichage du volet).
    void ensureView();

signals:
    /// Saisie validée : nouvelle (editingId == 0) ou modification.
    void submitted(qint64 editingId, const studylab::ui::CommentTarget& target, studylab::CommentKind kind,
                   const QString& body);
    void commentActivated(qint64 id);
    void editRequested(qint64 id);
    void deleteRequested(qint64 id);
    void menuRequested(qint64 id, QPoint globalPos);
    void linkActivated(const QUrl& url);
    void undoRequested();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void refreshIcons();
    void pushTheme(MarkdownView* view);
    void updateTarget();
    void updatePreview();
    void updateSubmit();
    /// Éditeur au repos (vide, sans focus) : une ligne ; il se déploie dès qu'on écrit.
    void updateComposer();
    void restoreDraft();
    void clearDraft();
    void submit();
    QJsonObject cardFor(const CommentRecord& c) const;
    QString emptyText() const;

    ThemeManager& m_theme;
    bool m_composerActive = true;
    bool m_composerInit = false;
    SettingsService& m_settings;
    qint64 m_documentId;
    QList<CommentRecord> m_all;
    QList<qint64> m_visible;
    int m_currentUnit = 0;
    bool m_unitIsChapter = false;
    qint64 m_active = 0;
    Filter m_filter = Filter::All;
    bool m_collapsed = false;
    bool m_restoring = false;

    qint64 m_editingId = 0;
    CommentTarget m_target;
    std::function<CommentTarget()> m_positionTarget;

    QLabel* m_title;
    QLabel* m_count;
    QToolButton* m_searchToggle;
    QToolButton* m_collapse;
    QWidget* m_body;
    QList<QToolButton*> m_filters;
    QLineEdit* m_search;
    QStackedWidget* m_listHost;
    QLabel* m_loading;
    MarkdownView* m_view = nullptr;
    QFrame* m_undoBar;
    QLabel* m_undoText;
    QPushButton* m_undo;
    QTimer m_undoTimer;
    QTimer m_searchDebounce;

    QFrame* m_editorFrame;
    QLabel* m_targetIcon;
    QLabel* m_targetLabel;
    QToolButton* m_detach;
    QToolButton* m_pageTarget;
    SegmentedControl* m_mode;
    QStackedWidget* m_editorStack;
    QPlainTextEdit* m_editor;
    MarkdownView* m_preview = nullptr;
    SegmentedControl* m_kind;
    QPushButton* m_cancel;
    QPushButton* m_submit;
    QTimer m_draftTimer;
};

}  // namespace studylab::ui

Q_DECLARE_METATYPE(studylab::ui::CommentTarget)
