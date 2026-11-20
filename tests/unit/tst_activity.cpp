#include "data/migrations/Migrator.h"
#include "data/repositories/ActivityRepository.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "services/activity/ActivityService.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

/// Journal d'activité, temps de lecture et objectifs (V2-4).
class TstActivity : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    QDateTime m_now;
    qint64 m_docA = 0;
    qint64 m_docB = 0;

    /// Instant UTC d'une heure locale.
    static QDateTime local(const QDate& d, int h, int m = 0) { return QDateTime(d, QTime(h, m)).toUTC(); }
    static QString iso(const QDateTime& utc) { return utc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")); }

    std::unique_ptr<ActivityService> service()
    {
        auto s = std::make_unique<ActivityService>(*m_db);
        s->setClock([this] { return m_now; });
        return s;
    }

    qint64 addDocument(const QString& title, const QString& hash)
    {
        DocumentRecord d;
        d.title = title;
        d.fileHash = hash;
        d.filePath = hash + QStringLiteral(".pdf");
        d.pageCount = 100;
        DocumentRepository repo(*m_db);
        if (!repo.insert(d).ok())
            return 0;
        return d.id;
    }

    /// Séance enregistrée « à » un instant donné (début et fin à cet instant).
    ReadingSessionRow session(ActivityService& s, qint64 doc, const QDateTime& at, int seconds, QList<int> pages)
    {
        m_now = at;
        ReadingSessionRow r;
        r.documentId = doc;
        r.seconds = seconds;
        r.pages = pages;
        const bool ok = s.saveSession(r).ok();
        Q_ASSERT(ok);
        Q_UNUSED(ok);
        return r;
    }

    int count(const QString& sql)
    {
        auto v = m_db->queryInt(sql);
        return v ? int(v.value()) : -1;
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
        m_docA = addDocument(QStringLiteral("Thermodynamique"), QStringLiteral("aaa"));
        m_docB = addDocument(QStringLiteral("Mécanique des fluides"), QStringLiteral("bbb"));
        QVERIFY(m_docA > 0 && m_docB > 0);
        // Jeudi 19 novembre 2026, 10 h locale
        m_now = local(QDate(2026, 11, 19), 10);
    }

    void dayBoundaryAndDuration()
    {
        QCOMPARE(ActivityService::activityDay(local(QDate(2026, 11, 17), 3, 59)), QDate(2026, 11, 16));
        QCOMPARE(ActivityService::activityDay(local(QDate(2026, 11, 17), 4, 0)), QDate(2026, 11, 17));
        QCOMPARE(ActivityService::activityDay(local(QDate(2026, 11, 17), 23, 30)), QDate(2026, 11, 17));
        QCOMPARE(ActivityService::dayStartUtc(QDate(2026, 11, 17)), local(QDate(2026, 11, 17), 4));
        QCOMPARE(formatDuration(0), QStringLiteral("0 min"));
        QCOMPARE(formatDuration(59), QStringLiteral("< 1 min"));
        QCOMPARE(formatDuration(600), QStringLiteral("10 min"));
        QCOMPARE(formatDuration(3900), QStringLiteral("1 h 05"));
        QCOMPARE(formatDuration(7200), QStringLiteral("2 h"));
    }

    void sessionsDaysTotalsAndWeek()
    {
        auto s = service();
        QSignalSpy changed(s.get(), &ActivityService::changed);
        const QDate monday(2026, 11, 16);
        const QDate thursday(2026, 11, 19);
        // Semaine précédente : mardi 10
        session(*s, m_docA, local(QDate(2026, 11, 10), 20), 450, {10});
        // Lundi : 10 min, pages 1 à 3 du document A
        ReadingSessionRow mon = session(*s, m_docA, local(monday, 9), 300, {1, 2});
        mon.seconds = 600;
        mon.pages = {1, 2, 3, 2};
        m_now = local(monday, 9, 10);
        QVERIFY(s->saveSession(mon).ok());  // même séance prolongée
        QCOMPARE(count(QStringLiteral("SELECT COUNT(*) FROM reading_sessions WHERE started_at >= '2026-11-15'")), 1);
        QCOMPARE(mon.pages, (QList<int>{1, 2, 3}));  // triées, sans doublon
        // Jeudi : A pages 3 et 4 (5 min), B page 1 (2 min) ; une séance tardive (2 h) compte pour mercredi
        session(*s, m_docA, local(thursday, 8), 300, {3, 4});
        session(*s, m_docB, local(thursday, 9), 120, {1});
        session(*s, m_docB, local(thursday, 2), 60, {7});
        QCOMPARE(changed.count(), 6);
        m_now = local(thursday, 10);

        const auto days = s->days(monday, thursday);
        QVERIFY(days.ok());
        QCOMPARE(days.value().size(), 4);
        QCOMPARE(days.value()[0].day, monday);
        QCOMPARE(days.value()[0].seconds, 600);
        QCOMPARE(days.value()[0].pages, 3);
        QCOMPARE(days.value()[1].seconds, 0);
        QVERIFY(!days.value()[1].any());
        QCOMPARE(days.value()[2].seconds, 60);  // mercredi (nuit de jeudi, avant 4 h)
        QCOMPARE(days.value()[3].seconds, 420);
        QCOMPARE(days.value()[3].pages, 3);

        const auto t = s->totals(monday, thursday);
        QVERIFY(t.ok());
        QCOMPARE(t.value().seconds, 1080);
        QCOMPARE(t.value().pages, 6);  // A : 1, 2, 3, 4 ; B : 1, 7 (la page 3 relue jeudi ne compte qu'une fois)
        QCOMPARE(t.value().documents, 2);
        QCOMPARE(t.value().activeDays, 3);

        QVERIFY(s->setGoals({200, 30, 50}).ok());
        const auto w = s->week();
        QVERIFY(w.ok());
        QCOMPARE(w.value().monday, monday);
        QCOMPARE(w.value().today, thursday);
        QCOMPARE(w.value().pages, 6);
        QCOMPARE(w.value().seconds, 1080);
        QCOMPARE(w.value().previousSeconds, 450);
        QCOMPARE(w.value().previousPages, 1);
        QCOMPARE(w.value().todaySeconds, 420);
        QCOMPARE(w.value().goals.pagesPerWeek, 200);
        QCOMPARE(w.value().goals.minutesPerDay, 30);

        // Un document retiré de la bibliothèque sort des chiffres
        QVERIFY(DocumentRepository(*m_db).remove(m_docB).ok());
        QCOMPARE(s->totals(monday, thursday).value().pages, 4);

        ReadingSessionRow orphan;
        QVERIFY(!s->saveSession(orphan).ok());
    }

    void goalsRoundTrip()
    {
        auto s = service();
        QVERIFY(!s->goals().value().any());
        QVERIFY(s->setGoals({200, 30, 50}).ok());
        auto g = s->goals().value();
        QCOMPARE(g.pagesPerWeek, 200);
        QCOMPARE(g.minutesPerDay, 30);
        QCOMPARE(g.reviewsPerDay, 50);
        QVERIFY(s->setGoals({0, 45, 0}).ok());
        g = s->goals().value();
        QCOMPARE(g.pagesPerWeek, 0);
        QCOMPARE(g.minutesPerDay, 45);
        QCOMPARE(g.reviewsPerDay, 0);
        QCOMPARE(count(QStringLiteral("SELECT COUNT(*) FROM goals")), 1);
        QVERIFY(!s->setGoals({-1, 0, 0}).ok());
    }

    void streak()
    {
        auto s = service();
        ActivityRepository repo(*m_db);
        for (int day : {17, 18, 19})  // mardi, mercredi, jeudi
            QVERIFY(repo.log(QStringLiteral("annotated"), QStringLiteral("annotation"), day,
                             {{QStringLiteral("document"), m_docA}}, iso(local(QDate(2026, 11, day), 15)))
                        .ok());
        QVERIFY(repo.log(QStringLiteral("annotated"), QStringLiteral("annotation"), 1, {}, iso(local(QDate(2026, 11, 15), 15))).ok());
        m_now = local(QDate(2026, 11, 19), 20);
        QCOMPARE(s->streak().value(), 3);
        m_now = local(QDate(2026, 11, 20), 9);  // vendredi, rien encore : la série tient
        QCOMPARE(s->streak().value(), 3);
        m_now = local(QDate(2026, 11, 21), 9);  // samedi : vendredi manqué
        QCOMPARE(s->streak().value(), 0);
    }

    void journalGroups()
    {
        auto s = service();
        ActivityRepository repo(*m_db);
        const QDate thursday(2026, 11, 19);
        const QDate wednesday(2026, 11, 18);
        auto log = [&](const QString& action, const QString& type, qint64 id, const QJsonObject& detail, int h, int m = 0,
                       const QDate& day = QDate(2026, 11, 19)) {
            QVERIFY(repo.log(action, type, id, detail, iso(local(day, h, m))).ok());
        };
        const QJsonObject onA{{QStringLiteral("document"), m_docA}};
        // Deux séances de A jeudi, une mercredi
        session(*s, m_docA, local(thursday, 8), 600, {1, 2});
        session(*s, m_docA, local(thursday, 14), 300, {2, 3});
        session(*s, m_docA, local(wednesday, 21), 120, {9});
        log(QStringLiteral("opened"), QStringLiteral("document"), m_docA, {}, 8);   // couvert par la lecture
        log(QStringLiteral("opened"), QStringLiteral("document"), m_docB, {}, 11);  // ouvert sans lire
        log(QStringLiteral("annotated"), QStringLiteral("annotation"), 101, onA, 9);
        log(QStringLiteral("annotated"), QStringLiteral("annotation"), 102, onA, 9, 30);
        log(QStringLiteral("annotated"), QStringLiteral("annotation"), 103, onA, 15);
        log(QStringLiteral("commented"), QStringLiteral("comment"), 201, onA, 16);
        for (int rating : {1, 3, 3, 4})
            log(QStringLiteral("reviewed"), QStringLiteral("card"), 300 + rating, {{QStringLiteral("rating"), rating}}, 18);
        log(QStringLiteral("imported"), QStringLiteral("document"), m_docA, {}, 7);
        log(QStringLiteral("imported"), QStringLiteral("document"), m_docB, {}, 7);
        log(QStringLiteral("note_created"), QStringLiteral("note"), 9999, {}, 12);  // note inexistante : ignorée
        m_now = local(thursday, 20);

        const auto journal = s->journal(wednesday, thursday);
        QVERIFY(journal.ok());
        using K = JournalEntry::Kind;
        QList<K> kinds;
        for (const JournalEntry& e : journal.value())
            kinds << e.kind;
        // Du plus récent au plus ancien
        QCOMPARE(kinds, (QList<K>{K::Reviews, K::Comments, K::Annotations, K::Reading, K::Opened, K::Imported, K::Reading}));
        const JournalEntry& reviews = journal.value()[0];
        QCOMPARE(reviews.count, 4);
        QCOMPARE(reviews.passed, 3);
        const JournalEntry& annotations = journal.value()[2];
        QCOMPARE(annotations.count, 3);
        QCOMPARE(annotations.itemId, 103);
        QCOMPARE(annotations.documentId, m_docA);
        QCOMPARE(annotations.title, QStringLiteral("Thermodynamique"));
        const JournalEntry& reading = journal.value()[3];
        QCOMPARE(reading.day, thursday);
        QCOMPARE(reading.seconds, 900);
        QCOMPARE(reading.pages, 3);
        QCOMPARE(reading.count, 2);
        QCOMPARE(journal.value()[4].documentId, m_docB);
        QCOMPARE(journal.value()[5].count, 2);
        QCOMPARE(journal.value()[5].documentId, 0);
        QCOMPARE(journal.value()[6].day, wednesday);

        const auto days = s->days(thursday, thursday);
        QCOMPARE(days.value().first().annotations, 3);
        QCOMPARE(days.value().first().comments, 1);
        QCOMPARE(days.value().first().reviews, 4);
        QCOMPARE(days.value().first().imported, 2);
        QCOMPARE(s->totals(thursday, thursday).value().reviewsPassed, 3);
    }

    void servicesWriteTheJournal()
    {
        QTemporaryDir dir;
        AnnotationService annotations(*m_db);
        AnnotationRecord a;
        a.documentId = m_docA;
        a.type = AnnotationType::Highlight;
        a.locator = QJsonObject{{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("page"), 3}};
        a.unitIndex = 3;
        a.selectedText = QStringLiteral("entropie");
        const auto created = annotations.create(a);
        QVERIFY2(created.ok(), qPrintable(created.ok() ? QString() : created.error().message));
        AnnotationRecord bookmark = a;
        bookmark.type = AnnotationType::Bookmark;
        bookmark.selectedText.clear();
        QVERIFY(annotations.create(bookmark).ok());  // un signet n'est pas une annotation du journal

        CommentService comments(*m_db);
        CommentRecord c;
        c.documentId = m_docA;
        c.body = QStringLiteral("Pourquoi ?");
        const auto comment = comments.create(c);
        QVERIFY(comment.ok());

        NoteService notes(*m_db, dir.path());
        const auto note = notes.create(QStringLiteral("Second principe"), NoteType::Concept, QString());
        QVERIFY(note.ok());

        auto rows = ActivityRepository(*m_db).entries(QStringLiteral("2000-01-01"), QStringLiteral("2999-01-01"));
        QVERIFY(rows.ok());
        QCOMPARE(rows.value().size(), 3);
        QCOMPARE(rows.value()[2].action, QStringLiteral("annotated"));
        QCOMPARE(rows.value()[2].entityId, created.value().id);
        QCOMPARE(rows.value()[2].detail.value(QStringLiteral("document")).toInteger(), m_docA);
        QCOMPARE(rows.value()[1].action, QStringLiteral("commented"));
        QCOMPARE(rows.value()[0].action, QStringLiteral("note_created"));
        QCOMPARE(rows.value()[0].entityId, note.value().id);

        ActivityService s(*m_db);  // horloge réelle : la note apparaît dans le journal du jour
        const auto journal = s.journal(s.today(), s.today());
        QVERIFY(journal.ok());
        bool found = false;
        for (const JournalEntry& e : journal.value())
            if (e.kind == JournalEntry::Kind::Note && e.title == QStringLiteral("Second principe"))
                found = true;
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(TstActivity)
#include "tst_activity.moc"
