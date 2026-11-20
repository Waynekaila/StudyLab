#include "services/references/ReferenceService.h"

#include "data/repositories/DocumentRepository.h"
#include "data/repositories/ReferenceRepository.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Transaction.h"
#include "services/notes/NoteService.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ReferenceService", text);
}

/// Chiffres et X d'un ISBN (« 978-2-07-036818-1 » → « 9782070368181 »).
QString normalizeIsbn(const QString& isbn)
{
    QString out;
    for (const QChar c : isbn) {
        if (c.isDigit() || c == u'X' || c == u'x')
            out += c.toUpper();
    }
    return out;
}

QString normalizeDoi(QString doi)
{
    doi = doi.trimmed().toLower();
    static const QRegularExpression prefix(QStringLiteral("^(https?://(dx\\.)?doi\\.org/|doi:)"));
    doi.remove(prefix);
    return doi;
}

QString titleKey(const QString& title)
{
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    return foldForSort(title).replace(nonWord, QStringLiteral(" ")).simplified();
}

bool sameWork(const Reference& a, const Reference& b)
{
    if (!a.doi.isEmpty() && !b.doi.isEmpty())
        return normalizeDoi(a.doi) == normalizeDoi(b.doi);
    if (!a.isbn.isEmpty() && !b.isbn.isEmpty() && normalizeIsbn(a.isbn) == normalizeIsbn(b.isbn))
        return true;
    return a.citekey.compare(b.citekey, Qt::CaseInsensitive) == 0 && titleKey(a.title) == titleKey(b.title);
}

QString sortKey(const Reference& r)
{
    const QList<PersonName>& people = r.authors.isEmpty() ? r.editors : r.authors;
    const QString who = people.isEmpty() ? r.title : people.first().sortName();
    return foldForSort(who) + u'\x1f' + QStringLiteral("%1").arg(r.year, 4, 10, QLatin1Char('0')) + u'\x1f'
           + foldForSort(r.title);
}

/// Remplace la clé dans les citations [[@ancienne…]] (suffixes « |texte » et « #p. 12 » conservés).
QString rewriteCitations(const QString& body, const QString& from, const QString& to, bool* changed)
{
    const QRegularExpression cite(QStringLiteral("\\[\\[@%1(?=[\\]|#])").arg(QRegularExpression::escape(from)),
                                  QRegularExpression::CaseInsensitiveOption);
    QString out = body;
    out.replace(cite, QStringLiteral("[[@") + to);
    *changed = out != body;
    return out;
}

}  // namespace

ReferenceService::ReferenceService(db::Database& db, NoteService& notes) : m_db(db), m_notes(notes) {}

Result<QList<Reference>> ReferenceService::list()
{
    auto all = db::ReferenceRepository(m_db).all();
    if (!all)
        return all;
    QList<Reference> refs = std::move(all).value();
    std::stable_sort(refs.begin(), refs.end(),
                     [](const Reference& a, const Reference& b) { return sortKey(a) < sortKey(b); });
    return refs;
}

Result<Reference> ReferenceService::get(qint64 id)
{
    return db::ReferenceRepository(m_db).get(id);
}

Result<Reference> ReferenceService::byCitekey(const QString& citekey)
{
    return db::ReferenceRepository(m_db).byCitekey(citekey.trimmed());
}

Result<QList<Reference>> ReferenceService::forDocument(qint64 documentId)
{
    return db::ReferenceRepository(m_db).byDocument(documentId);
}

Result<QSet<QString>> ReferenceService::takenKeys(qint64 exceptId)
{
    auto all = db::ReferenceRepository(m_db).all();
    if (!all)
        return all.error();
    QSet<QString> keys;
    for (const Reference& r : all.value()) {
        if (r.id != exceptId)
            keys.insert(r.citekey.toLower());
    }
    return keys;
}

Result<Reference> ReferenceService::create(Reference ref)
{
    ref.id = 0;
    ref.citekey = ref.citekey.trimmed();
    ref.title = ref.title.trimmed();
    if (!ref.citekey.isEmpty() && !isValidCitekey(ref.citekey))
        return Error::make(Error::Code::InvalidArgument,
                           tr("Clé « %1 » invalide : lettres, chiffres et « _ : . - » seulement, sans espace.")
                               .arg(ref.citekey));
    auto taken = takenKeys();
    if (!taken)
        return taken.error();
    if (ref.citekey.isEmpty() || taken.value().contains(ref.citekey.toLower())) {
        Reference basis = ref;
        if (!ref.citekey.isEmpty()) {
            // Clé imposée mais prise : on garde son radical.
            QSet<QString> keys = taken.value();
            QString key;
            for (char c = 'a'; c <= 'z' && key.isEmpty(); ++c) {
                if (!keys.contains((ref.citekey + QLatin1Char(c)).toLower()))
                    key = ref.citekey + QLatin1Char(c);
            }
            ref.citekey = key.isEmpty() ? makeCitekey(basis, keys) : key;
        } else {
            ref.citekey = makeCitekey(basis, taken.value());
        }
    }
    return db::ReferenceRepository(m_db).insert(ref);
}

Result<Reference> ReferenceService::update(const Reference& input, int* rewrittenNotes)
{
    if (rewrittenNotes)
        *rewrittenNotes = 0;
    Reference ref = input;
    ref.citekey = ref.citekey.trimmed();
    ref.title = ref.title.trimmed();
    if (!isValidCitekey(ref.citekey))
        return Error::make(Error::Code::InvalidArgument,
                           tr("Clé « %1 » invalide : lettres, chiffres et « _ : . - » seulement, sans espace.")
                               .arg(ref.citekey));
    db::ReferenceRepository repo(m_db);
    auto before = repo.get(ref.id);
    if (!before)
        return before.error();
    auto taken = takenKeys(ref.id);
    if (!taken)
        return taken.error();
    if (taken.value().contains(ref.citekey.toLower()))
        return Error::make(Error::Code::InvalidArgument, tr("La clé « %1 » est déjà employée.").arg(ref.citekey));
    if (auto r = repo.update(ref); !r)
        return r.error();

    const QString oldKey = before.value().citekey;
    if (oldKey != ref.citekey) {
        auto notes = m_notes.list();
        if (!notes)
            return notes.error();
        for (const NoteRecord& n : notes.value()) {
            bool changed = false;
            const QString body = rewriteCitations(n.body, oldKey, ref.citekey, &changed);
            if (!changed)
                continue;
            if (auto saved = m_notes.save(n.id, n.title, body); !saved)
                return saved.error();
            if (rewrittenNotes)
                ++*rewrittenNotes;
        }
    }
    return repo.get(ref.id);
}

Result<void> ReferenceService::remove(qint64 id)
{
    return db::ReferenceRepository(m_db).remove(id);
}

Reference ReferenceService::draftFromDocument(const DocumentRecord& doc)
{
    Reference r;
    r.documentId = doc.id;
    r.type = QStringLiteral("book");
    r.title = doc.subtitle.isEmpty() ? doc.title : doc.title + QStringLiteral(" : ") + doc.subtitle;
    for (const QString& a : doc.authors) {
        const PersonName p = parsePersonName(a);
        if (!p.isEmpty())
            r.authors << p;
    }
    r.year = doc.year;
    r.publisher = doc.publisher;
    r.isbn = doc.isbn;
    r.doi = doc.doi;
    r.language = doc.language;
    if (!doc.doi.isEmpty() && doc.isbn.isEmpty())
        r.type = QStringLiteral("article-journal");
    return r;
}

Result<Reference> ReferenceService::ensureForDocument(qint64 documentId)
{
    auto existing = forDocument(documentId);
    if (!existing)
        return existing.error();
    if (!existing.value().isEmpty())
        return existing.value().first();
    auto doc = db::DocumentRepository(m_db).findById(documentId);
    if (!doc)
        return doc.error();
    return create(draftFromDocument(doc.value()));
}

Result<ReferenceImportReport> ReferenceService::importFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, tr("Lecture impossible : %1").arg(QFileInfo(path).fileName()));
    const QByteArray data = f.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("json")) {
        auto refs = parseCslJson(data);
        if (!refs)
            return refs.error();
        return importReferences(refs.value());
    }
    if (suffix == QLatin1String("bib") || suffix == QLatin1String("bibtex") || suffix == QLatin1String("txt")) {
        const BibtexParse parsed = parseBibtex(QString::fromUtf8(data));
        if (parsed.references.isEmpty() && parsed.warnings.isEmpty())
            return Error::make(Error::Code::InvalidArgument, tr("Aucune référence BibTeX dans ce fichier."));
        return importReferences(parsed.references, parsed.warnings);
    }
    return Error::make(Error::Code::InvalidArgument,
                       tr("Format non pris en charge : .%1 (attendu : .bib ou .json CSL).").arg(suffix));
}

Result<ReferenceImportReport> ReferenceService::importReferences(const QList<Reference>& refs, const QStringList& warnings)
{
    ReferenceImportReport report;
    report.warnings = warnings;
    auto existingRes = db::ReferenceRepository(m_db).all();
    if (!existingRes)
        return existingRes.error();
    QList<Reference> existing = existingRes.value();
    auto docsRes = db::DocumentRepository(m_db).list();
    if (!docsRes)
        return docsRes.error();
    const QList<DocumentRecord> docs = docsRes.value();

    db::Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    for (Reference r : refs) {
        if (r.title.isEmpty() && r.authors.isEmpty()) {
            report.warnings << tr("« %1 » : ni titre ni auteur, ignorée.").arg(r.citekey);
            continue;
        }
        if (std::any_of(existing.cbegin(), existing.cend(), [&r](const Reference& e) { return sameWork(e, r); })) {
            ++report.duplicates;
            continue;
        }
        if (!r.citekey.isEmpty() && !isValidCitekey(r.citekey)) {
            report.warnings << tr("Clé « %1 » invalide, remplacée.").arg(r.citekey);
            r.citekey.clear();
        }
        const bool keyTaken = !r.citekey.isEmpty()
                              && std::any_of(existing.cbegin(), existing.cend(), [&r](const Reference& e) {
                                     return e.citekey.compare(r.citekey, Qt::CaseInsensitive) == 0;
                                 });
        // Rattachement au document de la bibliothèque : DOI, puis ISBN, puis titre identique.
        if (r.documentId == 0) {
            const auto doc = std::find_if(docs.cbegin(), docs.cend(), [&r](const DocumentRecord& d) {
                if (!r.doi.isEmpty() && !d.doi.isEmpty())
                    return normalizeDoi(r.doi) == normalizeDoi(d.doi);
                if (!r.isbn.isEmpty() && !d.isbn.isEmpty() && normalizeIsbn(r.isbn) == normalizeIsbn(d.isbn))
                    return true;
                return !r.title.isEmpty() && titleKey(r.title) == titleKey(d.title);
            });
            if (doc != docs.cend()) {
                r.documentId = doc->id;
                ++report.linked;
            }
        }
        auto created = create(r);
        if (!created)
            return created.error();
        if (keyTaken)
            ++report.renamed;
        existing << created.value();
        report.addedIds << created.value().id;
        ++report.added;
    }
    if (auto c = tx.commit(); !c)
        return c.error();
    return report;
}

Result<QList<Reference>> ReferenceService::selected(const QList<qint64>& ids)
{
    auto all = list();
    if (!all || ids.isEmpty())
        return all;
    QList<Reference> out;
    for (const Reference& r : all.value()) {
        if (ids.contains(r.id))
            out << r;
    }
    return out;
}

Result<QString> ReferenceService::bibtex(const QList<qint64>& ids)
{
    auto refs = selected(ids);
    if (!refs)
        return refs.error();
    return writeBibtex(refs.value());
}

Result<int> ReferenceService::exportFile(const QString& path, const QList<qint64>& ids)
{
    auto refs = selected(ids);
    if (!refs)
        return refs.error();
    const bool json = QFileInfo(path).suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) == 0;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return Error::make(Error::Code::Io, tr("Écriture impossible : %1").arg(path));
    f.write(json ? writeCslJson(refs.value()) : writeBibtex(refs.value()).toUtf8());
    if (!f.commit())
        return Error::make(Error::Code::Io, tr("Écriture impossible : %1").arg(path));
    return int(refs.value().size());
}

QHash<QString, Reference> ReferenceService::resolveKeys(const QStringList& keys)
{
    QHash<QString, Reference> out;
    if (keys.isEmpty())
        return out;
    auto all = db::ReferenceRepository(m_db).all();
    if (!all)
        return out;
    QSet<QString> wanted;
    for (const QString& k : keys)
        wanted.insert(k.toLower());
    for (const Reference& r : all.value()) {
        if (wanted.contains(r.citekey.toLower()))
            out.insert(r.citekey.toLower(), r);
    }
    return out;
}

QStringList ReferenceService::completions()
{
    QStringList out;
    auto all = list();
    if (!all)
        return out;
    for (const Reference& r : all.value())
        out << u'@' + r.citekey;
    return out;
}

Result<QList<QPair<qint64, QString>>> ReferenceService::citingNotes(const QString& citekey)
{
    auto notes = m_notes.list();
    if (!notes)
        return notes.error();
    QList<QPair<qint64, QString>> out;
    for (const NoteRecord& n : notes.value()) {
        const QStringList keys = citedKeys(n.body);
        if (std::any_of(keys.cbegin(), keys.cend(),
                        [&citekey](const QString& k) { return k.compare(citekey, Qt::CaseInsensitive) == 0; }))
            out.append({n.id, n.title});
    }
    return out;
}

}  // namespace studylab
