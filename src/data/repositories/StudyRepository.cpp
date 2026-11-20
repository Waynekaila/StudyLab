#include "data/repositories/StudyRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QJsonDocument>

namespace studylab::db {

namespace {

const QString kCardColumns = QStringLiteral(
    "c.id, c.uuid, c.deck_id, c.card_type, c.front_md, c.back_md, c.ordinal, c.source_type, c.source_id, c.suspended, "
    "c.created_at, c.updated_at, s.state, s.due_at, s.interval_d, s.ease, s.reps, s.lapses, s.step "
    "FROM cards c LEFT JOIN card_state s ON s.card_id = c.id ");

QDateTime parseIso(const QString& iso)
{
    if (iso.isEmpty())
        return {};
    return QDateTime::fromString(iso, Qt::ISODate).toUTC();  // « …Z » : UTC
}

QString isoOf(const QDateTime& dt)
{
    return dt.isValid() ? dt.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")) : QString();
}

CardRow readCard(const Statement& s)
{
    CardRow r;
    r.card.id = s.columnInt64(0);
    r.card.uuid = s.columnText(1);
    r.card.deckId = s.columnInt64(2);
    r.card.type = cardTypeFromKey(s.columnText(3));
    r.card.front = s.columnText(4);
    r.card.back = s.columnText(5);
    r.card.ordinal = int(s.columnInt64(6));
    r.card.sourceType = cardSourceFromKey(s.columnText(7));
    r.card.sourceId = s.columnInt64(8);
    r.card.suspended = s.columnInt64(9) != 0;
    r.card.createdAt = s.columnText(10);
    r.card.updatedAt = s.columnText(11);
    if (!s.isNull(12)) {
        r.state.state = cardStateFromKey(s.columnText(12));
        r.state.due = parseIso(s.columnText(13));
        r.state.intervalDays = s.columnDouble(14);
        r.state.ease = s.columnDouble(15);
        r.state.reps = int(s.columnInt64(16));
        r.state.lapses = int(s.columnInt64(17));
        r.state.step = int(s.columnInt64(18));
    }
    return r;
}

Result<QList<CardRow>> readCards(Statement& stmt)
{
    QList<CardRow> out;
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << readCard(stmt);
    }
    return out;
}

Deck readDeck(const Statement& s)
{
    Deck d;
    d.id = s.columnInt64(0);
    d.uuid = s.columnText(1);
    d.name = s.columnText(2);
    d.subjectId = s.columnInt64(3);
    d.scheduler = s.columnText(4);
    d.applyConfig(QJsonDocument::fromJson(s.columnText(5).toUtf8()).object());
    d.createdAt = s.columnText(6);
    return d;
}

QString configText(const Deck& d)
{
    return QString::fromUtf8(QJsonDocument(d.configJson()).toJson(QJsonDocument::Compact));
}

}  // namespace

StudyRepository::StudyRepository(Database& db) : m_db(db) {}

// ───────────────────────── Paquets ─────────────────────────

Result<QList<Deck>> StudyRepository::decks()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, uuid, name, subject_id, scheduler, config_json, created_at FROM decks ORDER BY name COLLATE NOCASE, id"));
    if (!stmt)
        return stmt.error();
    QList<Deck> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << readDeck(stmt.value());
    }
    return out;
}

Result<Deck> StudyRepository::deck(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, uuid, name, subject_id, scheduler, config_json, created_at FROM decks WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error::make(Error::Code::NotFound, QStringLiteral("Paquet introuvable"));
    return readDeck(stmt.value());
}

Result<Deck> StudyRepository::insertDeck(Deck deck)
{
    if (deck.uuid.isEmpty())
        deck.uuid = newUuid();
    if (deck.createdAt.isEmpty())
        deck.createdAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO decks(uuid, name, subject_id, scheduler, config_json, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, deck.uuid).bind(2, deck.name).bind(5, configText(deck)).bind(6, deck.createdAt).bind(4, deck.scheduler);
    if (deck.subjectId > 0)
        s.bind(3, deck.subjectId);
    else
        s.bindNull(3);
    if (auto r = s.run(); !r)
        return r.error();
    deck.id = m_db.lastInsertRowId();
    return deck;
}

Result<void> StudyRepository::updateDeck(const Deck& deck)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE decks SET name = ?2, subject_id = ?3, scheduler = ?4, config_json = ?5 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, deck.id).bind(2, deck.name).bind(4, deck.scheduler).bind(5, configText(deck));
    if (deck.subjectId > 0)
        s.bind(3, deck.subjectId);
    else
        s.bindNull(3);
    return s.run();
}

Result<void> StudyRepository::removeDeck(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("DELETE FROM decks WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, id).run();
}

// ───────────────────────── Cartes ─────────────────────────

Result<QList<CardRow>> StudyRepository::cards(qint64 deckId)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT ") + kCardColumns +
                             QStringLiteral("WHERE c.deck_id = ?1 AND c.deleted_at IS NULL ORDER BY c.created_at DESC, c.id DESC"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId);
    return readCards(stmt.value());
}

Result<CardRow> StudyRepository::card(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT ") + kCardColumns + QStringLiteral("WHERE c.id = ?1 AND c.deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = readCards(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return Error::make(Error::Code::NotFound, QStringLiteral("Carte introuvable"));
    return rows.value().first();
}

Result<QList<Card>> StudyRepository::insertCards(const QList<Card>& cards)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto ins = m_db.prepare(QStringLiteral(
        "INSERT INTO cards(uuid, deck_id, card_type, front_md, back_md, ordinal, source_type, source_id, suspended, "
        "created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?10)"));
    if (!ins)
        return ins.error();
    auto state = m_db.prepare(QStringLiteral("INSERT INTO card_state(card_id) VALUES (?1)"));
    if (!state)
        return state.error();
    QList<Card> out;
    const QString now = nowIsoUtc();
    for (Card c : cards) {
        if (c.uuid.isEmpty())
            c.uuid = newUuid();
        c.createdAt = c.updatedAt = now;
        Statement& s = ins.value();
        s.reset();
        s.bind(1, c.uuid).bind(2, c.deckId).bind(3, cardTypeKey(c.type)).bind(4, c.front).bind(5, c.back.isNull() ? QStringLiteral("") : c.back).bind(6, c.ordinal)
            .bind(9, c.suspended ? 1 : 0).bind(10, now);
        if (c.sourceType != CardSource::None) {
            s.bind(7, cardSourceKey(c.sourceType));
            s.bind(8, c.sourceId);
        } else {
            s.bindNull(7);
            s.bindNull(8);
        }
        if (auto r = s.run(); !r)
            return r.error();
        c.id = m_db.lastInsertRowId();
        state.value().reset();
        if (auto r = state.value().bind(1, c.id).run(); !r)
            return r.error();
        out << c;
    }
    if (auto r = tx.commit(); !r)
        return r.error();
    return out;
}

Result<void> StudyRepository::updateCard(const Card& card)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE cards SET deck_id = ?2, card_type = ?3, front_md = ?4, back_md = ?5, ordinal = ?6, updated_at = ?7 "
        "WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value()
        .bind(1, card.id)
        .bind(2, card.deckId)
        .bind(3, cardTypeKey(card.type))
        .bind(4, card.front)
        .bind(5, card.back.isNull() ? QStringLiteral("") : card.back)
        .bind(6, card.ordinal)
        .bind(7, nowIsoUtc())
        .run();
}

Result<void> StudyRepository::setSuspended(qint64 id, bool suspended)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE cards SET suspended = ?2, updated_at = ?3 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, id).bind(2, suspended ? 1 : 0).bind(3, nowIsoUtc()).run();
}

Result<void> StudyRepository::removeCard(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE cards SET deleted_at = ?2 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, id).bind(2, nowIsoUtc()).run();
}

// ───────────────────────── File de révision ─────────────────────────

Result<DeckCounts> StudyRepository::counts(qint64 deckId, const QString& nowIso, const QString& dayStartIso,
                                           const QString& dayEndIso)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT COUNT(*), "
        "COUNT(*) FILTER (WHERE c.suspended = 1), "
        "COUNT(*) FILTER (WHERE c.suspended = 0 AND s.state = 'new'), "
        "COUNT(*) FILTER (WHERE c.suspended = 0 AND s.state IN ('learning', 'relearning') AND s.due_at <= ?2), "
        "COUNT(*) FILTER (WHERE c.suspended = 0 AND s.state = 'review' AND s.due_at <= ?4) "
        "FROM cards c JOIN card_state s ON s.card_id = c.id WHERE c.deck_id = ?1 AND c.deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId).bind(2, nowIso).bind(4, dayEndIso);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    DeckCounts c;
    const Statement& s = stmt.value();
    c.total = int(s.columnInt64(0));
    c.suspended = int(s.columnInt64(1));
    c.fresh = int(s.columnInt64(2));
    c.learningDue = int(s.columnInt64(3));
    c.reviewDue = int(s.columnInt64(4));

    auto done = m_db.prepare(QStringLiteral(
        "SELECT COUNT(*) FILTER (WHERE json_extract(r.prev_state_json, '$.state') = 'new'), "
        "COUNT(*) FILTER (WHERE json_extract(r.prev_state_json, '$.state') = 'review') "
        "FROM card_reviews r JOIN cards c ON c.id = r.card_id WHERE c.deck_id = ?1 AND r.reviewed_at >= ?2"));
    if (!done)
        return done.error();
    done.value().bind(1, deckId).bind(2, dayStartIso);
    auto drow = done.value().step();
    if (!drow)
        return drow.error();
    c.newDoneToday = int(done.value().columnInt64(0));
    c.reviewsDoneToday = int(done.value().columnInt64(1));
    return c;
}

Result<QList<CardRow>> StudyRepository::learning(qint64 deckId, const QString& untilIso, int limit)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT ") + kCardColumns +
                             QStringLiteral("WHERE c.deck_id = ?1 AND c.deleted_at IS NULL AND c.suspended = 0 "
                                            "AND s.state IN ('learning', 'relearning') AND s.due_at <= ?2 "
                                            "ORDER BY s.due_at, c.id LIMIT ?3"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId).bind(2, untilIso).bind(3, limit);
    return readCards(stmt.value());
}

Result<QList<CardRow>> StudyRepository::reviewsDue(qint64 deckId, const QString& untilIso, int limit)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT ") + kCardColumns +
                             QStringLiteral("WHERE c.deck_id = ?1 AND c.deleted_at IS NULL AND c.suspended = 0 "
                                            "AND s.state = 'review' AND s.due_at <= ?2 ORDER BY s.due_at, c.id LIMIT ?3"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId).bind(2, untilIso).bind(3, limit);
    return readCards(stmt.value());
}

Result<QList<CardRow>> StudyRepository::fresh(qint64 deckId, int limit)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT ") + kCardColumns +
                             QStringLiteral("WHERE c.deck_id = ?1 AND c.deleted_at IS NULL AND c.suspended = 0 "
                                            "AND s.state = 'new' ORDER BY c.created_at, c.id, c.ordinal LIMIT ?2"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId).bind(2, limit);
    return readCards(stmt.value());
}

Result<void> StudyRepository::recordReview(qint64 cardId, const ScheduleState& before, const ScheduleState& after,
                                           Rating rating, qint64 elapsedMs, const QString& atIso)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto upd = m_db.prepare(QStringLiteral(
        "INSERT INTO card_state(card_id, state, due_at, interval_d, ease, reps, lapses, step) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) ON CONFLICT(card_id) DO UPDATE SET state = excluded.state, "
        "due_at = excluded.due_at, interval_d = excluded.interval_d, ease = excluded.ease, reps = excluded.reps, "
        "lapses = excluded.lapses, step = excluded.step"));
    if (!upd)
        return upd.error();
    if (auto r = upd.value()
                     .bind(1, cardId)
                     .bind(2, cardStateKey(after.state))
                     .bind(3, isoOf(after.due))
                     .bind(4, after.intervalDays)
                     .bind(5, after.ease)
                     .bind(6, after.reps)
                     .bind(7, after.lapses)
                     .bind(8, after.step)
                     .run();
        !r)
        return r;
    auto log = m_db.prepare(QStringLiteral(
        "INSERT INTO card_reviews(card_id, reviewed_at, rating, elapsed_ms, prev_state_json, new_state_json) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6)"));
    if (!log)
        return log.error();
    const auto json = [](const ScheduleState& s) {
        return QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact));
    };
    if (auto r = log.value()
                     .bind(1, cardId)
                     .bind(2, atIso)
                     .bind(3, int(rating))
                     .bind(4, elapsedMs)
                     .bind(5, json(before))
                     .bind(6, json(after))
                     .run();
        !r)
        return r;
    auto act = m_db.prepare(QStringLiteral(
        "INSERT INTO activity(at, action, entity_type, entity_id, detail_json) VALUES (?1, 'reviewed', 'card', ?2, ?3)"));
    if (!act)
        return act.error();
    if (auto r = act.value().bind(1, atIso).bind(2, cardId).bind(3, QStringLiteral("{\"rating\":%1}").arg(int(rating))).run(); !r)
        return r;
    return tx.commit();
}

Result<QPair<int, int>> StudyRepository::reviewsBetween(qint64 deckId, const QString& fromIso, const QString& toIso)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT COUNT(*), COUNT(*) FILTER (WHERE r.rating > 1) FROM card_reviews r JOIN cards c ON c.id = r.card_id "
        "WHERE (?1 = 0 OR c.deck_id = ?1) AND r.reviewed_at >= ?2 AND r.reviewed_at < ?3"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, deckId).bind(2, fromIso).bind(3, toIso);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return qMakePair(int(stmt.value().columnInt64(0)), int(stmt.value().columnInt64(1)));
}

Result<CardSourceInfo> StudyRepository::source(CardSource type, qint64 id)
{
    CardSourceInfo info;
    info.type = type;
    info.itemId = id;
    if (type == CardSource::Annotation) {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT a.document_id, a.selected_text, a.unit_index, a.page_label, d.title, d.format FROM annotations a "
            "JOIN documents d ON d.id = a.document_id WHERE a.id = ?1 AND a.deleted_at IS NULL AND d.deleted_at IS NULL"));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, id);
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return Error::make(Error::Code::NotFound, QStringLiteral("Passage d'origine supprimé"));
        const Statement& s = stmt.value();
        info.documentId = s.columnInt64(0);
        info.quote = s.columnText(1).simplified();
        const QString page = s.columnText(3).isEmpty() ? QString::number(s.columnInt64(2) + 1) : s.columnText(3);
        info.label = s.columnText(5) == QLatin1String("pdf") ? QStringLiteral("%1 · p. %2").arg(s.columnText(4), page)
                                                            : s.columnText(4);
        return info;
    }
    if (type == CardSource::Note) {
        auto stmt = m_db.prepare(QStringLiteral("SELECT title FROM notes WHERE id = ?1 AND deleted_at IS NULL"));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, id);
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return Error::make(Error::Code::NotFound, QStringLiteral("Note d'origine supprimée"));
        info.label = stmt.value().columnText(0);
        return info;
    }
    if (type == CardSource::Document) {
        auto stmt = m_db.prepare(QStringLiteral("SELECT title FROM documents WHERE id = ?1 AND deleted_at IS NULL"));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, id);
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return Error::make(Error::Code::NotFound, QStringLiteral("Document d'origine retiré"));
        info.documentId = id;
        info.label = stmt.value().columnText(0);
        return info;
    }
    return Error::make(Error::Code::NotFound, QStringLiteral("Carte sans origine"));
}

}  // namespace studylab::db
