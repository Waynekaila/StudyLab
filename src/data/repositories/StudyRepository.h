#pragma once

#include "core/Result.h"
#include "core/domain/Card.h"

#include <QList>
#include <QPair>

namespace studylab::db {

class Database;

/// Carte et son état de planification.
struct CardRow {
    Card card;
    ScheduleState state;
};

/// Compteurs d'un paquet à un instant donné (bornes en ISO UTC).
struct DeckCounts {
    int total = 0;          ///< cartes actives (non supprimées)
    int suspended = 0;
    int fresh = 0;          ///< nouvelles, non suspendues
    int learningDue = 0;    ///< en apprentissage, dues maintenant
    int reviewDue = 0;      ///< à réviser, dues avant la fin du jour
    int newDoneToday = 0;   ///< nouvelles vues pour la première fois aujourd'hui
    int reviewsDoneToday = 0;  ///< révisions (hors première vue) aujourd'hui
};

/// Origine lisible d'une carte : passage annoté (document, page) ou note.
struct CardSourceInfo {
    CardSource type = CardSource::None;
    qint64 documentId = 0;  ///< annotation, document
    qint64 itemId = 0;      ///< annotation ou note
    QString quote;          ///< passage annoté
    QString label;          ///< « Çengel · p. 298 », titre de la note
};

/// Ligne du journal des révisions d'une carte.
struct ReviewEntry {
    QDateTime at;        ///< UTC
    Rating rating = Rating::Good;
    qint64 elapsedMs = 0;
    QJsonObject before;  ///< ScheduleState::toJson()
    QJsonObject after;
};

/// Paquets, cartes, état du planificateur et journal des révisions (tables decks, cards, card_state,
/// card_reviews ; activité « reviewed »).
class StudyRepository {
public:
    explicit StudyRepository(Database& db);

    Result<QList<Deck>> decks();
    Result<Deck> deck(qint64 id);
    Result<Deck> insertDeck(Deck deck);
    Result<void> updateDeck(const Deck& deck);
    Result<void> removeDeck(qint64 id);

    Result<QList<CardRow>> cards(qint64 deckId);
    Result<CardRow> card(qint64 id);
    /// Insère les cartes et leur état « nouvelle », en une transaction.
    Result<QList<Card>> insertCards(const QList<Card>& cards);
    Result<void> updateCard(const Card& card);
    Result<void> setSuspended(qint64 id, bool suspended);
    Result<void> removeCard(qint64 id);

    Result<DeckCounts> counts(qint64 deckId, const QString& nowIso, const QString& dayStartIso, const QString& dayEndIso);
    /// Prochaines cartes candidates, dans l'ordre : apprentissage dû avant `learnAheadIso`, révisions dues avant
    /// `dayEndIso`, nouvelles (ordre de création). Au plus `limit` de chaque.
    Result<QList<CardRow>> learning(qint64 deckId, const QString& untilIso, int limit);
    Result<QList<CardRow>> reviewsDue(qint64 deckId, const QString& untilIso, int limit);
    Result<QList<CardRow>> fresh(qint64 deckId, int limit);

    /// Enregistre une réponse : nouvel état, journal (états avant/après en JSON), activité. Une transaction.
    Result<void> recordReview(qint64 cardId, const ScheduleState& before, const ScheduleState& after, Rating rating,
                              qint64 elapsedMs, const QString& atIso);
    /// Révisions entre deux instants (ISO UTC) : nombre, réussies (réponse > 1), par paquet (0 : tous).
    Result<QPair<int, int>> reviewsBetween(qint64 deckId, const QString& fromIso, const QString& toIso);

    /// Journal d'une carte, du plus ancien au plus récent.
    Result<QList<ReviewEntry>> reviews(qint64 cardId);

    Result<CardSourceInfo> source(CardSource type, qint64 id);

private:
    Database& m_db;
};

}  // namespace studylab::db
