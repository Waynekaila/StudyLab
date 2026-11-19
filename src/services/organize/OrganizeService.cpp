#include "services/organize/OrganizeService.h"

#include "core/Clock.h"
#include "data/sqlite/Database.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("OrganizeService", text);
}

Error subjectNotFound() { return Error::make(Error::Code::NotFound, tr("Matière introuvable.")); }
Error collectionNotFound() { return Error::make(Error::Code::NotFound, tr("Collection introuvable.")); }

CollectionItem::Type itemType(OrganizeService::Entity e)
{
    return e == OrganizeService::Entity::Note ? CollectionItem::Type::Note : CollectionItem::Type::Document;
}

}  // namespace

OrganizeService::OrganizeService(db::Database& db, QObject* parent) : QObject(parent), m_db(db), m_repo(db) {}

QString OrganizeService::entityKey(Entity e)
{
    return e == Entity::Note ? QStringLiteral("note") : QStringLiteral("document");
}

// ───────────────────────── Matières ─────────────────────────

Result<QList<SubjectRecord>> OrganizeService::subjects()
{
    return m_repo.subjects();
}

Result<SubjectRecord> OrganizeService::subject(qint64 id)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    for (const SubjectRecord& s : all.value())
        if (s.id == id)
            return s;
    return subjectNotFound();
}

Result<SubjectRecord> OrganizeService::createSubject(const QString& name, qint64 parentId, const QString& color)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    SubjectRecord s;
    s.name = name;
    s.parentId = parentId;
    int last = -1;
    for (const SubjectRecord& o : all.value())
        if (o.parentId == parentId)
            last = std::max(last, o.position);
    if (parentId > 0 && std::none_of(all.value().cbegin(), all.value().cend(),
                                     [parentId](const SubjectRecord& o) { return o.id == parentId; }))
        return subjectNotFound();
    s.position = last + 1;
    const QStringList colors = organizeColorKeys();
    s.color = colors.contains(color) ? color : colors.at(int(all.value().size()) % colors.size());
    if (auto r = m_repo.insertSubject(s); !r)
        return r.error();
    emit subjectsChanged();
    return s;
}

Result<SubjectRecord> OrganizeService::modifySubject(qint64 id, const std::function<void(SubjectRecord&)>& change)
{
    auto s = subject(id);
    if (!s)
        return s.error();
    SubjectRecord r = s.value();
    change(r);
    if (auto res = m_repo.updateSubject(r); !res)
        return res.error();
    emit subjectsChanged();
    return subject(id);
}

Result<SubjectRecord> OrganizeService::renameSubject(qint64 id, const QString& name)
{
    return modifySubject(id, [&](SubjectRecord& r) { r.name = name; });
}

Result<SubjectRecord> OrganizeService::setSubjectColor(qint64 id, const QString& color)
{
    if (!organizeColorKeys().contains(color))
        return Error::make(Error::Code::InvalidArgument, tr("Couleur inconnue."));
    return modifySubject(id, [&](SubjectRecord& r) { r.color = color; });
}

Result<SubjectRecord> OrganizeService::setSubjectDescription(qint64 id, const QString& description)
{
    return modifySubject(id, [&](SubjectRecord& r) { r.description = description.trimmed(); });
}

Result<SubjectRecord> OrganizeService::setSubjectArchived(qint64 id, bool archived)
{
    return modifySubject(id, [&](SubjectRecord& r) { r.archived = archived; });
}

Result<void> OrganizeService::moveSubject(qint64 id, qint64 parentId, int position)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    if (parentId > 0 && withDescendants(id, all.value()).contains(parentId))
        return Error::make(Error::Code::InvalidArgument, tr("Une matière ne peut pas être rangée dans l'une de ses sous-matières."));
    QList<SubjectRecord> siblings;
    SubjectRecord moved;
    bool found = false;
    for (const SubjectRecord& s : all.value()) {
        if (s.id == id) {
            moved = s;
            found = true;
        } else if (s.parentId == parentId) {
            siblings << s;
        }
    }
    if (!found)
        return subjectNotFound();
    moved.parentId = parentId;
    const qsizetype at = position < 0 || position > siblings.size() ? siblings.size() : position;
    siblings.insert(at, moved);
    for (int i = 0; i < siblings.size(); ++i) {
        SubjectRecord s = siblings.at(i);
        if (s.position == i && s.id != id)
            continue;
        s.position = i;
        if (auto r = m_repo.updateSubject(s); !r)
            return r;
    }
    emit subjectsChanged();
    return {};
}

Result<void> OrganizeService::removeSubject(qint64 id)
{
    if (auto r = m_repo.removeSubject(id); !r)
        return r;
    emit subjectsChanged();
    emit membershipChanged();
    return {};
}

QList<qint64> OrganizeService::withDescendants(qint64 id, const QList<SubjectRecord>& all) const
{
    QList<qint64> out{id};
    for (qsizetype i = 0; i < out.size(); ++i)
        for (const SubjectRecord& s : all)
            if (s.parentId == out.at(i) && !out.contains(s.id))
                out << s.id;
    return out;
}

QString OrganizeService::subjectPath(qint64 id, const QList<SubjectRecord>& all) const
{
    QStringList parts;
    QSet<qint64> seen;
    qint64 cur = id;
    while (cur > 0 && !seen.contains(cur)) {
        seen.insert(cur);
        auto it = std::find_if(all.cbegin(), all.cend(), [cur](const SubjectRecord& s) { return s.id == cur; });
        if (it == all.cend())
            break;
        parts.prepend(it->name);
        cur = it->parentId;
    }
    return parts.join(QStringLiteral(" › "));
}

Result<void> OrganizeService::attach(qint64 subjectId, Entity entity, qint64 entityId)
{
    if (auto r = m_repo.attach(subjectId, entityKey(entity), entityId); !r)
        return r;
    emit membershipChanged();
    return {};
}

Result<void> OrganizeService::detach(qint64 subjectId, Entity entity, qint64 entityId)
{
    if (auto r = m_repo.detach(subjectId, entityKey(entity), entityId); !r)
        return r;
    emit membershipChanged();
    return {};
}

Result<void> OrganizeService::setSubjects(Entity entity, qint64 entityId, const QList<qint64>& subjectIds)
{
    auto current = m_repo.subjectsOf(entityKey(entity), entityId);
    if (!current)
        return current.error();
    for (qint64 id : current.value())
        if (!subjectIds.contains(id))
            if (auto r = m_repo.detach(id, entityKey(entity), entityId); !r)
                return r;
    for (qint64 id : subjectIds)
        if (!current.value().contains(id))
            if (auto r = m_repo.attach(id, entityKey(entity), entityId); !r)
                return r;
    emit membershipChanged();
    return {};
}

Result<QList<qint64>> OrganizeService::subjectsOf(Entity entity, qint64 entityId)
{
    return m_repo.subjectsOf(entityKey(entity), entityId);
}

Result<QList<qint64>> OrganizeService::subjectItems(qint64 subjectId, Entity entity)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    return m_repo.itemsOf(withDescendants(subjectId, all.value()), entityKey(entity));
}

Result<SubjectStats> OrganizeService::subjectStats(qint64 subjectId)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    return m_repo.stats(withDescendants(subjectId, all.value()));
}

Result<QHash<qint64, int>> OrganizeService::subjectCounts()
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    QHash<qint64, int> out;
    // Un même document peut être rattaché à une matière et à sa sous-matière : on compte les éléments distincts.
    for (const SubjectRecord& s : all.value()) {
        const QList<qint64> scope = withDescendants(s.id, all.value());
        auto docs = m_repo.itemsOf(scope, QStringLiteral("document"));
        auto notes = m_repo.itemsOf(scope, QStringLiteral("note"));
        if (!docs)
            return docs.error();
        if (!notes)
            return notes.error();
        out.insert(s.id, int(docs.value().size() + notes.value().size()));
    }
    return out;
}

// ───────────────────────── Collections ─────────────────────────

Result<QList<CollectionRecord>> OrganizeService::collections()
{
    return m_repo.collections();
}

Result<CollectionRecord> OrganizeService::collection(qint64 id)
{
    auto all = m_repo.collections();
    if (!all)
        return all.error();
    for (const CollectionRecord& c : all.value())
        if (c.id == id)
            return c;
    return collectionNotFound();
}

Result<CollectionRecord> OrganizeService::createCollection(const QString& name, CollectionKind kind,
                                                           const SmartQuery& query)
{
    auto all = m_repo.collections();
    if (!all)
        return all.error();
    CollectionRecord c;
    c.name = name;
    c.kind = kind;
    c.query = query;
    int last = -1;
    for (const CollectionRecord& o : all.value())
        last = std::max(last, o.position);
    c.position = last + 1;
    if (auto r = m_repo.insertCollection(c); !r)
        return r.error();
    emit collectionsChanged();
    return c;
}

Result<CollectionRecord> OrganizeService::modifyCollection(qint64 id,
                                                           const std::function<void(CollectionRecord&)>& change)
{
    auto c = collection(id);
    if (!c)
        return c.error();
    CollectionRecord r = c.value();
    change(r);
    if (auto res = m_repo.updateCollection(r); !res)
        return res.error();
    emit collectionsChanged();
    return r;
}

Result<CollectionRecord> OrganizeService::renameCollection(qint64 id, const QString& name)
{
    return modifyCollection(id, [&](CollectionRecord& r) { r.name = name; });
}

Result<CollectionRecord> OrganizeService::setQuery(qint64 id, const SmartQuery& query)
{
    return modifyCollection(id, [&](CollectionRecord& r) { r.query = query; });
}

Result<void> OrganizeService::removeCollection(qint64 id)
{
    if (auto r = m_repo.removeCollection(id); !r)
        return r;
    emit collectionsChanged();
    return {};
}

QHash<QString, QList<qint64>> OrganizeService::subjectScope(const QList<SubjectRecord>& all) const
{
    QHash<QString, QList<qint64>> out;
    for (const SubjectRecord& s : all)
        out.insert(s.uuid, withDescendants(s.id, all));
    return out;
}

Result<QList<qint64>> OrganizeService::evaluate(const SmartQuery& query)
{
    auto all = m_repo.subjects();
    if (!all)
        return all.error();
    return m_repo.smartDocuments(query, subjectScope(all.value()), nowIsoUtc());
}

Result<QList<CollectionItem>> OrganizeService::collectionItems(qint64 id)
{
    auto c = collection(id);
    if (!c)
        return c.error();
    if (c.value().kind != CollectionKind::Smart)
        return m_repo.items(id);
    auto docs = evaluate(c.value().query);
    if (!docs)
        return docs.error();
    QList<CollectionItem> out;
    for (int i = 0; i < docs.value().size(); ++i)
        out << CollectionItem{CollectionItem::Type::Document, docs.value().at(i), i};
    return out;
}

Result<QHash<qint64, int>> OrganizeService::collectionCounts()
{
    auto all = m_repo.collections();
    if (!all)
        return all.error();
    auto subjectsList = m_repo.subjects();
    if (!subjectsList)
        return subjectsList.error();
    const auto scope = subjectScope(subjectsList.value());
    const QString now = nowIsoUtc();
    QHash<qint64, int> out;
    for (const CollectionRecord& c : all.value()) {
        if (c.kind == CollectionKind::Smart) {
            auto docs = m_repo.smartDocuments(c.query, scope, now);
            if (!docs)
                return docs.error();
            out.insert(c.id, int(docs.value().size()));
        } else {
            auto items = m_repo.items(c.id);
            if (!items)
                return items.error();
            out.insert(c.id, int(items.value().size()));
        }
    }
    return out;
}

Result<void> OrganizeService::addToCollection(qint64 collectionId, Entity entity, qint64 entityId)
{
    auto c = collection(collectionId);
    if (!c)
        return c.error();
    if (c.value().kind == CollectionKind::Smart)
        return Error::make(Error::Code::InvalidArgument,
                           tr("Une collection intelligente se remplit d'après ses règles."));
    if (auto r = m_repo.addItem(collectionId, itemType(entity), entityId); !r)
        return r;
    emit membershipChanged();
    return {};
}

Result<void> OrganizeService::removeFromCollection(qint64 collectionId, Entity entity, qint64 entityId)
{
    if (auto r = m_repo.removeItem(collectionId, itemType(entity), entityId); !r)
        return r;
    emit membershipChanged();
    return {};
}

Result<void> OrganizeService::reorder(qint64 collectionId, const QList<CollectionItem>& order)
{
    if (auto r = m_repo.reorder(collectionId, order); !r)
        return r;
    emit membershipChanged();
    return {};
}

Result<QList<qint64>> OrganizeService::collectionsOf(Entity entity, qint64 entityId)
{
    return m_repo.collectionsOf(itemType(entity), entityId);
}

}  // namespace studylab
