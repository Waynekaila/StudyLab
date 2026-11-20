#pragma once

#include "services/activity/ActivityService.h"
#include "ui/shell/AppContext.h"

#include <QDialog>
#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace studylab::ui {

class ActivityHeatmap;
class BarChart;
class GoalsDialog;
class SegmentedControl;

/// Écran Activité (01-vision §9, V2-4) : chiffres de la période (7 jours, 30 jours, 12 mois), temps de lecture par
/// jour ou par mois, régularité sur six mois et journal filtrable (lu, annoté, commenté, noté, révisé, importé).
/// Chaque entrée du journal ouvre ce qu'elle désigne.
class ActivityPage : public QWidget {
    Q_OBJECT
public:
    enum class Period { Week, Month, Year };
    enum class Filter { All, Reading, Annotations, Comments, Notes, Reviews, Imports };

    ActivityPage(AppContext& ctx, ActivityService& activity, QWidget* parent = nullptr);

    void refresh();
    void setPeriod(Period period);
    Period period() const { return m_period; }
    void setFilter(Filter filter);
    GoalsDialog* editGoals();

    // Pour les tests
    QLabel* tileValue(int index) const { return m_tileValues.value(index); }
    QLabel* tileCaption(int index) const { return m_tileCaptions.value(index); }
    BarChart* chart() const { return m_chart; }
    ActivityHeatmap* heatmap() const { return m_heatmap; }
    QLabel* streakLabel() const { return m_streak; }
    QList<QPushButton*> journalRows() const { return m_rows; }
    QList<QLabel*> dayHeaders() const { return m_dayHeaders; }
    QLabel* emptyLabel() const { return m_empty; }
    SegmentedControl* periodControl() const { return m_periodControl; }
    QToolButton* filterChip(Filter filter) const { return m_chips.value(int(filter)); }

signals:
    void navigateRequested(const QString& pageId);
    void openDocumentRequested(qint64 documentId);
    void openItemRequested(qint64 documentId, bool comment, qint64 itemId);
    void openNoteRequested(qint64 noteId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QDate periodStart() const;
    void fillTiles();
    void fillChart();
    void fillHeatmap();
    void fillJournal();
    bool matches(const JournalEntry& e) const;
    QPushButton* journalRow(const JournalEntry& e);

    AppContext& m_ctx;
    ActivityService& m_activity;
    Period m_period = Period::Week;
    Filter m_filter = Filter::All;

    SegmentedControl* m_periodControl = nullptr;
    QList<QLabel*> m_tileValues;
    QList<QLabel*> m_tileCaptions;
    QLabel* m_chartTitle = nullptr;
    QLabel* m_chartNote = nullptr;
    BarChart* m_chart = nullptr;
    ActivityHeatmap* m_heatmap = nullptr;
    QLabel* m_streak = nullptr;
    QLabel* m_activeDays = nullptr;
    QList<QToolButton*> m_chips;
    QVBoxLayout* m_journal = nullptr;
    QList<QPushButton*> m_rows;
    QList<QLabel*> m_dayHeaders;
    QLabel* m_empty = nullptr;
    QLabel* m_more = nullptr;
};

/// Objectifs généraux : pages par semaine, minutes de lecture et cartes révisées par jour (0 = aucun).
class GoalsDialog : public QDialog {
    Q_OBJECT
public:
    GoalsDialog(AppContext& ctx, ActivityService& activity, QWidget* parent = nullptr);

    /// Enregistre ; false si l'écriture a échoué.
    bool save();

    QSpinBox* pagesBox() const { return m_pages; }
    QSpinBox* minutesBox() const { return m_minutes; }
    QSpinBox* reviewsBox() const { return m_reviews; }

private:
    ActivityService& m_activity;
    QSpinBox* m_pages;
    QSpinBox* m_minutes;
    QSpinBox* m_reviews;
    QLabel* m_error;
};

}  // namespace studylab::ui
