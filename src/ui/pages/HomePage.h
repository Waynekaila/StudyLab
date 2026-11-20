#pragma once

#include "core/domain/Document.h"
#include "ui/shell/AppContext.h"

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace studylab {
class StudyService;
}

namespace studylab::ui {

class GoalRing;
class GoalsDialog;
class ResumeCover;

/// Accueil et tableau de bord (05-maquettes §1) : reprendre la lecture, révisions du jour par matière, objectif de
/// la semaine, lectures en cours, dernières annotations, progression par matière, derniers commentaires, favoris,
/// notes récentes et état de la sauvegarde. Toutes les valeurs sont lues en base ; les statistiques détaillées sont
/// dans l'écran Activité.
class HomePage : public QWidget {
    Q_OBJECT
public:
    /// `study` peut être nul (pas de carte « Révisions du jour »).
    HomePage(AppContext& ctx, StudyService* study, QWidget* parent = nullptr);

    void refresh();
    /// Fenêtre « Objectifs » (non bloquante).
    GoalsDialog* editGoals();

    // Pour les tests
    QLabel* subtitle() const { return m_date; }
    QLabel* resumeTitle() const { return m_resumeTitle; }
    QLabel* resumePosition() const { return m_resumePosition; }
    QPushButton* continueButton() const { return m_continue; }
    QList<QPushButton*> annotationRows() const { return m_annotationRows; }
    QList<QPushButton*> commentRows() const { return m_commentRows; }
    QList<QPushButton*> readingRows() const { return m_readingRows; }
    QLabel* backupLabel() const { return m_backup; }
    QLabel* dueLabel() const { return m_due; }
    QList<QPushButton*> revisionRows() const { return m_revisionRows; }
    QPushButton* startReviewButton() const { return m_startReview; }
    GoalRing* goalRing() const { return m_ring; }
    QLabel* weekReading() const { return m_weekTime; }
    QLabel* weekTrend() const { return m_weekTrend; }
    QLabel* dailyGoals() const { return m_dailyGoals; }
    QPushButton* goalsButton() const { return m_goalsButton; }
    QList<QPushButton*> subjectRows() const { return m_subjectRows; }
    QList<QToolButton*> favoriteButtons() const { return m_favoriteButtons; }
    QList<QPushButton*> noteChips() const { return m_noteChips; }

signals:
    void navigateRequested(const QString& pageId);
    void openDocumentRequested(qint64 documentId);
    void openItemRequested(qint64 documentId, bool comment, qint64 itemId);
    void openNoteRequested(qint64 noteId);
    void importRequested();
    void newNoteRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildResume();
    QWidget* buildRevisions();
    QWidget* buildGoal();
    QWidget* buildFavorites();
    QWidget* cardHeader(const QString& title, const QString& link, const QString& pageId, QLabel** count = nullptr);
    QPushButton* makeRow(const QPixmap& icon, const QString& text, const QString& caption, double progress = -1,
                         const QColor& progressColor = {});
    void clearRows(QVBoxLayout* layout, QList<QPushButton*>& rows);
    QPixmap smallCover(const DocumentRecord& doc, int width, int height) const;
    void refreshResume();
    void refreshReading();
    void refreshRevisions();
    void refreshGoal();
    void refreshSubjects();
    void refreshActivity();
    void refreshFavorites();
    void refreshNotes();
    void refreshIfVisible();

    AppContext& m_ctx;
    StudyService* m_study;
    QLabel* m_greeting = nullptr;
    QLabel* m_date = nullptr;

    QWidget* m_resumeFilled = nullptr;
    QWidget* m_resumeEmpty = nullptr;
    ResumeCover* m_cover = nullptr;
    QLabel* m_resumeTitle = nullptr;
    QLabel* m_resumeAuthor = nullptr;
    QLabel* m_resumePosition = nullptr;
    QProgressBar* m_resumeProgress = nullptr;
    QLabel* m_resumeQuote = nullptr;
    QPushButton* m_continue = nullptr;
    qint64 m_resumeId = 0;

    QVBoxLayout* m_readingList = nullptr;
    QLabel* m_readingEmpty = nullptr;
    QList<QPushButton*> m_readingRows;

    QWidget* m_revisionsCard = nullptr;
    QLabel* m_due = nullptr;
    QLabel* m_dueCaption = nullptr;
    QVBoxLayout* m_revisionList = nullptr;
    QLabel* m_revisionEmpty = nullptr;
    QList<QPushButton*> m_revisionRows;
    QPushButton* m_startReview = nullptr;

    QWidget* m_goalCard = nullptr;
    GoalRing* m_ring = nullptr;
    QLabel* m_weekDays = nullptr;
    QLabel* m_weekTime = nullptr;
    QLabel* m_weekTrend = nullptr;
    QLabel* m_dailyGoals = nullptr;
    QPushButton* m_goalsButton = nullptr;

    QVBoxLayout* m_subjectList = nullptr;
    QLabel* m_subjectEmpty = nullptr;
    QList<QPushButton*> m_subjectRows;

    QVBoxLayout* m_annotationList = nullptr;
    QLabel* m_annotationEmpty = nullptr;
    QList<QPushButton*> m_annotationRows;
    QVBoxLayout* m_commentList = nullptr;
    QLabel* m_commentEmpty = nullptr;
    QList<QPushButton*> m_commentRows;

    QLabel* m_favoriteCount = nullptr;
    QHBoxLayout* m_favoriteRow = nullptr;
    QLabel* m_favoriteEmpty = nullptr;
    QList<QToolButton*> m_favoriteButtons;

    QHBoxLayout* m_noteRow = nullptr;
    QLabel* m_notesLabel = nullptr;
    QList<QPushButton*> m_noteChips;
    QLabel* m_backup = nullptr;
};

}  // namespace studylab::ui
