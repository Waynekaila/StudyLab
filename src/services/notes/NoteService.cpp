#include "services/notes/NoteService.h"

#include "data/repositories/ActivityRepository.h"

#include "core/Uuid.h"
#include "core/domain/Comment.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Transaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>

namespace studylab {

namespace {

/// Cibles [[…]] d'un texte qui désignent une note (les citations [[@clé]] relèvent des références).
QStringList noteTargets(const QString& body)
{
    QStringList out;
    QStringList keys;
    for (const QString& t : wikiLinkTargets(stripFrontMatter(body))) {
        if (t.startsWith(u'@'))
            continue;
        const QString key = wikiLinkKey(t);
        if (key.isEmpty() || keys.contains(key))
            continue;
        keys << key;
        out << t;
    }
    return out;
}

/// Remplace la cible des [[liens]] dont la clé est dans oldKeys, en gardant « #section », « ^bloc » et « |alias ».
QString retarget(const QString& body, const QStringList& oldKeys, const QString& newTarget, bool* changed)
{
    static const QRegularExpression link(QStringLiteral("\\[\\[([^\\[\\]\\n]+)\\]\\]"));
    QString out;
    qsizetype last = 0;
    auto it = link.globalMatch(body);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString inner = m.captured(1);
        if (!oldKeys.contains(wikiLinkKey(inner)))
            continue;
        static const QRegularExpression cut(QStringLiteral("[#^|]"));
        const qsizetype end = inner.indexOf(cut);
        const QString rest = end < 0 ? QString() : inner.mid(end);
        out += body.mid(last, m.capturedStart() - last);
        out += QStringLiteral("[[") + newTarget + rest + QStringLiteral("]]");
        last = m.capturedEnd();
        *changed = true;
    }
    out += body.mid(last);
    return out;
}

Error invalid(const QString& message) { return Error::make(Error::Code::InvalidArgument, message); }

}  // namespace

NoteService::NoteService(db::Database& db, QString attachmentsDir, QObject* parent)
    : QObject(parent), m_db(db), m_repo(db), m_attachmentsDir(std::move(attachmentsDir))
{
    qRegisterMetaType<NoteRecord>();
}

QString NoteService::normalizeFolder(const QString& folder)
{
    QStringList parts;
    for (const QString& p : folder.split(u'/', Qt::SkipEmptyParts)) {
        const QString t = noteSlug(p.simplified());
        if (!p.trimmed().isEmpty())
            parts << t;
    }
    return parts.join(u'/');
}

Result<QString> NoteService::uniqueSlug(const QString& title, qint64 exceptId)
{
    const QString base = noteSlug(title);
    for (int n = 1; n < 10000; ++n) {
        const QString candidate = n == 1 ? base : QStringLiteral("%1 %2").arg(base).arg(n);
        auto taken = m_repo.slugTaken(candidate, exceptId);
        if (!taken)
            return taken.error();
        if (!taken.value())
            return candidate;
    }
    return Error::make(Error::Code::Generic, tr("Trop de notes portent ce titre."));
}

QHash<QString, qint64> NoteService::keyMap()
{
    QHash<QString, qint64> map;
    auto keys = m_repo.keys();
    if (!keys)
        return map;
    // Priorité : titre, puis nom de fichier, puis alias ; à égalité, la note la plus ancienne.
    for (const auto& k : keys.value())
        map.insert(wikiLinkKey(k.title), k.id);
    for (const auto& k : keys.value()) {
        const QString key = wikiLinkKey(k.slug);
        if (!map.contains(key))
            map.insert(key, k.id);
    }
    for (const auto& k : keys.value())
        for (const QString& a : k.aliases) {
            const QString key = wikiLinkKey(a);
            if (!map.contains(key))
                map.insert(key, k.id);
        }
    return map;
}

QHash<QString, qint64> NoteService::documentMap()
{
    QHash<QString, qint64> map;
    auto docs = m_repo.documentKeys();
    if (!docs)
        return map;
    for (const auto& d : docs.value()) {
        const QString key = wikiLinkKey(d.title);
        if (!key.isEmpty() && !map.contains(key))
            map.insert(key, d.id);
    }
    return map;
}

NoteLinkTarget NoteService::pick(const QString& target, const QHash<QString, qint64>& notes, const QHash<QString, qint64>& docs)
{
    if (target.trimmed().startsWith(u'@'))
        return {};
    const QString key = wikiLinkKey(target);
    if (const qint64 n = notes.value(key, 0); n > 0)
        return {NoteLinkTarget::Kind::Note, n};
    if (const qint64 d = docs.value(key, 0); d > 0)
        return {NoteLinkTarget::Kind::Document, d};
    return {};
}

namespace {
QString typeKey(const NoteLinkTarget& t)
{
    return t.kind == NoteLinkTarget::Kind::Document ? QStringLiteral("document") : QStringLiteral("note");
}
NoteLinkTarget fromRow(const db::NoteLinkRow& l)
{
    if (l.targetNoteId <= 0)
        return {};
    return {l.targetType == QLatin1String("document") ? NoteLinkTarget::Kind::Document : NoteLinkTarget::Kind::Note,
            l.targetNoteId};
}
}  // namespace

Result<void> NoteService::indexNote(const NoteRecord& note)
{
    if (auto r = m_repo.setAliases(note.id, noteAliases(note.body)); !r)
        return r;
    const QHash<QString, qint64> notes = keyMap();
    const QHash<QString, qint64> docs = documentMap();
    QList<db::NoteLinkRow> rows;
    for (const QString& target : noteTargets(note.body)) {
        const NoteLinkTarget t = pick(target, notes, docs);
        db::NoteLinkRow row;
        row.targetText = target;
        row.targetType = typeKey(t);
        row.targetNoteId = t.id;
        rows << row;
    }
    return m_repo.replaceWikiLinks(note.id, rows);
}

Result<bool> NoteService::relinkAll()
{
    const QHash<QString, qint64> notes = keyMap();
    const QHash<QString, qint64> docs = documentMap();
    auto links = m_repo.allWikiLinks();
    if (!links)
        return links.error();
    bool changed = false;
    for (const auto& l : links.value()) {
        const NoteLinkTarget t = pick(l.targetText, notes, docs);
        if (t == fromRow(l))
            continue;
        if (auto r = m_repo.setLinkTarget(l.id, typeKey(t), t.id); !r)
            return r.error();
        changed = true;
    }
    return changed;
}

Result<NoteRecord> NoteService::create(const QString& title, NoteType type, const QString& folder, const QString& body)
{
    QString t = title.simplified();
    if (t.isEmpty())
        t = tr("Sans titre");
    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto slug = uniqueSlug(t, 0);
    if (!slug)
        return slug.error();
    NoteRecord note;
    // Un titre déjà pris reçoit le même suffixe que son nom de fichier (« Sans titre 2 »).
    note.title = slug.value() == noteSlug(t) ? t : slug.value();
    note.slug = slug.value();
    note.type = type;
    note.folder = normalizeFolder(folder);
    note.body = body.isEmpty() ? noteTemplate(type, note.title) : body;
    note.frontMatter = parseFrontMatter(note.body).values;
    if (auto r = m_repo.insert(note); !r)
        return r.error();
    if (auto r = indexNote(note); !r)
        return r.error();
    auto relinked = relinkAll();
    if (!relinked)
        return relinked.error();
    (void)db::ActivityRepository(m_db).log(QStringLiteral("note_created"), QStringLiteral("note"), note.id);
    if (auto r = tx.commit(); !r)
        return r.error();
    auto stored = m_repo.findById(note.id);
    if (stored) {
        emit noteAdded(stored.value());
        emit linksChanged();
    }
    return stored;
}

Result<NoteRecord> NoteService::save(qint64 id, const QString& title, const QString& body)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    const NoteRecord before = current.value();
    NoteRecord note = before;
    const QString newTitle = title.simplified();
    if (newTitle.isEmpty())
        return invalid(tr("Une note exige un titre."));

    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    const bool renamed = newTitle != before.title;
    if (renamed) {
        auto slug = uniqueSlug(newTitle, id);
        if (!slug)
            return slug.error();
        note.slug = slug.value();
        note.title = slug.value() == noteSlug(newTitle) ? newTitle : slug.value();
    }
    note.body = body;
    note.frontMatter = parseFrontMatter(body).values;
    if (auto r = m_repo.update(note); !r)
        return r.error();

    auto oldLinks = m_repo.outgoingLinks(id);
    if (!oldLinks)
        return oldLinks.error();
    const QStringList oldAliases = noteAliases(before.body);
    if (auto r = indexNote(note); !r)
        return r.error();

    // Renommage : les [[liens]] des autres notes suivent le nouveau titre.
    QList<qint64> rewritten;
    if (renamed) {
        auto incoming = m_repo.incomingLinks(id);
        if (!incoming)
            return incoming.error();
        const QStringList oldKeys{wikiLinkKey(before.title), wikiLinkKey(before.slug)};
        for (const auto& l : incoming.value()) {
            if (rewritten.contains(l.sourceNoteId) || !oldKeys.contains(wikiLinkKey(l.targetText)))
                continue;
            auto source = m_repo.findById(l.sourceNoteId);
            if (!source)
                continue;
            bool changed = false;
            NoteRecord s = source.value();
            s.body = retarget(s.body, oldKeys, note.title, &changed);
            if (!changed)
                continue;
            s.frontMatter = parseFrontMatter(s.body).values;
            if (auto r = m_repo.update(s); !r)
                return r.error();
            if (auto r = indexNote(s); !r)
                return r.error();
            rewritten << s.id;
        }
    }
    auto relinked = relinkAll();
    if (!relinked)
        return relinked.error();
    auto newLinks = m_repo.outgoingLinks(id);
    if (!newLinks)
        return newLinks.error();
    if (auto r = tx.commit(); !r)
        return r.error();

    auto stored = m_repo.findById(id);
    if (!stored)
        return stored.error();
    emit noteChanged(stored.value());
    for (qint64 other : std::as_const(rewritten))
        if (auto o = m_repo.findById(other))
            emit noteChanged(o.value());

    auto targets = [](const QList<db::NoteLinkRow>& rows) {
        QStringList out;
        for (const auto& r : rows)
            out << QStringLiteral("%1→%2:%3").arg(r.targetText, r.targetType).arg(r.targetNoteId);
        return out;
    };
    if (renamed || relinked.value() || oldAliases != noteAliases(note.body)
        || targets(oldLinks.value()) != targets(newLinks.value()))
        emit linksChanged();
    return stored;
}

Result<NoteRecord> NoteService::modify(qint64 id, const std::function<void(NoteRecord&)>& change)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    NoteRecord n = current.value();
    change(n);
    if (auto r = m_repo.update(n); !r)
        return r.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit noteChanged(stored.value());
    return stored;
}

Result<NoteRecord> NoteService::setType(qint64 id, NoteType type)
{
    return modify(id, [type](NoteRecord& n) { n.type = type; });
}

Result<NoteRecord> NoteService::setFolder(qint64 id, const QString& folder)
{
    const QString f = normalizeFolder(folder);
    return modify(id, [f](NoteRecord& n) { n.folder = f; });
}

Result<NoteRecord> NoteService::setPinned(qint64 id, bool pinned)
{
    return modify(id, [pinned](NoteRecord& n) { n.pinned = pinned; });
}

Result<int> NoteService::renameFolder(const QString& from, const QString& to)
{
    const QString src = normalizeFolder(from);
    const QString dst = normalizeFolder(to);
    if (src.isEmpty())
        return invalid(tr("Dossier inconnu."));
    if (src == dst)
        return 0;
    if (dst.startsWith(src + u'/'))
        return invalid(tr("Un dossier ne peut pas être déplacé dans l'un de ses sous-dossiers."));
    auto all = m_repo.list();
    if (!all)
        return all.error();
    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    QList<qint64> moved;
    for (NoteRecord n : all.value()) {
        if (n.folder != src && !n.folder.startsWith(src + u'/'))
            continue;
        n.folder = normalizeFolder(dst + n.folder.mid(src.size()));
        if (auto r = m_repo.update(n); !r)
            return r.error();
        moved << n.id;
    }
    if (auto r = tx.commit(); !r)
        return r.error();
    for (qint64 id : std::as_const(moved))
        if (auto n = m_repo.findById(id))
            emit noteChanged(n.value());
    return int(moved.size());
}

Result<void> NoteService::remove(qint64 id)
{
    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    if (auto r = m_repo.remove(id); !r)
        return r;
    if (auto r = relinkAll(); !r)
        return r.error();
    if (auto r = tx.commit(); !r)
        return r;
    emit noteRemoved(id);
    emit linksChanged();
    return {};
}

Result<NoteRecord> NoteService::restore(qint64 id)
{
    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    if (auto r = m_repo.restore(id); !r)
        return r.error();
    if (auto r = relinkAll(); !r)
        return r.error();
    if (auto r = tx.commit(); !r)
        return r.error();
    auto stored = m_repo.findById(id);
    if (stored) {
        emit noteAdded(stored.value());
        emit linksChanged();
    }
    return stored;
}

Result<NoteRecord> NoteService::find(qint64 id) { return m_repo.findById(id); }
Result<QList<NoteRecord>> NoteService::list() { return m_repo.list(); }
Result<int> NoteService::count() { return m_repo.count(); }

Result<qint64> NoteService::resolve(const QString& target)
{
    if (target.trimmed().startsWith(u'@'))
        return qint64(0);
    return keyMap().value(wikiLinkKey(target), 0);
}

NoteLinkTarget NoteService::resolveTarget(const QString& target) { return pick(target, keyMap(), documentMap()); }

QHash<QString, NoteLinkTarget> NoteService::resolveMany(const QStringList& targets)
{
    const QHash<QString, qint64> notes = keyMap();
    const QHash<QString, qint64> docs = documentMap();
    QHash<QString, NoteLinkTarget> out;
    for (const QString& t : targets)
        out.insert(t, pick(t, notes, docs));
    return out;
}

Result<bool> NoteService::refreshLinks()
{
    auto changed = relinkAll();
    if (changed && changed.value())
        emit linksChanged();
    return changed;
}

QStringList NoteService::linkCompletions()
{
    QStringList out;
    QStringList keys;
    auto add = [&](const QString& title) {
        const QString key = wikiLinkKey(title);
        if (key.isEmpty() || keys.contains(key))
            return;
        keys << key;
        out << title;
    };
    if (auto notes = m_repo.keys())
        for (const auto& k : notes.value()) {
            add(k.title);
            for (const QString& a : k.aliases)
                add(a);
        }
    if (auto docs = m_repo.documentKeys())
        for (const auto& d : docs.value())
            add(d.title);
    return out;
}

Result<NoteRecord> NoteService::createDerived(const QString& title, NoteType type, const QString& folder,
                                              const QString& body, const QList<NoteSourceRef>& sources)
{
    QString t = title.simplified();
    if (t.isEmpty())
        t = tr("Sans titre");
    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto slug = uniqueSlug(t, 0);
    if (!slug)
        return slug.error();
    NoteRecord note;
    note.title = slug.value() == noteSlug(t) ? t : slug.value();
    note.slug = slug.value();
    note.type = type;
    note.folder = normalizeFolder(folder);
    note.body = body;
    note.frontMatter = parseFrontMatter(body).values;
    if (auto r = m_repo.insert(note); !r)
        return r.error();
    if (auto r = m_repo.addSources(note.id, sources); !r)
        return r.error();
    if (auto r = indexNote(note); !r)
        return r.error();
    if (auto r = relinkAll(); !r)
        return r.error();
    (void)db::ActivityRepository(m_db).log(QStringLiteral("note_created"), QStringLiteral("note"), note.id);
    if (auto r = tx.commit(); !r)
        return r.error();
    auto stored = m_repo.findById(note.id);
    if (stored) {
        emit noteAdded(stored.value());
        emit linksChanged();
    }
    return stored;
}

Result<NoteImportReport> NoteService::importNotes(const QList<NoteImport>& notes)
{
    NoteImportReport report;
    auto existing = m_repo.list();
    if (!existing)
        return existing.error();
    // Doublon de contenu : même titre (sans casse), même dossier, même texte.
    // Les images comptent pour leur place, pas leur fichier (jeton à l'import, attachments/<uuid> en base).
    static const QRegularExpression image(QStringLiteral("\\]\\((?:attachments/[\\w.-]+|\\x{2063}studylab-image-\\d+\\x{2063})\\)"));
    auto contentKey = [](const QString& title, const QString& folder, const QString& body) {
        QString text = body.trimmed();
        text.replace(image, QStringLiteral("](image)"));
        return title.simplified().toLower() + QChar(1) + folder.toLower() + QChar(1) + text;
    };
    QSet<QString> seen;
    for (const NoteRecord& n : existing.value())
        seen.insert(contentKey(n.title, n.folder, n.body));

    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    QList<NoteRecord> added;
    for (const NoteImport& in : notes) {
        QString t = in.title.simplified();
        if (t.isEmpty())
            t = tr("Sans titre");
        const QString folder = normalizeFolder(in.folder);
        if (!in.uuid.isEmpty()) {
            if (auto live = m_repo.findByUuid(in.uuid)) {
                ++report.duplicates;
                continue;
            }
        }
        if (seen.contains(contentKey(t, folder, in.body))) {
            ++report.duplicates;
            continue;
        }
        auto slug = uniqueSlug(t, 0);
        if (!slug)
            return slug.error();
        NoteRecord note;
        note.title = slug.value() == noteSlug(t) ? t : slug.value();
        if (note.title != t)
            ++report.renamed;
        note.slug = slug.value();
        note.type = in.type;
        note.folder = folder;
        note.body = in.body;
        if (!in.uuid.isEmpty()) {
            auto taken = m_repo.uuidTaken(in.uuid);
            if (!taken)
                return taken.error();
            if (!taken.value())
                note.uuid = in.uuid;
        }
        note.frontMatter = parseFrontMatter(note.body).values;
        if (auto r = m_repo.insert(note); !r)
            return r.error();
        if (!in.images.isEmpty()) {
            for (const auto& [token, file] : in.images) {
                QFile f(file);
                if (!f.open(QIODevice::ReadOnly)) {
                    report.warnings << tr("%1 : image illisible « %2 ».").arg(in.sourcePath, QFileInfo(file).fileName());
                    continue;
                }
                auto rel = addImage(note.id, f.readAll(), QFileInfo(file).suffix());
                if (!rel) {
                    report.warnings << QStringLiteral("%1 : %2").arg(in.sourcePath, rel.error().message);
                    continue;
                }
                note.body.replace(token, rel.value());
                ++report.images;
            }
            note.frontMatter = parseFrontMatter(note.body).values;
            if (auto r = m_repo.update(note); !r)
                return r.error();
        }
        if (auto r = m_repo.setAliases(note.id, noteAliases(note.body)); !r)
            return r.error();
        seen.insert(contentKey(note.title, note.folder, note.body));
        added << note;
    }
    // Liens : tables calculées une fois, toutes les notes importées comprises.
    if (!added.isEmpty()) {
        const QHash<QString, qint64> keys = keyMap();
        const QHash<QString, qint64> docs = documentMap();
        for (const NoteRecord& note : std::as_const(added)) {
            QList<db::NoteLinkRow> rows;
            for (const QString& target : noteTargets(note.body)) {
                const NoteLinkTarget t = pick(target, keys, docs);
                db::NoteLinkRow row;
                row.targetText = target;
                row.targetType = typeKey(t);
                row.targetNoteId = t.id;
                rows << row;
            }
            if (auto r = m_repo.replaceWikiLinks(note.id, rows); !r)
                return r.error();
        }
        if (auto r = relinkAll(); !r)
            return r.error();
    }
    if (auto r = tx.commit(); !r)
        return r.error();
    for (const NoteRecord& note : std::as_const(added)) {
        report.noteIds << note.id;
        if (auto stored = m_repo.findById(note.id))
            emit noteAdded(stored.value());
    }
    report.imported = int(added.size());
    if (!added.isEmpty())
        emit linksChanged();
    return report;
}

Result<QList<NoteSource>> NoteService::sources(qint64 id) { return m_repo.sources(id); }

Result<QList<NoteRecord>> NoteService::derivedFrom(const QList<qint64>& documentIds)
{
    auto ids = m_repo.notesDerivedFrom(documentIds);
    if (!ids)
        return ids.error();
    QList<NoteRecord> out;
    for (qint64 id : ids.value())
        if (auto n = m_repo.findById(id))
            out << n.value();
    return out;
}

Result<QList<NoteBacklink>> NoteService::backlinks(qint64 id)
{
    auto note = m_repo.findById(id);
    if (!note)
        return note.error();
    auto incoming = m_repo.incomingLinks(id);
    if (!incoming)
        return incoming.error();
    QList<NoteBacklink> out;
    QList<qint64> seen;
    for (const auto& l : incoming.value()) {
        if (seen.contains(l.sourceNoteId))
            continue;
        seen << l.sourceNoteId;
        auto source = m_repo.findById(l.sourceNoteId);
        if (!source)
            continue;
        QStringList keys{wikiLinkKey(l.targetText)};
        out.append({source.value().id, source.value().title, source.value().type,
                    wikiLinkSnippet(source.value().body, keys)});
    }
    return out;
}

Result<QList<NoteLink>> NoteService::links(qint64 id)
{
    auto rows = m_repo.outgoingLinks(id);
    if (!rows)
        return rows.error();
    QList<NoteLink> out;
    for (const auto& r : rows.value()) {
        NoteLink l;
        l.target = r.targetText;
        if (r.targetNoteId > 0 && r.targetType == QLatin1String("document"))
            l.documentId = r.targetNoteId;
        else
            l.noteId = r.targetNoteId;
        out << l;
    }
    return out;
}

Result<QString> NoteService::addImage(qint64 noteId, const QByteArray& data, const QString& extension)
{
    static const QStringList allowed{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                     QStringLiteral("gif"), QStringLiteral("webp")};
    const QString ext = extension.toLower();
    if (data.isEmpty() || !allowed.contains(ext))
        return invalid(tr("Format d'image non pris en charge."));
    if (!QDir().mkpath(m_attachmentsDir))
        return Error::make(Error::Code::Io, tr("Impossible de créer le dossier %1").arg(m_attachmentsDir));
    const QString uuid = newUuid();
    const QString fileName = QStringLiteral("%1.%2").arg(uuid, ext);
    QSaveFile file(QDir(m_attachmentsDir).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error::make(Error::Code::Io, tr("Impossible d'enregistrer l'image : %1").arg(file.errorString()));
    const QString rel = QStringLiteral("attachments/") + fileName;
    const QString mime = ext == QLatin1String("jpg") ? QStringLiteral("image/jpeg") : QStringLiteral("image/") + ext;
    if (auto r = m_repo.addAttachment(noteId, uuid, fileName, rel, mime); !r)
        return r.error();
    return rel;
}

}  // namespace studylab
