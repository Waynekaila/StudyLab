#pragma once

#include "core/Result.h"
#include "core/domain/Card.h"
#include "data/repositories/StudyRepository.h"
#include "services/study/Scheduler.h"

#include <QDateTime>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace studylab {

namespace db {
class Database;
}

/// Paquet et ses compteurs du jour.
struct DeckSummary {
    Deck deck;
    db::DeckCounts counts;
    int newToday = 0;      ///< nouvelles encore présentables aujourd'hui (limite du paquet)
    int learning = 0;      ///< en apprentissage, dues maintenant
    int reviews = 0;       ///< à réviser aujourd'hui (limite du paquet)
    int due() const { return newToday + learning + reviews; }
};

/// Statistiques du jour et des sept derniers jours (paquet, ou tous si 0).
struct DayStats {
    int reviewed = 0;
    int remaining = 0;
    int newDone = 0;
    int newLimit = 0;
    double successRate = -1;   ///< part des réponses autres que « À revoir », -1 sans révision
    QList<int> lastDays;       ///< révisions des 7 derniers jours, le plus ancien d'abord (aujourd'hui en dernier)
};

/// Cartes mémoire et répétition espacée (07-versions V2) : paquets, création (basique, inversée, texte à trous,
/// formule) depuis la lecture ou une note, file de révision du jour, réponses et journal complet.
///
/// La journée de révision commence à 4 h (heure locale) : une session tardive compte pour la veille. Les cartes
/// nouvelles et les révisions sont plafonnées par paquet (20 et 200 par défaut).
class StudyService : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<QDateTime()>;
    explicit StudyService(db::Database& db, QObject* parent = nullptr);
    ~StudyService() override;

    /// Horloge (UTC) : les tests avancent le temps.
    void setClock(Clock clock) { m_clock = std::move(clock); }
    QDateTime now() const;
    /// Début (4 h locale) du jour de révision qui contient `utc`, en UTC.
    static QDateTime dayStart(const QDateTime& utc);

    // Paquets
    Result<QList<DeckSummary>> decks();
    Result<DeckSummary> deckSummary(qint64 deckId);
    Result<Deck> createDeck(const QString& name, qint64 subjectId = 0);
    Result<Deck> updateDeck(const Deck& deck);
    Result<void> removeDeck(qint64 deckId);

    // Cartes
    /// Crée les cartes d'une saisie : une par trou pour un texte à trous ; deux (recto et inversée) si
    /// `withReversed` pour une carte basique.
    Result<QList<Card>> addCards(qint64 deckId, CardType type, const QString& front, const QString& back,
                                 CardSource source = CardSource::None, qint64 sourceId = 0, bool withReversed = false);
    Result<Card> updateCard(const Card& card);
    Result<void> removeCard(qint64 cardId);
    Result<void> setSuspended(qint64 cardId, bool suspended);
    Result<QList<db::CardRow>> cards(qint64 deckId);
    Result<db::CardRow> card(qint64 cardId);
    Result<db::CardSourceInfo> source(const Card& card);

    // Révision
    /// Prochaine carte du paquet, ou nullopt si la séance du jour est finie. Les cartes en apprentissage dues
    /// dans les 20 minutes sont présentées en avance quand il ne reste rien d'autre.
    Result<std::optional<db::CardRow>> nextCard(qint64 deckId);
    /// Enregistre la réponse ; renvoie le nouvel état.
    Result<ScheduleState> answer(qint64 cardId, Rating rating, qint64 elapsedMs = 0);
    /// Délais (secondes) des quatre réponses pour cette carte, maintenant.
    QList<qint64> preview(const db::CardRow& row) const;
    Result<DayStats> stats(qint64 deckId = 0);
    /// Cartes à voir aujourd'hui, tous paquets confondus (badge de la navigation).
    int dueTotal();

    /// Planificateur du paquet (SM-2 ou FSRS) avec ses réglages.
    static std::unique_ptr<Scheduler> schedulerFor(const Deck& deck);
    /// Journal des réponses d'une carte, du plus ancien au plus récent.
    Result<QList<db::ReviewEntry>> history(qint64 cardId);

signals:
    /// Paquets, cartes ou révisions modifiés.
    void changed();

private:
    DeckSummary summarize(const Deck& deck, const db::DeckCounts& counts) const;

    mutable db::StudyRepository m_repo;
    Clock m_clock;
};

/// « < 1 min », « 10 min », « 3 h », « 4 j », « 2,5 mois », « 1,2 an » (boutons de réponse).
QString formatInterval(qint64 seconds);

}  // namespace studylab
