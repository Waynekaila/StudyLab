#include "services/study/StudyService.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace studylab {

namespace {
constexpr int kLearnAheadSecs = 20 * 60;

QString iso(const QDateTime& dt) { return dt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")); }
}  // namespace

StudyService::StudyService(db::Database& db, QObject* parent)
    : QObject(parent), m_repo(db), m_sm2(std::make_unique<Sm2Scheduler>())
{
}

StudyService::~StudyService() = default;

QDateTime StudyService::now() const
{
    return m_clock ? m_clock().toUTC() : QDateTime::currentDateTimeUtc();
}

QDateTime StudyService::dayStart(const QDateTime& utc)
{
    const QDateTime local = utc.toLocalTime();
    QDateTime start(local.date(), QTime(4, 0));
    if (local < start)
        start = QDateTime(local.date().addDays(-1), QTime(4, 0));
    return start.toUTC();
}

const Scheduler& StudyService::schedulerFor(const Deck&) const
{
    return *m_sm2;  // FSRS : V2-2
}

DeckSummary StudyService::summarize(const Deck& deck, const db::DeckCounts& counts) const
{
    DeckSummary s;
    s.deck = deck;
    s.counts = counts;
    s.learning = counts.learningDue;
    s.reviews = std::clamp(deck.reviewsPerDay - counts.reviewsDoneToday, 0, counts.reviewDue);
    s.newToday = std::clamp(deck.newPerDay - counts.newDoneToday, 0, counts.fresh);
    return s;
}

Result<DeckSummary> StudyService::deckSummary(qint64 deckId)
{
    auto deck = m_repo.deck(deckId);
    if (!deck)
        return deck.error();
    const QDateTime t = now();
    const QDateTime start = dayStart(t);
    auto counts = m_repo.counts(deckId, iso(t), iso(start), iso(start.addDays(1)));
    if (!counts)
        return counts.error();
    return summarize(deck.value(), counts.value());
}

Result<QList<DeckSummary>> StudyService::decks()
{
    auto list = m_repo.decks();
    if (!list)
        return list.error();
    QList<DeckSummary> out;
    for (const Deck& d : list.value()) {
        auto s = deckSummary(d.id);
        if (!s)
            return s.error();
        out << s.value();
    }
    return out;
}

Result<Deck> StudyService::createDeck(const QString& name, qint64 subjectId)
{
    const QString n = name.simplified();
    if (n.isEmpty())
        return Error::make(Error::Code::InvalidArgument, tr("Le paquet doit avoir un nom."));
    Deck d;
    d.name = n;
    d.subjectId = subjectId;
    auto r = m_repo.insertDeck(d);
    if (r)
        emit changed();
    return r;
}

Result<Deck> StudyService::updateDeck(const Deck& deck)
{
    Deck d = deck;
    d.name = d.name.simplified();
    if (d.name.isEmpty())
        return Error::make(Error::Code::InvalidArgument, tr("Le paquet doit avoir un nom."));
    if (auto r = m_repo.updateDeck(d); !r)
        return r.error();
    emit changed();
    return d;
}

Result<void> StudyService::removeDeck(qint64 deckId)
{
    auto r = m_repo.removeDeck(deckId);
    if (r)
        emit changed();
    return r;
}

Result<QList<Card>> StudyService::addCards(qint64 deckId, CardType type, const QString& front, const QString& back,
                                           CardSource source, qint64 sourceId, bool withReversed)
{
    if (front.trimmed().isEmpty())
        return Error::make(Error::Code::InvalidArgument, tr("La question (recto) est vide."));
    if (type != CardType::Cloze && back.trimmed().isEmpty())
        return Error::make(Error::Code::InvalidArgument, tr("La réponse (verso) est vide."));
    if (auto d = m_repo.deck(deckId); !d)
        return d.error();
    Card base;
    base.deckId = deckId;
    base.type = type;
    base.front = front.trimmed();
    base.back = back.trimmed();
    base.sourceType = sourceId > 0 ? source : CardSource::None;
    base.sourceId = sourceId > 0 ? sourceId : 0;
    QList<Card> cards;
    if (type == CardType::Cloze) {
        const QList<int> numbers = clozeNumbers(base.front);
        if (numbers.isEmpty())
            return Error::make(Error::Code::InvalidArgument,
                               tr("Aucun trou dans le texte : entourez un passage de {{c1::…}}."));
        for (int n : numbers) {
            Card c = base;
            c.ordinal = n;
            cards << c;
        }
    } else {
        cards << base;
        if (withReversed && type == CardType::Basic) {
            Card r = base;
            r.type = CardType::Reversed;
            cards << r;
        }
    }
    auto r = m_repo.insertCards(cards);
    if (r)
        emit changed();
    return r;
}

Result<Card> StudyService::updateCard(const Card& card)
{
    Card c = card;
    c.front = c.front.trimmed();
    c.back = c.back.trimmed();
    if (c.front.isEmpty())
        return Error::make(Error::Code::InvalidArgument, tr("La question (recto) est vide."));
    if (c.type == CardType::Cloze) {
        const QList<int> numbers = clozeNumbers(c.front);
        if (numbers.isEmpty())
            return Error::make(Error::Code::InvalidArgument,
                               tr("Aucun trou dans le texte : entourez un passage de {{c1::…}}."));
        if (!numbers.contains(c.ordinal))
            c.ordinal = numbers.first();
    } else {
        c.ordinal = 0;
        if (c.back.isEmpty())
            return Error::make(Error::Code::InvalidArgument, tr("La réponse (verso) est vide."));
    }
    if (auto r = m_repo.updateCard(c); !r)
        return r.error();
    emit changed();
    return c;
}

Result<void> StudyService::removeCard(qint64 cardId)
{
    auto r = m_repo.removeCard(cardId);
    if (r)
        emit changed();
    return r;
}

Result<void> StudyService::setSuspended(qint64 cardId, bool suspended)
{
    auto r = m_repo.setSuspended(cardId, suspended);
    if (r)
        emit changed();
    return r;
}

Result<QList<db::CardRow>> StudyService::cards(qint64 deckId) { return m_repo.cards(deckId); }

Result<db::CardRow> StudyService::card(qint64 cardId) { return m_repo.card(cardId); }

Result<db::CardSourceInfo> StudyService::source(const Card& card) { return m_repo.source(card.sourceType, card.sourceId); }

Result<std::optional<db::CardRow>> StudyService::nextCard(qint64 deckId)
{
    auto summary = deckSummary(deckId);
    if (!summary)
        return summary.error();
    const QDateTime t = now();
    const QDateTime end = dayStart(t).addDays(1);
    const DeckSummary& s = summary.value();
    if (s.learning > 0) {
        auto l = m_repo.learning(deckId, iso(t), 1);
        if (!l)
            return l.error();
        if (!l.value().isEmpty())
            return std::optional<db::CardRow>(l.value().first());
    }
    if (s.reviews > 0) {
        auto r = m_repo.reviewsDue(deckId, iso(end), 1);
        if (!r)
            return r.error();
        if (!r.value().isEmpty())
            return std::optional<db::CardRow>(r.value().first());
    }
    if (s.newToday > 0) {
        auto n = m_repo.fresh(deckId, 1);
        if (!n)
            return n.error();
        if (!n.value().isEmpty())
            return std::optional<db::CardRow>(n.value().first());
    }
    auto ahead = m_repo.learning(deckId, iso(t.addSecs(kLearnAheadSecs)), 1);
    if (!ahead)
        return ahead.error();
    if (!ahead.value().isEmpty())
        return std::optional<db::CardRow>(ahead.value().first());
    return std::optional<db::CardRow>();
}

Result<ScheduleState> StudyService::answer(qint64 cardId, Rating rating, qint64 elapsedMs)
{
    auto row = m_repo.card(cardId);
    if (!row)
        return row.error();
    auto deck = m_repo.deck(row.value().card.deckId);
    if (!deck)
        return deck.error();
    const QDateTime t = now();
    const ScheduleState before = row.value().state;
    const ScheduleState after = schedulerFor(deck.value()).next(before, rating, t);
    if (auto r = m_repo.recordReview(cardId, before, after, rating, elapsedMs, iso(t)); !r)
        return r.error();
    emit changed();
    return after;
}

QList<qint64> StudyService::preview(const db::CardRow& row) const
{
    auto deck = m_repo.deck(row.card.deckId);
    return schedulerFor(deck ? deck.value() : Deck{}).previewSeconds(row.state, now());
}

Result<DayStats> StudyService::stats(qint64 deckId)
{
    DayStats st;
    const QDateTime t = now();
    const QDateTime start = dayStart(t);
    QList<DeckSummary> summaries;
    if (deckId > 0) {
        auto s = deckSummary(deckId);
        if (!s)
            return s.error();
        summaries << s.value();
    } else {
        auto all = decks();
        if (!all)
            return all.error();
        summaries = all.value();
    }
    for (const DeckSummary& s : summaries) {
        st.remaining += s.due();
        st.newDone += s.counts.newDoneToday;
        st.newLimit += std::min(s.deck.newPerDay, s.counts.newDoneToday + s.counts.fresh);
    }
    auto today = m_repo.reviewsBetween(deckId, iso(start), iso(start.addDays(1)));
    if (!today)
        return today.error();
    st.reviewed = today.value().first;
    if (st.reviewed > 0)
        st.successRate = double(today.value().second) / st.reviewed;
    for (int i = 6; i >= 0; --i) {
        const QDateTime from = start.addDays(-i);
        auto day = m_repo.reviewsBetween(deckId, iso(from), iso(from.addDays(1)));
        if (!day)
            return day.error();
        st.lastDays << day.value().first;
    }
    return st;
}

int StudyService::dueTotal()
{
    auto all = decks();
    if (!all)
        return 0;
    int n = 0;
    for (const DeckSummary& s : all.value())
        n += s.due();
    return n;
}

QString formatInterval(qint64 seconds)
{
    const QLocale fr(QLocale::French, QLocale::France);
    const auto tr = [](const char* s) { return QCoreApplication::translate("StudyService", s); };
    if (seconds < 60)
        return tr("< 1 min");
    if (seconds < 3600)
        return tr("%1 min").arg(qRound(seconds / 60.0));
    if (seconds < 86400)
        return tr("%1 h").arg(qRound(seconds / 3600.0));
    const double days = seconds / 86400.0;
    if (days < 30.5)
        return tr("%1 j").arg(qRound(days));
    if (days < 365)
        return tr("%1 mois").arg(fr.toString(std::round(days / 30.4 * 10) / 10, 'g', 3));
    const double years = std::round(days / 365 * 10) / 10;
    return (years < 2 ? tr("%1 an") : tr("%1 ans")).arg(fr.toString(years, 'g', 3));
}

}  // namespace studylab
