#pragma once

#include "core/domain/Card.h"
#include "data/repositories/StudyRepository.h"
#include "ui/shell/AppContext.h"

#include <QElapsedTimer>
#include <QList>
#include <QWidget>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

namespace studylab {
class StudyService;
}

namespace studylab::ui {

class CardEditorDialog;
class DayChart;
class MarkdownView;

/// Espace Révisions (V2-1) : paquets et compteurs du jour, statistiques des sept derniers jours, vue d'un paquet
/// (cartes, ajout, modification, suspension) et séance de révision (recto, verso, quatre réponses avec l'intervalle
/// suivant, origine cliquable). Raccourcis de séance : Espace, 1 à 4, E, S, Échap.
class RevisionsPage : public QWidget {
    Q_OBJECT
public:
    RevisionsPage(AppContext& ctx, StudyService& study, QWidget* parent = nullptr);

    void refresh();
    /// Affiche le paquet ; false s'il n'existe pas.
    bool showDeck(qint64 deckId);
    qint64 currentDeckId() const { return m_deckId; }
    /// Crée un paquet et l'affiche ; 0 si le nom est vide.
    qint64 createDeck(const QString& name);
    bool renameDeck(qint64 deckId, const QString& name);
    bool removeDeck(qint64 deckId);

    /// Fenêtre de saisie (non bloquante) d'une carte pour le paquet affiché, ou modification d'une carte.
    CardEditorDialog* addCard();
    CardEditorDialog* editCard(qint64 cardId);
    bool setCardSuspended(qint64 cardId, bool suspended);
    bool removeCard(qint64 cardId);

    // Séance
    /// Commence la séance du paquet affiché ; false s'il n'y a rien à voir.
    bool startSession();
    void closeSession();
    bool inSession() const { return m_inSession; }
    qint64 currentCardId() const { return m_inSession ? m_row.card.id : 0; }
    bool answerShown() const { return m_answerShown; }
    bool revealAnswer();
    bool rate(Rating rating);
    int sessionReviewed() const { return m_sessionReviewed; }

    // Accès pour les tests
    QListWidget* deckList() const { return m_deckList; }
    QPushButton* startButton() const { return m_start; }
    QPushButton* addCardButton() const { return m_addCard; }
    QTreeWidget* cardList() const { return m_cards; }
    QLabel* deckTitle() const { return m_deckName; }
    QLabel* tileValue(int index) const { return m_tiles.value(index); }
    QLabel* statValue(int index) const { return m_stats.value(index); }
    DayChart* chart() const { return m_chart; }
    MarkdownView* cardView() const { return m_view; }
    QPushButton* showAnswerButton() const { return m_showAnswer; }
    QList<QPushButton*> rateButtons() const { return m_rateButtons; }
    QLabel* rateInterval(int index) const { return m_rateIntervals.value(index); }
    QLabel* progressLabel() const { return m_progressLabel; }
    QLabel* finishedLabel() const { return m_finishedSummary; }
    QStackedWidget* center() const { return m_center; }
    QWidget* overviewPage() const { return m_overview; }
    QWidget* sessionPage() const { return m_session; }
    QWidget* finishedPage() const { return m_finished; }

signals:
    void openItemRequested(qint64 documentId, bool comment, qint64 itemId);
    void openDocumentRequested(qint64 documentId);
    void openNoteRequested(qint64 noteId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildNav();
    void buildOverview();
    void buildSession();
    void buildFinished();
    void fillDecks();
    void fillStats();
    void fillOverview();
    void showDeckMenu(const QPoint& pos);
    void showCardMenu(const QPoint& pos);
    void chooseNewDeck();
    void nextCard();
    void renderCard();
    void fitCard();
    void updateProgress();
    void openSource();
    void pushTheme();

    AppContext& m_ctx;
    StudyService& m_study;
    qint64 m_deckId = 0;
    bool m_stale = true;
    bool m_filling = false;

    // Séance
    bool m_inSession = false;
    bool m_answerShown = false;
    db::CardRow m_row;
    db::CardSourceInfo m_source;
    QElapsedTimer m_shownAt;
    int m_sessionReviewed = 0;
    int m_sessionPassed = 0;

    QListWidget* m_deckList = nullptr;
    QList<QLabel*> m_stats;
    DayChart* m_chart = nullptr;
    QStackedWidget* m_center = nullptr;
    QWidget* m_empty = nullptr;
    QWidget* m_overview = nullptr;
    QWidget* m_session = nullptr;
    QWidget* m_finished = nullptr;
    QLabel* m_deckName = nullptr;
    QLabel* m_deckInfo = nullptr;
    QList<QLabel*> m_tiles;
    QPushButton* m_start = nullptr;
    QPushButton* m_addCard = nullptr;
    QLabel* m_cardsEmpty = nullptr;
    QTreeWidget* m_cards = nullptr;
    QLabel* m_sessionDeck = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressLabel = nullptr;
    QVBoxLayout* m_cardHost = nullptr;
    MarkdownView* m_view = nullptr;
    QStackedWidget* m_actions = nullptr;
    QPushButton* m_showAnswer = nullptr;
    QList<QPushButton*> m_rateButtons;
    QList<QLabel*> m_rateIntervals;
    QLabel* m_finishedSummary = nullptr;
    QLabel* m_finishedNext = nullptr;
};

/// Histogramme des révisions des sept derniers jours (aujourd'hui en lavande).
class DayChart : public QWidget {
    Q_OBJECT
public:
    DayChart(AppContext& ctx, QWidget* parent = nullptr);
    void setValues(const QList<int>& values);
    const QList<int>& values() const { return m_values; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    AppContext& m_ctx;
    QList<int> m_values;
};

}  // namespace studylab::ui
