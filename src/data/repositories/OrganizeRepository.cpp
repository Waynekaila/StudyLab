#include "data/repositories/OrganizeRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QDateTime>
#include <QJsonDocument>

#include <variant>

namespace studylab::db {

namespace {

Error invalid(const QString& message) { return Error::make(Error::Code::InvalidArgument, message); }

template <typename T, typename F>
Result<QList<T>> collect(Statement& stmt, F read)
{
    QList<T> out;
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << read(stmt);
    }
    return out;
}

/// « ?, ?, ? » pour n valeurs.
QString placeholders(qsizetype n)
{
    QStringList p;
    for (qsizetype i = 0; i < n; ++i)
        p << QStringLiteral("?");
    return p.join(QStringLiteral(", "));
}

QString idList(const QList<qint64>& ids)
{
    QStringList s;
    for (qint64 id : ids)
        s << QString::number(id);
    return s.join(QLatin1Char(','));
}

/// Condition « élément actif » d'un rattachement (document ou note hors corbeille).
const QString kActiveDoc = QStringLiteral("EXISTS (SELECT 1 FROM documents d WHERE d.id = %1 AND d.deleted_at IS NULL)");
const QString kActiveNote = QStringLiteral("EXISTS (SELECT 1 FROM notes n WHERE n.id = %1 AND n.deleted_at IS NULL)");

SubjectRecord readSubject(const Statement& s)
{
    SubjectRecord r;
    r.id = s.columnInt64(0);
    r.uuid = s.columnText(1);
    r.parentId = s.isNull(2) ? 0 : s.columnInt64(2);
    r.name = s.columnText(3);
    r.description = s.columnText(4);
    r.color = s.isNull(5) ? QStringLiteral("slate") : s.columnText(5);
    r.position = int(s.columnInt64(6));
    r.createdAt = s.columnText(7);
    r.archived = s.columnInt64(8) != 0;
    return r;
}

CollectionRecord readCollection(const Statement& s)
{
    CollectionRecord c;
    c.id = s.columnInt64(0);
    c.uuid = s.columnText(1);
    c.kind = collectionKindFromKey(s.columnText(2)).value_or(CollectionKind::Manual);
    c.name = s.columnText(3);
    c.description = s.columnText(4);
    c.color = s.isNull(5) ? QStringLiteral("slate") : s.columnText(5);
    if (!s.isNull(6))
        c.query = SmartQuery::fromJson(QJsonDocument::fromJson(s.columnText(6).toUtf8()).object()).value_or(SmartQuery{});
    c.position = int(s.columnInt64(7));
    c.createdAt = s.columnText(8);
    c.updatedAt = s.columnText(9);
    return c;
}

void bindParent(Statement& s, int index, qint64 parentId)
{
    if (parentId > 0)
        s.bind(index, parentId);
    else
        s.bindNull(index);
}

}  // namespace

QString collectionItemTypeKey(CollectionItem::Type t)
{
    return t == CollectionItem::Type::Note ? QStringLiteral("note") : QStringLiteral("document");
}

OrganizeRepository::OrganizeRepository(Database& db) : m_db(db) {}

// ───────────────────────── Matières ─────────────────────────

Result<void> OrganizeRepository::insertSubject(SubjectRecord& r)
{
    r.name = r.name.simplified();
    if (r.name.isEmpty())
        return invalid(QStringLiteral("Une matière exige un nom."));
    if (r.uuid.isEmpty())
        r.uuid = newUuid();
    if (r.createdAt.isEmpty())
        r.createdAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO subjects(uuid, parent_id, name, description, color, position, created_at, archived) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, r.uuid);
    bindParent(s, 2, r.parentId);
    s.bind(3, r.name).bind(4, r.description).bind(5, r.color).bind(6, r.position).bind(7, r.createdAt);
    s.bind(8, r.archived ? 1 : 0);
    if (auto res = s.run(); !res)
        return res;
    r.id = m_db.lastInsertRowId();
    return {};
}

Result<void> OrganizeRepository::updateSubject(const SubjectRecord& r)
{
    if (r.name.simplified().isEmpty())
        return invalid(QStringLiteral("Une matière exige un nom."));
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE subjects SET parent_id = ?2, name = ?3, description = ?4, color = ?5, position = ?6, archived = ?7 "
        "WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, r.id);
    bindParent(s, 2, r.parentId);
    s.bind(3, r.name.simplified()).bind(4, r.description).bind(5, r.color).bind(6, r.position);
    s.bind(7, r.archived ? 1 : 0);
    if (auto res = s.run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Matière introuvable."));
    return {};
}

Result<void> OrganizeRepository::removeSubject(qint64 id)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto up = m_db.prepare(QStringLiteral(
        "UPDATE subjects SET parent_id = (SELECT parent_id FROM subjects WHERE id = ?1) WHERE parent_id = ?1"));
    if (!up)
        return up.error();
    if (auto res = up.value().bind(1, id).run(); !res)
        return res;
    auto del = m_db.prepare(QStringLiteral("DELETE FROM subjects WHERE id = ?1"));
    if (!del)
        return del.error();
    if (auto res = del.value().bind(1, id).run(); !res)
        return res;
    return tx.commit();
}

Result<QList<SubjectRecord>> OrganizeRepository::subjects()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, uuid, parent_id, name, description, color, position, created_at, archived FROM subjects "
        "ORDER BY position, name COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    return collect<SubjectRecord>(stmt.value(), readSubject);
}

Result<void> OrganizeRepository::attach(qint64 subjectId, const QString& entityType, qint64 entityId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO subject_items(subject_id, entity_type, entity_id) VALUES (?1, ?2, ?3)"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, subjectId).bind(2, entityType).bind(3, entityId).run();
}

Result<void> OrganizeRepository::detach(qint64 subjectId, const QString& entityType, qint64 entityId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "DELETE FROM subject_items WHERE subject_id = ?1 AND entity_type = ?2 AND entity_id = ?3"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, subjectId).bind(2, entityType).bind(3, entityId).run();
}

Result<QList<qint64>> OrganizeRepository::subjectsOf(const QString& entityType, qint64 entityId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT si.subject_id FROM subject_items si JOIN subjects s ON s.id = si.subject_id "
        "WHERE si.entity_type = ?1 AND si.entity_id = ?2 ORDER BY s.position, s.name COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, entityType).bind(2, entityId);
    return collect<qint64>(stmt.value(), [](const Statement& s) { return s.columnInt64(0); });
}

Result<QList<qint64>> OrganizeRepository::itemsOf(const QList<qint64>& subjectIds, const QString& entityType)
{
    if (subjectIds.isEmpty())
        return QList<qint64>{};
    const bool doc = entityType == QLatin1String("document");
    const QString sql = doc
        ? QStringLiteral("SELECT d.id FROM documents d WHERE d.deleted_at IS NULL AND d.id IN "
                         "(SELECT entity_id FROM subject_items WHERE entity_type = 'document' AND subject_id IN (%1)) "
                         "ORDER BY d.sort_title")
        : QStringLiteral("SELECT n.id FROM notes n WHERE n.deleted_at IS NULL AND n.id IN "
                         "(SELECT entity_id FROM subject_items WHERE entity_type = 'note' AND subject_id IN (%1)) "
                         "ORDER BY n.pinned DESC, n.updated_at DESC");
    auto stmt = m_db.prepare(sql.arg(idList(subjectIds)));
    if (!stmt)
        return stmt.error();
    return collect<qint64>(stmt.value(), [](const Statement& s) { return s.columnInt64(0); });
}

Result<SubjectStats> OrganizeRepository::stats(const QList<qint64>& subjectIds)
{
    SubjectStats st;
    if (subjectIds.isEmpty())
        return st;
    const QString ids = idList(subjectIds);
    const QString docs = QStringLiteral(
        "SELECT d.id FROM documents d WHERE d.deleted_at IS NULL AND d.id IN "
        "(SELECT entity_id FROM subject_items WHERE entity_type = 'document' AND subject_id IN (%1))").arg(ids);
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM (%1)), "
        "(SELECT COUNT(*) FROM annotations a WHERE a.deleted_at IS NULL AND a.type <> 'bookmark' AND a.document_id IN (%1)), "
        "(SELECT COUNT(*) FROM notes n WHERE n.deleted_at IS NULL AND n.id IN "
        "  (SELECT entity_id FROM subject_items WHERE entity_type = 'note' AND subject_id IN (%2))), "
        "(SELECT COALESCE(AVG(COALESCE(rp.progress, 0)), 0) FROM documents d2 LEFT JOIN reading_progress rp "
        "  ON rp.document_id = d2.id WHERE d2.id IN (%1))").arg(docs, ids));
    if (!stmt)
        return stmt.error();
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    if (row.value()) {
        const Statement& s = stmt.value();
        st.documents = int(s.columnInt64(0));
        st.annotations = int(s.columnInt64(1));
        st.notes = int(s.columnInt64(2));
        st.progress = s.columnDouble(3);
    }
    return st;
}

Result<QHash<qint64, int>> OrganizeRepository::directCounts()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT si.subject_id, COUNT(*) FROM subject_items si WHERE "
        "(si.entity_type = 'document' AND %1) OR (si.entity_type = 'note' AND %2) GROUP BY si.subject_id")
                                 .arg(kActiveDoc.arg(QStringLiteral("si.entity_id")),
                                      kActiveNote.arg(QStringLiteral("si.entity_id"))));
    if (!stmt)
        return stmt.error();
    QHash<qint64, int> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out.insert(stmt.value().columnInt64(0), int(stmt.value().columnInt64(1)));
    }
    return out;
}

// ───────────────────────── Collections ─────────────────────────

Result<void> OrganizeRepository::insertCollection(CollectionRecord& c)
{
    c.name = c.name.simplified();
    if (c.name.isEmpty())
        return invalid(QStringLiteral("Une collection exige un nom."));
    if (c.uuid.isEmpty())
        c.uuid = newUuid();
    c.createdAt = c.createdAt.isEmpty() ? nowIsoUtc() : c.createdAt;
    c.updatedAt = c.createdAt;
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO collections(uuid, kind, name, description, color, query_json, position, created_at, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, c.uuid).bind(2, collectionKindKey(c.kind)).bind(3, c.name).bind(4, c.description).bind(5, c.color);
    if (c.kind == CollectionKind::Smart)
        s.bind(6, QString::fromUtf8(QJsonDocument(c.query.toJson()).toJson(QJsonDocument::Compact)));
    else
        s.bindNull(6);
    s.bind(7, c.position).bind(8, c.createdAt).bind(9, c.updatedAt);
    if (auto res = s.run(); !res)
        return res;
    c.id = m_db.lastInsertRowId();
    return {};
}

Result<void> OrganizeRepository::updateCollection(CollectionRecord& c)
{
    c.name = c.name.simplified();
    if (c.name.isEmpty())
        return invalid(QStringLiteral("Une collection exige un nom."));
    c.updatedAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE collections SET name = ?2, description = ?3, color = ?4, query_json = ?5, position = ?6, updated_at = ?7 "
        "WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, c.id).bind(2, c.name).bind(3, c.description).bind(4, c.color);
    if (c.kind == CollectionKind::Smart)
        s.bind(5, QString::fromUtf8(QJsonDocument(c.query.toJson()).toJson(QJsonDocument::Compact)));
    else
        s.bindNull(5);
    s.bind(6, c.position).bind(7, c.updatedAt);
    if (auto res = s.run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Collection introuvable."));
    return {};
}

Result<void> OrganizeRepository::removeCollection(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("DELETE FROM collections WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, id).run();
}

Result<QList<CollectionRecord>> OrganizeRepository::collections()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, uuid, kind, name, description, color, query_json, position, created_at, updated_at FROM collections "
        "WHERE kind <> 'folder' ORDER BY position, name COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    return collect<CollectionRecord>(stmt.value(), readCollection);
}

Result<QList<CollectionItem>> OrganizeRepository::items(qint64 collectionId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT ci.entity_type, ci.entity_id, ci.position FROM collection_items ci WHERE ci.collection_id = ?1 AND "
        "((ci.entity_type = 'document' AND %1) OR (ci.entity_type = 'note' AND %2)) ORDER BY ci.position, ci.added_at")
                                 .arg(kActiveDoc.arg(QStringLiteral("ci.entity_id")),
                                      kActiveNote.arg(QStringLiteral("ci.entity_id"))));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, collectionId);
    return collect<CollectionItem>(stmt.value(), [](const Statement& s) {
        CollectionItem i;
        i.type = s.columnText(0) == QLatin1String("note") ? CollectionItem::Type::Note : CollectionItem::Type::Document;
        i.id = s.columnInt64(1);
        i.position = int(s.columnInt64(2));
        return i;
    });
}

Result<void> OrganizeRepository::addItem(qint64 collectionId, CollectionItem::Type type, qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO collection_items(collection_id, entity_type, entity_id, position, added_at) "
        "VALUES (?1, ?2, ?3, (SELECT COALESCE(MAX(position), -1) + 1 FROM collection_items WHERE collection_id = ?1), ?4)"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, collectionId).bind(2, collectionItemTypeKey(type)).bind(3, id).bind(4, nowIsoUtc()).run();
}

Result<void> OrganizeRepository::removeItem(qint64 collectionId, CollectionItem::Type type, qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "DELETE FROM collection_items WHERE collection_id = ?1 AND entity_type = ?2 AND entity_id = ?3"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, collectionId).bind(2, collectionItemTypeKey(type)).bind(3, id).run();
}

Result<void> OrganizeRepository::reorder(qint64 collectionId, const QList<CollectionItem>& order)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE collection_items SET position = ?4 WHERE collection_id = ?1 AND entity_type = ?2 AND entity_id = ?3"));
    if (!stmt)
        return stmt.error();
    for (int i = 0; i < order.size(); ++i) {
        Statement& s = stmt.value();
        s.reset();
        s.bind(1, collectionId).bind(2, collectionItemTypeKey(order.at(i).type)).bind(3, order.at(i).id).bind(4, i);
        if (auto res = s.run(); !res)
            return res;
    }
    return tx.commit();
}

Result<QList<qint64>> OrganizeRepository::collectionsOf(CollectionItem::Type type, qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT ci.collection_id FROM collection_items ci JOIN collections c ON c.id = ci.collection_id "
        "WHERE ci.entity_type = ?1 AND ci.entity_id = ?2 ORDER BY c.position, c.name COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, collectionItemTypeKey(type)).bind(2, id);
    return collect<qint64>(stmt.value(), [](const Statement& s) { return s.columnInt64(0); });
}

Result<QList<qint64>> OrganizeRepository::smartDocuments(const SmartQuery& query,
                                                         const QHash<QString, QList<qint64>>& subjectScope,
                                                         const QString& now)
{
    using Value = std::variant<qint64, QString>;
    QList<Value> binds;
    QStringList conds;
    const QDateTime reference = QDateTime::fromString(now, Qt::ISODate);
    const auto cutoff = [&](const SmartRule& r) {
        const int days = r.values.isEmpty() ? 0 : r.values.first().toInt();
        return reference.addDays(-qMax(0, days)).toUTC().toString(Qt::ISODate);
    };
    const auto in = [&](const QStringList& values) {
        for (const QString& v : values)
            binds << Value(v);
        return QStringLiteral("(%1)").arg(placeholders(values.size()));
    };

    for (const SmartRule& r : query.rules) {
        const bool neg = r.op == SmartRule::Op::IsNot;
        QString c;
        switch (r.field) {
        case SmartRule::Field::Subject: {
            QList<qint64> ids;
            for (const QString& uuid : r.values)
                ids << subjectScope.value(uuid);
            c = ids.isEmpty() ? QStringLiteral("0")
                              : QStringLiteral("d.id IN (SELECT entity_id FROM subject_items WHERE entity_type = 'document' "
                                               "AND subject_id IN (%1))").arg(idList(ids));
            break;
        }
        case SmartRule::Field::Status:
            c = r.values.isEmpty() ? QStringLiteral("0") : QStringLiteral("d.status IN ") + in(r.values);
            break;
        case SmartRule::Field::Format:
            c = r.values.isEmpty() ? QStringLiteral("0") : QStringLiteral("d.format IN ") + in(r.values);
            break;
        case SmartRule::Field::Tag:
            c = r.values.isEmpty() ? QStringLiteral("0")
                                   : QStringLiteral("d.id IN (SELECT et.entity_id FROM entity_tags et JOIN tags t ON "
                                                    "t.id = et.tag_id WHERE et.entity_type = 'document' AND t.name IN ")
                    + in(r.values) + QLatin1Char(')');
            break;
        case SmartRule::Field::Favorite:
            c = QStringLiteral("d.favorite = 1");
            break;
        case SmartRule::Field::AddedWithin:
            binds << Value(cutoff(r));
            c = QStringLiteral("d.added_at >= ?");
            break;
        case SmartRule::Field::AnnotatedWithin:
            binds << Value(cutoff(r));
            c = QStringLiteral("d.id IN (SELECT document_id FROM annotations WHERE deleted_at IS NULL AND created_at >= ?)");
            break;
        case SmartRule::Field::OpenedWithin:
            binds << Value(cutoff(r));
            c = QStringLiteral("COALESCE(d.last_opened_at, '') >= ?");
            break;
        }
        conds << (neg ? QStringLiteral("NOT (%1)").arg(c) : QStringLiteral("(%1)").arg(c));
    }
    QString where = QStringLiteral("d.deleted_at IS NULL");
    if (!conds.isEmpty())
        where += QStringLiteral(" AND (%1)").arg(conds.join(query.matchAll ? QStringLiteral(" AND ") : QStringLiteral(" OR ")));
    auto stmt = m_db.prepare(QStringLiteral("SELECT d.id FROM documents d WHERE %1 ORDER BY d.sort_title").arg(where));
    if (!stmt)
        return stmt.error();
    for (int i = 0; i < binds.size(); ++i) {
        if (std::holds_alternative<qint64>(binds.at(i)))
            stmt.value().bind(i + 1, std::get<qint64>(binds.at(i)));
        else
            stmt.value().bind(i + 1, std::get<QString>(binds.at(i)));
    }
    return collect<qint64>(stmt.value(), [](const Statement& s) { return s.columnInt64(0); });
}

}  // namespace studylab::db
