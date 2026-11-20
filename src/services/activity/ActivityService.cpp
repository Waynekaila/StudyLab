#include "services/activity/ActivityService.h"

#include <QHash>

#include <algorithm>
#include <map>

namespace studylab {

namespace {

QDateTime parseUtc(const QString& isoText)
{
    QDateTime dt = QDateTime::fromString(isoText, Qt::ISODate);
    if (dt.timeSpec() != Qt::UTC)
        dt = dt.toUTC();
    return dt;
}

/// Clé « document:page » d'une page vue.
qint64 pageKey(qint64 documentId, int page) { return (documentId << 24) ^ qint64(page); }

}  // namespace

ActivityService::ActivityService(db::Database& db, QObject* parent) : QObject(parent), m_db(db), m_repo(db) {}

QDateTime ActivityService::now() const
{
    return m_clock ? m_clock().toUTC() : QDateTime::currentDateTimeUtc();
}

QDate ActivityService::activityDay(const QDateTime& utc)
{
    const QDateTime local = utc.toLocalTime();
    return local.time() < QTime(4, 0) ? local.date().addDays(-1) : local.date();
}

QDateTime ActivityService::dayStartUtc(const QDate& day)
{
    return QDateTime(day, QTime(4, 0)).toUTC();
}

QString ActivityService::iso(const QDateTime& utc) const
{
    return utc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
}

Result<void> ActivityService::saveSession(db::ReadingSessionRow& session)
{
    if (session.documentId <= 0)
        return Error::make(Error::Code::InvalidArgument, tr("Séance de lecture sans document."));
    const QString at = iso(now());
    if (session.startedAt.isEmpty())
        session.startedAt = at;
    session.endedAt = at;
    auto r = m_repo.saveSession(session);
    if (r)
        emit changed();
    return r;
}

Result<Goals> ActivityService::goals()
{
    auto rows = m_repo.goals();
    if (!rows)
        return rows.error();
    Goals g;
    for (const db::GoalRow& row : rows.value()) {
        if (row.kind == QLatin1String("pages_per_week"))
            g.pagesPerWeek = row.target;
        else if (row.kind == QLatin1String("minutes_per_day"))
            g.minutesPerDay = row.target;
        else if (row.kind == QLatin1String("reviews_per_day"))
            g.reviewsPerDay = row.target;
    }
    return g;
}

Result<void> ActivityService::setGoals(const Goals& goals)
{
    if (goals.pagesPerWeek < 0 || goals.minutesPerDay < 0 || goals.reviewsPerDay < 0)
        return Error::make(Error::Code::InvalidArgument, tr("Un objectif ne peut pas être négatif."));
    if (auto r = m_repo.setGoal(QStringLiteral("pages_per_week"), goals.pagesPerWeek); !r)
        return r;
    if (auto r = m_repo.setGoal(QStringLiteral("minutes_per_day"), goals.minutesPerDay); !r)
        return r;
    if (auto r = m_repo.setGoal(QStringLiteral("reviews_per_day"), goals.reviewsPerDay); !r)
        return r;
    emit changed();
    return {};
}

Result<QList<DayActivity>> ActivityService::days(const QDate& from, const QDate& to)
{
    QList<DayActivity> out;
    if (!from.isValid() || !to.isValid() || to < from)
        return out;
    const QString fromIso = iso(dayStartUtc(from));
    const QString toIso = iso(dayStartUtc(to.addDays(1)));
    auto sessions = m_repo.sessions(fromIso, toIso);
    if (!sessions)
        return sessions.error();
    auto entries = m_repo.actionTimes(fromIso, toIso);
    if (!entries)
        return entries.error();

    const int n = int(from.daysTo(to)) + 1;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out << DayActivity{from.addDays(i)};
    QList<QSet<qint64>> pages(n);
    // Début (UTC, au format stocké) de chaque journée et de la suivante : une recherche dichotomique sur le texte
    // range chaque horodatage sans le convertir en heure locale (coûteux sur des dizaines de milliers de lignes).
    QStringList bounds;
    bounds.reserve(n + 1);
    for (int i = 0; i <= n; ++i)
        bounds << iso(dayStartUtc(from.addDays(i)));
    auto indexOf = [&](const QString& at) {
        // Horodatage au format stocké (« …Z », à la seconde) ; sinon, conversion complète.
        if (at.size() != 20 || !at.endsWith(u'Z')) {
            const qint64 i = from.daysTo(activityDay(parseUtc(at)));
            return i >= 0 && i < n ? int(i) : -1;
        }
        const auto it = std::upper_bound(bounds.cbegin(), bounds.cend(), at);
        const int i = int(it - bounds.cbegin()) - 1;
        return i >= 0 && i < n ? i : -1;
    };
    for (const db::ReadingSessionRow& s : sessions.value()) {
        const int i = indexOf(s.startedAt);
        if (i < 0)
            continue;
        out[i].seconds += s.seconds;
        for (int p : s.pages)
            pages[i].insert(pageKey(s.documentId, p));
    }
    for (int i = 0; i < n; ++i)
        out[i].pages = int(pages[i].size());
    for (const auto& [at, action] : entries.value()) {
        const int i = indexOf(at);
        if (i < 0)
            continue;
        if (action == QLatin1String("annotated"))
            ++out[i].annotations;
        else if (action == QLatin1String("commented"))
            ++out[i].comments;
        else if (action == QLatin1String("note_created"))
            ++out[i].notes;
        else if (action == QLatin1String("reviewed"))
            ++out[i].reviews;
        else if (action == QLatin1String("imported"))
            ++out[i].imported;
    }
    return out;
}

Result<PeriodTotals> ActivityService::totals(const QDate& from, const QDate& to)
{
    PeriodTotals t;
    if (!from.isValid() || !to.isValid() || to < from)
        return t;
    auto list = days(from, to);
    if (!list)
        return list.error();
    for (const DayActivity& d : list.value()) {
        t.seconds += d.seconds;
        t.annotations += d.annotations;
        t.comments += d.comments;
        t.notes += d.notes;
        t.reviews += d.reviews;
        t.imported += d.imported;
        if (d.any())
            ++t.activeDays;
    }
    // Pages et documents distincts sur toute la période (une page relue un autre jour ne compte qu'une fois).
    const QString fromIso = iso(dayStartUtc(from));
    const QString toIso = iso(dayStartUtc(to.addDays(1)));
    auto sessions = m_repo.sessions(fromIso, toIso);
    if (!sessions)
        return sessions.error();
    QSet<qint64> pages;
    QSet<qint64> docs;
    for (const db::ReadingSessionRow& s : sessions.value()) {
        if (s.seconds > 0 || !s.pages.isEmpty())
            docs.insert(s.documentId);
        for (int p : s.pages)
            pages.insert(pageKey(s.documentId, p));
    }
    t.pages = int(pages.size());
    t.documents = int(docs.size());
    auto entries = m_repo.entries(fromIso, toIso);
    if (!entries)
        return entries.error();
    for (const db::ActivityRow& e : entries.value())
        if (e.action == QLatin1String("reviewed") && e.detail.value(QStringLiteral("rating")).toInt() > 1)
            ++t.reviewsPassed;
    return t;
}

Result<WeekProgress> ActivityService::week()
{
    WeekProgress w;
    w.today = today();
    w.monday = w.today.addDays(1 - w.today.dayOfWeek());
    auto current = totals(w.monday, w.today);
    if (!current)
        return current.error();
    auto previous = totals(w.monday.addDays(-7), w.today.addDays(-7));
    if (!previous)
        return previous.error();
    auto day = days(w.today, w.today);
    if (!day)
        return day.error();
    auto g = goals();
    if (!g)
        return g.error();
    w.pages = current.value().pages;
    w.seconds = current.value().seconds;
    w.previousPages = previous.value().pages;
    w.previousSeconds = previous.value().seconds;
    w.todaySeconds = day.value().first().seconds;
    w.todayReviews = day.value().first().reviews;
    w.goals = g.value();
    return w;
}

Result<int> ActivityService::streak()
{
    // Remonte par tranches de 5 semaines jusqu'au premier jour sans activité (au plus 400 jours) : une série est
    // le plus souvent courte, inutile de relire une année d'activité.
    const QDate t = today();
    int n = 0;
    bool first = true;
    for (QDate end = t; end > t.addDays(-400);) {
        const QDate start = std::max(end.addDays(-34), t.addDays(-400));
        auto list = days(start, end);
        if (!list)
            return list.error();
        const QList<DayActivity>& d = list.value();
        int i = int(d.size()) - 1;
        if (first && i >= 0 && !d[i].any())
            --i;  // rien encore aujourd'hui : la série d'hier tient toujours
        first = false;
        for (; i >= 0 && d[i].any(); --i)
            ++n;
        if (i >= 0)
            return n;
        end = start.addDays(-1);
    }
    return n;
}

Result<QList<JournalEntry>> ActivityService::journal(const QDate& from, const QDate& to)
{
    QList<JournalEntry> out;
    if (!from.isValid() || !to.isValid() || to < from)
        return out;
    const QString fromIso = iso(dayStartUtc(from));
    const QString toIso = iso(dayStartUtc(to.addDays(1)));
    auto sessions = m_repo.sessions(fromIso, toIso);
    if (!sessions)
        return sessions.error();
    auto entries = m_repo.entries(fromIso, toIso);
    if (!entries)
        return entries.error();

    // Groupes : (jour, genre, document) → entrée.
    std::map<std::tuple<qint64, int, qint64>, JournalEntry> groups;
    auto group = [&](const QDateTime& atUtc, JournalEntry::Kind kind, qint64 key) -> JournalEntry& {
        const QDate day = activityDay(atUtc);
        auto [it, inserted] = groups.try_emplace({day.toJulianDay(), int(kind), key});
        JournalEntry& e = it->second;
        if (inserted) {
            e.kind = kind;
            e.day = day;
            e.count = 0;
        }
        const QDateTime local = atUtc.toLocalTime();
        if (!e.at.isValid() || local > e.at)
            e.at = local;
        return e;
    };
    std::map<std::tuple<qint64, qint64>, QSet<int>> readingPages;
    for (const db::ReadingSessionRow& s : sessions.value()) {
        if (s.seconds <= 0 && s.pages.isEmpty())
            continue;
        const QDateTime end = parseUtc(s.endedAt.isEmpty() ? s.startedAt : s.endedAt);
        const QDate day = activityDay(parseUtc(s.startedAt));
        auto [it, inserted] = groups.try_emplace({day.toJulianDay(), int(JournalEntry::Kind::Reading), s.documentId});
        JournalEntry& e = it->second;
        if (inserted) {
            e.kind = JournalEntry::Kind::Reading;
            e.day = day;
            e.count = 0;
        }
        if (!e.at.isValid() || end.toLocalTime() > e.at)
            e.at = end.toLocalTime();
        e.documentId = s.documentId;
        e.count += 1;
        e.seconds += s.seconds;
        auto& set = readingPages[{day.toJulianDay(), s.documentId}];
        for (int p : s.pages)
            set.insert(p);
        e.pages = int(set.size());
    }
    for (const db::ActivityRow& a : entries.value()) {
        const QDateTime at = parseUtc(a.at);
        const qint64 doc = a.detail.value(QStringLiteral("document")).toInteger();
        if (a.action == QLatin1String("annotated") || a.action == QLatin1String("commented")) {
            const auto kind = a.action == QLatin1String("annotated") ? JournalEntry::Kind::Annotations
                                                                     : JournalEntry::Kind::Comments;
            JournalEntry& e = group(at, kind, doc);
            if (e.count == 0)
                e.itemId = a.entityId;  // entrées lues des plus récentes aux plus anciennes : la plus récente
            e.documentId = doc;
            ++e.count;
        } else if (a.action == QLatin1String("note_created")) {
            JournalEntry& e = group(at, JournalEntry::Kind::Note, a.entityId);
            e.noteId = a.entityId;
            ++e.count;
        } else if (a.action == QLatin1String("reviewed")) {
            JournalEntry& e = group(at, JournalEntry::Kind::Reviews, 0);
            ++e.count;
            if (a.detail.value(QStringLiteral("rating")).toInt() > 1)
                ++e.passed;
        } else if (a.action == QLatin1String("imported")) {
            JournalEntry& e = group(at, JournalEntry::Kind::Imported, 0);
            e.documentId = e.count == 0 ? a.entityId : 0;
            ++e.count;
        } else if (a.action == QLatin1String("opened")) {
            JournalEntry& e = group(at, JournalEntry::Kind::Opened, a.entityId);
            e.documentId = a.entityId;
            ++e.count;
        }
    }

    QList<qint64> docIds;
    QList<qint64> noteIds;
    for (const auto& [key, e] : groups) {
        if (e.documentId > 0)
            docIds << e.documentId;
        if (e.noteId > 0)
            noteIds << e.noteId;
    }
    auto docTitles = m_repo.documentTitles(docIds);
    if (!docTitles)
        return docTitles.error();
    auto noteTitles = m_repo.noteTitles(noteIds);
    if (!noteTitles)
        return noteTitles.error();

    for (auto& [key, e] : groups) {
        const qint64 day = std::get<0>(key);
        if (e.kind == JournalEntry::Kind::Opened
            && groups.count({day, int(JournalEntry::Kind::Reading), e.documentId}) > 0)
            continue;  // la séance de lecture du même jour suffit
        if (e.documentId > 0) {
            e.title = docTitles.value().value(e.documentId);
            if (e.title.isEmpty() && e.kind != JournalEntry::Kind::Imported)
                continue;  // document retiré de la bibliothèque
        }
        if (e.kind == JournalEntry::Kind::Note) {
            e.title = noteTitles.value().value(e.noteId);
            if (e.title.isEmpty())
                continue;  // note supprimée
        }
        out << e;
    }
    std::sort(out.begin(), out.end(), [](const JournalEntry& a, const JournalEntry& b) { return a.at > b.at; });
    return out;
}

QString formatDuration(int seconds)
{
    if (seconds < 60)
        return seconds > 0 ? QObject::tr("< 1 min") : QObject::tr("0 min");
    const int minutes = seconds / 60;
    if (minutes < 60)
        return QObject::tr("%1 min").arg(minutes);
    const int m = minutes % 60;
    return m == 0 ? QObject::tr("%1 h").arg(minutes / 60)
                  : QObject::tr("%1 h %2").arg(minutes / 60).arg(m, 2, 10, QLatin1Char('0'));
}

}  // namespace studylab
