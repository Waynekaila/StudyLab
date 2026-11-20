#include "data/repositories/ActivityRepository.h"

#include "core/Clock.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace studylab::db {

namespace {
QString toJson(const QJsonObject& o)
{
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString pagesJson(const QList<int>& pages)
{
    QJsonArray a;
    for (int p : pages)
        a.append(p);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QList<int> pagesFromJson(const QString& text)
{
    QList<int> out;
    for (const QJsonValue& v : QJsonDocument::fromJson(text.toUtf8()).array())
        out << v.toInt();
    return out;
}
}  // namespace

ActivityRepository::ActivityRepository(Database& db) : m_db(db) {}

Result<void> ActivityRepository::log(const QString& action, const QString& entityType, qint64 entityId,
                                     const QJsonObject& detail, const QString& atIso)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO activity(at, action, entity_type, entity_id, detail_json) VALUES (?1, ?2, ?3, ?4, ?5)"));
    if (!stmt)
        return stmt.error();
    return stmt.value()
        .bind(1, atIso.isEmpty() ? nowIsoUtc() : atIso)
        .bind(2, action)
        .bind(3, entityType)
        .bind(4, entityId)
        .bind(5, toJson(detail))
        .run();
}

Result<QList<QPair<QString, QString>>> ActivityRepository::actionTimes(const QString& fromIso, const QString& toIso)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT at, action FROM activity WHERE at >= ?1 AND at < ?2"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, fromIso).bind(2, toIso);
    QList<QPair<QString, QString>> out;
    while (true) {
        auto row = s.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out.append({s.columnText(0), s.columnText(1)});
    }
    return out;
}

Result<QList<ActivityRow>> ActivityRepository::entries(const QString& fromIso, const QString& toIso, int limit)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, at, action, entity_type, entity_id, detail_json FROM activity WHERE at >= ?1 AND at < ?2 "
        "ORDER BY at DESC, id DESC LIMIT ?3"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, fromIso).bind(2, toIso).bind(3, limit);
    QList<ActivityRow> out;
    while (true) {
        auto row = s.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        ActivityRow r;
        r.id = s.columnInt64(0);
        r.at = s.columnText(1);
        r.action = s.columnText(2);
        r.entityType = s.columnText(3);
        r.entityId = s.columnInt64(4);
        if (!s.isNull(5))
            r.detail = QJsonDocument::fromJson(s.columnText(5).toUtf8()).object();
        out << r;
    }
    return out;
}

Result<void> ActivityRepository::saveSession(ReadingSessionRow& session)
{
    QList<int> pages = session.pages;
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    session.pages = pages;
    if (session.startedAt.isEmpty())
        session.startedAt = nowIsoUtc();
    if (session.endedAt.isEmpty())
        session.endedAt = nowIsoUtc();
    if (session.id == 0) {
        auto stmt = m_db.prepare(QStringLiteral(
            "INSERT INTO reading_sessions(document_id, started_at, ended_at, seconds, pages_seen) "
            "VALUES (?1, ?2, ?3, ?4, ?5)"));
        if (!stmt)
            return stmt.error();
        if (auto r = stmt.value()
                         .bind(1, session.documentId)
                         .bind(2, session.startedAt)
                         .bind(3, session.endedAt)
                         .bind(4, session.seconds)
                         .bind(5, pagesJson(pages))
                         .run();
            !r)
            return r;
        session.id = m_db.lastInsertRowId();
        return {};
    }
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE reading_sessions SET ended_at = ?2, seconds = ?3, pages_seen = ?4 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value()
        .bind(1, session.id)
        .bind(2, session.endedAt)
        .bind(3, session.seconds)
        .bind(4, pagesJson(pages))
        .run();
}

Result<QList<ReadingSessionRow>> ActivityRepository::sessions(const QString& fromIso, const QString& toIso)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT s.id, s.document_id, s.started_at, s.ended_at, s.seconds, s.pages_seen FROM reading_sessions s "
        "JOIN documents d ON d.id = s.document_id AND d.deleted_at IS NULL "
        "WHERE s.started_at >= ?1 AND s.started_at < ?2 ORDER BY s.started_at, s.id"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, fromIso).bind(2, toIso);
    QList<ReadingSessionRow> out;
    while (true) {
        auto row = s.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        ReadingSessionRow r;
        r.id = s.columnInt64(0);
        r.documentId = s.columnInt64(1);
        r.startedAt = s.columnText(2);
        r.endedAt = s.columnText(3);
        r.seconds = int(s.columnInt64(4));
        r.pages = pagesFromJson(s.columnText(5));
        out << r;
    }
    return out;
}

Result<QList<GoalRow>> ActivityRepository::goals()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT kind, target FROM goals WHERE subject_id IS NULL AND active = 1 ORDER BY id"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    QList<GoalRow> out;
    while (true) {
        auto row = s.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << GoalRow{s.columnText(0), int(s.columnInt64(1))};
    }
    return out;
}

Result<void> ActivityRepository::setGoal(const QString& kind, int target)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto del = m_db.prepare(QStringLiteral("DELETE FROM goals WHERE kind = ?1 AND subject_id IS NULL"));
    if (!del)
        return del.error();
    if (auto r = del.value().bind(1, kind).run(); !r)
        return r;
    if (target > 0) {
        auto ins = m_db.prepare(QStringLiteral("INSERT INTO goals(kind, target, active) VALUES (?1, ?2, 1)"));
        if (!ins)
            return ins.error();
        if (auto r = ins.value().bind(1, kind).bind(2, target).run(); !r)
            return r;
    }
    return tx.commit();
}

Result<QHash<qint64, QString>> ActivityRepository::titles(const QString& table, const QList<qint64>& ids)
{
    QHash<qint64, QString> out;
    if (ids.isEmpty())
        return out;
    QStringList list;
    for (qint64 id : ids)
        list << QString::number(id);
    auto stmt = m_db.prepare(QStringLiteral("SELECT id, title FROM %1 WHERE deleted_at IS NULL AND id IN (%2)")
                                 .arg(table, list.join(QLatin1Char(','))));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    while (true) {
        auto row = s.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out.insert(s.columnInt64(0), s.columnText(1));
    }
    return out;
}

Result<QHash<qint64, QString>> ActivityRepository::documentTitles(const QList<qint64>& ids)
{
    return titles(QStringLiteral("documents"), ids);
}

Result<QHash<qint64, QString>> ActivityRepository::noteTitles(const QList<qint64>& ids)
{
    return titles(QStringLiteral("notes"), ids);
}

}  // namespace studylab::db
