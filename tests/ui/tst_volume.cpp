#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"
#include "services/activity/ActivityService.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/knowledge/KnowledgeService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "services/study/StudyService.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace studylab;

/// Campagne « gros volumes » (V2-7) : une bibliothèque d'étudiant chargée sur plusieurs années, écrite directement en
/// base, puis les opérations courantes chronométrées avec un plafond par opération.
///
/// Volume de base (STUDYLAB_VOLUME_SCALE=1, utilisé en CI) : 3 000 documents, 60 000 pages de texte, 30 000
/// annotations, 6 000 commentaires, 3 000 notes reliées, 20 000 cartes et 60 000 révisions, 3 ans d'activité.
/// STUDYLAB_VOLUME_SCALE=5 multiplie tout par 5 (mesures de docs/technical/performances.md). Les plafonds, larges
/// pour une compilation de débogage sur une machine de CI, sont multipliés par le facteur.
class TstVolume : public QObject {
    Q_OBJECT

    int m_scale = 1;
    QTemporaryDir m_dir;
    std::unique_ptr<AppPaths> m_paths;
    std::unique_ptr<db::Database> m_db;
    QList<QPair<QString, qint64>> m_timings;

    int docs() const { return 3000 * m_scale; }
    int notes() const { return 3000 * m_scale; }
    int cards() const { return 20000 * m_scale; }

    /// Chronomètre `f`, note la mesure et vérifie le plafond (ms, multiplié par le facteur de volume).
    bool timed(const QString& label, qint64 budgetMs, const std::function<void()>& f)
    {
        QElapsedTimer t;
        t.start();
        f();
        const qint64 ms = t.elapsed();
        m_timings << qMakePair(label, ms);
        qInfo().noquote() << QStringLiteral("%1 : %2 ms (plafond %3 ms)").arg(label, -48).arg(ms, 6).arg(budgetMs * m_scale);
        if (ms > budgetMs * m_scale) {
            qWarning().noquote() << QStringLiteral("Trop lent : %1").arg(label);
            return false;
        }
        return true;
    }

    static QString iso(const QDateTime& dt) { return dt.toUTC().toString(Qt::ISODate); }

    void fill()
    {
        static const QStringList words = {
            QStringLiteral("entropie"), QStringLiteral("énergie"), QStringLiteral("système"), QStringLiteral("cycle"),
            QStringLiteral("Carnot"), QStringLiteral("pression"), QStringLiteral("température"), QStringLiteral("travail"),
            QStringLiteral("chaleur"), QStringLiteral("réversible"), QStringLiteral("intégrale"), QStringLiteral("dérivée"),
            QStringLiteral("matrice"), QStringLiteral("vecteur"), QStringLiteral("fonction"), QStringLiteral("limite"),
            QStringLiteral("théorème"), QStringLiteral("démonstration"), QStringLiteral("hypothèse"), QStringLiteral("cellule"),
            QStringLiteral("protéine"), QStringLiteral("enzyme"), QStringLiteral("molécule"), QStringLiteral("réaction"),
            QStringLiteral("histoire"), QStringLiteral("révolution"), QStringLiteral("économie"), QStringLiteral("marché"),
            QStringLiteral("droit"), QStringLiteral("contrat"), QStringLiteral("langage"), QStringLiteral("algorithme")};
        QRandomGenerator rng(20261120);
        const auto sentence = [&](int n) {
            QStringList out;
            for (int i = 0; i < n; ++i)
                out << words.at(int(rng.bounded(words.size())));
            return out.join(u' ');
        };
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto past = [&](int maxDays) { return now.addSecs(-qint64(rng.bounded(maxDays * 86400))); };

        db::Transaction tx(*m_db);
        QVERIFY(tx.isActive());
        const auto prep = [this](const char* sql) { return std::move(m_db->prepare(QString::fromUtf8(sql)).value()); };

        // Documents, auteurs, texte intégral
        auto doc = prep("INSERT INTO documents(id, uuid, title, sort_title, format, file_path, file_hash, file_size, page_count, "
                        "status, favorite, text_indexed, added_at, updated_at, last_opened_at, publisher, year) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, 1, ?12, ?12, ?13, ?14, ?15)");
        auto author = prep("INSERT OR IGNORE INTO authors(name, sort_name) VALUES (?1, ?1)");
        auto docAuthor = prep("INSERT INTO document_authors(document_id, author_id, position) "
                              "SELECT ?1, id, 0 FROM authors WHERE name = ?2");
        auto fts = prep("INSERT INTO fts_documents(rowid, title, authors, publisher) VALUES (?1, ?2, ?3, ?4)");
        auto page = prep("INSERT INTO page_text(document_id, unit_index, source, content) VALUES (?1, ?2, 'native', ?3)");
        auto progress = prep("INSERT INTO reading_progress(document_id, locator_json, progress, updated_at) VALUES (?1, '{}', ?2, ?3)");
        static const char* const statuses[] = {"to_read", "reading", "finished", "paused", "to_reread"};
        static const char* const formats[] = {"pdf", "pdf", "pdf", "epub", "epub", "md", "docx"};
        for (int d = 1; d <= docs(); ++d) {
            const QString title = QStringLiteral("Cours %1 : %2").arg(d).arg(sentence(3));
            const QString who = QStringLiteral("Auteur %1").arg(d % 400);
            const bool opened = d % 3 == 0;
            doc.reset();
            QVERIFY(doc.bind(1, qint64(d)).bind(2, QStringLiteral("doc-%1").arg(d)).bind(3, title).bind(4, title.toLower())
                        .bind(5, QString::fromLatin1(formats[d % 7])).bind(6, QStringLiteral("x/%1.pdf").arg(d))
                        .bind(7, QStringLiteral("%1").arg(d, 64, 10, QLatin1Char('0'))).bind(8, qint64(1 << 20))
                        .bind(9, 20).bind(10, QString::fromLatin1(statuses[d % 5])).bind(11, d % 17 == 0 ? 1 : 0)
                        .bind(12, iso(past(1000))).bind(13, opened ? iso(past(400)) : QString())
                        .bind(14, QStringLiteral("Éditions %1").arg(d % 30)).bind(15, 1990 + d % 35)
                        .run().ok());
            author.reset();
            QVERIFY(author.bind(1, who).run().ok());
            docAuthor.reset();
            QVERIFY(docAuthor.bind(1, qint64(d)).bind(2, who).run().ok());
            fts.reset();
            QVERIFY(fts.bind(1, qint64(d)).bind(2, title).bind(3, who).bind(4, QStringLiteral("Éditions %1").arg(d % 30)).run().ok());
            for (int u = 0; u < 20; ++u) {
                page.reset();
                QVERIFY(page.bind(1, qint64(d)).bind(2, u).bind(3, sentence(120)).run().ok());
            }
            if (opened) {
                progress.reset();
                QVERIFY(progress.bind(1, qint64(d)).bind(2, rng.bounded(1.0)).bind(3, iso(past(400))).run().ok());
            }
        }

        // Annotations (10 par document) et commentaires (2 par document)
        auto ann = prep("INSERT INTO annotations(uuid, document_id, type, color, unit_index, locator_json, sort_key, "
                        "selected_text, doc_hash, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 'h', ?9, ?9)");
        auto com = prep("INSERT INTO comments(uuid, document_id, annotation_id, anchor_kind, unit_index, kind, body_md, "
                        "created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)");
        static const char* const types[] = {"highlight", "highlight", "underline", "note", "marker", "bookmark"};
        static const char* const colors[] = {"yellow", "green", "blue", "pink", "purple"};
        static const char* const kinds[] = {"reflection", "question", "explanation", "reference"};
        qint64 annId = 0;
        for (int d = 1; d <= docs(); ++d) {
            for (int k = 0; k < 10; ++k) {
                ++annId;
                const int unit = int(rng.bounded(20));
                const QJsonObject loc{{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("page"), unit},
                                      {QStringLiteral("rects"), QJsonArray{QJsonArray{0.1, 0.2, 0.5, 0.03}}}};
                ann.reset();
                QVERIFY(ann.bind(1, QStringLiteral("a-%1").arg(annId)).bind(2, qint64(d))
                            .bind(3, QString::fromLatin1(types[k % 6])).bind(4, QString::fromLatin1(colors[k % 5])).bind(5, unit)
                            .bind(6, QString::fromUtf8(QJsonDocument(loc).toJson(QJsonDocument::Compact)))
                            .bind(7, unit + rng.bounded(1.0)).bind(8, sentence(8)).bind(9, iso(past(900)))
                            .run().ok());
            }
            for (int k = 0; k < 2; ++k) {
                com.reset();
                if (k == 0)
                    com.bind(3, annId);
                else
                    com.bindNull(3);
                QVERIFY(com.bind(1, QStringLiteral("c-%1-%2").arg(d).arg(k)).bind(2, qint64(d)).bind(4, k == 0 ? QStringLiteral("annotation") : QStringLiteral("free"))
                            .bind(5, k == 0 ? 3 : -1).bind(6, QString::fromLatin1(kinds[(d + k) % 4]))
                            .bind(7, QStringLiteral("**%1** : %2 $E = mc^2$").arg(sentence(2), sentence(30))).bind(8, iso(past(900)))
                            .run().ok());
            }
        }

        // Notes reliées en [[liens]] (3 par note), dans 40 dossiers
        auto note = prep("INSERT INTO notes(id, uuid, title, slug, note_type, body_md, folder, created_at, updated_at) "
                         "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)");
        auto link = prep("INSERT INTO links(source_type, source_id, target_type, target_id, target_text, kind, origin, created_at) "
                         "VALUES ('note', ?1, 'note', ?2, ?3, 'reference', 'wikilink', ?4)");
        static const char* const noteTypes[] = {"course", "concept", "reading", "formula", "personal", "revision"};
        for (int n = 1; n <= notes(); ++n) {
            QStringList targets;
            QString body = QStringLiteral("# Note %1\n\n%2\n\n").arg(n).arg(sentence(150));
            for (int k = 0; k < 3; ++k) {
                const int t = 1 + int(rng.bounded(notes()));
                targets << QStringLiteral("Note %1").arg(t);
                body += QStringLiteral("Voir [[Note %1]]. ").arg(t);
                link.reset();
                QVERIFY(link.bind(1, qint64(n)).bind(2, qint64(t)).bind(3, QStringLiteral("Note %1").arg(t)).bind(4, iso(past(900))).run().ok());
            }
            note.reset();
            QVERIFY(note.bind(1, qint64(n)).bind(2, QStringLiteral("n-%1").arg(n)).bind(3, QStringLiteral("Note %1").arg(n))
                        .bind(4, QStringLiteral("note-%1").arg(n)).bind(5, QString::fromLatin1(noteTypes[n % 6])).bind(6, body)
                        .bind(7, QStringLiteral("Matière %1/Cours").arg(n % 40)).bind(8, iso(past(900)))
                        .run().ok());
        }

        // Matières, collections
        QVERIFY(m_db->exec(QStringLiteral(
                               "WITH RECURSIVE s(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM s WHERE i < 40) "
                               "INSERT INTO subjects(id, uuid, name, position, created_at) "
                               "SELECT i, 'sub-' || i, 'Matière ' || i, i, '2025-01-01T00:00:00Z' FROM s")).ok());
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO subject_items(subject_id, entity_type, entity_id) "
                                          "SELECT 1 + id % 40, 'document', id FROM documents")).ok());
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO subject_items(subject_id, entity_type, entity_id) "
                                          "SELECT 1 + id % 40, 'note', id FROM notes")).ok());

        // Paquets, cartes, état et journal de révisions (3 par carte)
        QVERIFY(m_db->exec(QStringLiteral(
                               "WITH RECURSIVE s(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM s WHERE i < 40) "
                               "INSERT INTO decks(id, uuid, name, subject_id, scheduler, created_at) "
                               "SELECT i, 'deck-' || i, 'Paquet ' || i, i, CASE WHEN i % 2 THEN 'fsrs' ELSE 'sm2' END, "
                               "'2025-01-01T00:00:00Z' FROM s")).ok());
        auto card = prep("INSERT INTO cards(id, uuid, deck_id, card_type, front_md, back_md, created_at, updated_at) "
                         "VALUES (?1, ?2, ?3, 'basic', ?4, ?5, ?6, ?6)");
        auto state = prep("INSERT INTO card_state(card_id, state, due_at, interval_d, reps, stability, difficulty) "
                          "VALUES (?1, ?2, ?3, ?4, ?5, 5.0, 5.0)");
        auto review = prep("INSERT INTO card_reviews(card_id, reviewed_at, rating, elapsed_ms) VALUES (?1, ?2, ?3, 4000)");
        auto act = prep("INSERT INTO activity(at, action, entity_type, entity_id, detail_json) VALUES (?1, ?2, ?3, ?4, ?5)");
        for (int c = 1; c <= cards(); ++c) {
            card.reset();
            QVERIFY(card.bind(1, qint64(c)).bind(2, QStringLiteral("card-%1").arg(c)).bind(3, qint64(1 + c % 40))
                        .bind(4, sentence(6) + u'?').bind(5, sentence(12)).bind(6, iso(past(900))).run().ok());
            const bool isNew = c % 5 == 0;
            state.reset();
            QVERIFY(state.bind(1, qint64(c)).bind(2, isNew ? QStringLiteral("new") : QStringLiteral("review"))
                        .bind(3, isNew ? QString() : iso(now.addSecs(qint64(rng.bounded(30 * 86400)) - 5 * 86400)))
                        .bind(4, 1.0 + rng.bounded(60)).bind(5, isNew ? 0 : 3).run().ok());
            if (isNew)
                continue;
            for (int r = 0; r < 3; ++r) {
                const QString at = iso(past(900));
                const int rating = 1 + int(rng.bounded(4));
                review.reset();
                QVERIFY(review.bind(1, qint64(c)).bind(2, at).bind(3, rating).run().ok());
                act.reset();
                QVERIFY(act.bind(1, at).bind(2, QStringLiteral("reviewed")).bind(3, QStringLiteral("card")).bind(4, qint64(c))
                            .bind(5, QStringLiteral("{\"rating\":%1}").arg(rating)).run().ok());
            }
        }

        // Activité de lecture : séances (3 ans) et journal
        auto session = prep("INSERT INTO reading_sessions(document_id, started_at, ended_at, seconds, pages_seen) "
                            "VALUES (?1, ?2, ?2, ?3, ?4)");
        for (int s = 0; s < 10000 * m_scale; ++s) {
            const qint64 d = 1 + qint64(rng.bounded(docs()));
            const QString at = iso(past(1100));
            QJsonArray seen;
            for (int p = 0; p < 6; ++p)
                seen.append(int(rng.bounded(20)));
            session.reset();
            QVERIFY(session.bind(1, d).bind(2, at).bind(3, 300 + int(rng.bounded(3000)))
                        .bind(4, QString::fromUtf8(QJsonDocument(seen).toJson(QJsonDocument::Compact))).run().ok());
            act.reset();
            QVERIFY(act.bind(1, at).bind(2, QStringLiteral("opened")).bind(3, QStringLiteral("document")).bind(4, d)
                        .bind(5, QString()).run().ok());
        }
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO activity(at, action, entity_type, entity_id) "
                                          "SELECT created_at, 'annotated', 'annotation', id FROM annotations")).ok());
        QVERIFY(m_db->exec(QStringLiteral("INSERT INTO activity(at, action, entity_type, entity_id) "
                                          "SELECT created_at, 'note_created', 'note', id FROM notes")).ok());
        QVERIFY(tx.commit().ok());
        QVERIFY(m_db->exec(QStringLiteral("ANALYZE")).ok());
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        m_scale = std::max(1, qEnvironmentVariableIntValue("STUDYLAB_VOLUME_SCALE"));
        m_paths = std::make_unique<AppPaths>(m_dir.path());
        auto startup = openAndMigrate(*m_paths);
        QVERIFY(startup.ok());
        m_db = std::move(startup.value().db);
        QElapsedTimer t;
        t.start();
        fill();
        qInfo().noquote() << QStringLiteral("Base remplie en %1 s : %2 Mo").arg(t.elapsed() / 1000.0, 0, 'f', 1)
                                 .arg(QFileInfo(m_paths->databaseFile()).size() / 1048576.0, 0, 'f', 1);
    }

    void startup()
    {
        m_db.reset();
        QVERIFY(timed(QStringLiteral("Ouverture de la base et migrations"), 1500, [this] {
            auto startup = openAndMigrate(*m_paths);
            QVERIFY(startup.ok());
            m_db = std::move(startup.value().db);
        }));
    }

    void services()
    {
        LibraryService library(*m_db, *m_paths);
        bool ok = true;
        ok &= timed(QStringLiteral("Bibliothèque : tous les documents"), 800, [&] {
            QCOMPARE(library.documents().value().size(), docs());
        });
        ok &= timed(QStringLiteral("Bibliothèque : tri par auteur"), 800, [&] {
            db::DocumentQuery q;
            q.sort = db::DocumentQuery::Sort::Author;
            QCOMPARE(library.documents(q).value().size(), docs());
        });
        ok &= timed(QStringLiteral("Bibliothèque : filtre texte"), 300, [&] {
            db::DocumentQuery q;
            q.text = QStringLiteral("entropie");
            QVERIFY(!library.documents(q).value().isEmpty());
        });
        SearchService search(*m_db);
        ok &= timed(QStringLiteral("Recherche partout : un mot"), 600, [&] {
            QVERIFY(!search.search(QStringLiteral("carnot")).value().passages.isEmpty());
        });
        ok &= timed(QStringLiteral("Recherche partout : trois mots"), 600, [&] {
            QVERIFY(search.search(QStringLiteral("cycle reversible chaleur")).ok());
        });
        ok &= timed(QStringLiteral("Recherche partout : préfixe"), 600, [&] {
            QVERIFY(search.search(QStringLiteral("thermo")).ok());
        });
        AnnotationService annotations(*m_db);
        ok &= timed(QStringLiteral("Annotations d'un document"), 50, [&] {
            QCOMPARE(annotations.list(docs() / 2).value().size(), 10);
        });
        NoteService notesService(*m_db, m_paths->attachmentsDir());
        ok &= timed(QStringLiteral("Mes notes : liste"), 600, [&] {
            QCOMPARE(notesService.list().value().size(), notes());
        });
        ok &= timed(QStringLiteral("Mes notes : rétroliens"), 100, [&] { QVERIFY(notesService.backlinks(10).ok()); });
        KnowledgeService knowledge(*m_db);
        ok &= timed(QStringLiteral("Graphe : données"), 1500, [&] { QVERIFY(knowledge.graph().ok()); });
        StudyService study(*m_db);
        ok &= timed(QStringLiteral("Révisions : cartes dues (total)"), 300, [&] { QVERIFY(study.dueTotal() > 0); });
        ok &= timed(QStringLiteral("Révisions : paquets"), 600, [&] { QCOMPARE(study.decks().value().size(), 40); });
        ok &= timed(QStringLiteral("Révisions : statistiques"), 400, [&] { QVERIFY(study.stats().ok()); });
        ActivityService activity(*m_db);
        const QDate today = QDate::currentDate();
        ok &= timed(QStringLiteral("Activité : 12 mois"), 800, [&] { QVERIFY(activity.days(today.addYears(-1), today).ok()); });
        ok &= timed(QStringLiteral("Activité : série en cours"), 400, [&] { QVERIFY(activity.streak().ok()); });
        ok &= timed(QStringLiteral("Activité : journal de 30 jours"), 600, [&] {
            QVERIFY(activity.journal(today.addDays(-30), today).ok());
        });
        ok &= timed(QStringLiteral("Accueil : objectif de la semaine"), 300, [&] { QVERIFY(activity.week().ok()); });
        QVERIFY2(ok, "au moins une opération dépasse son plafond (voir les mesures ci-dessus)");
    }

    void screens()
    {
        SettingsService settings(*m_db);
        settings.setThemeMode(ThemeMode::Light);
        ui::ThemeManager theme(settings);
        theme.apply();
        LibraryService library(*m_db, *m_paths);
        AnnotationService annotations(*m_db);
        CommentService comments(*m_db);
        SearchService search(*m_db);
        BackupService backups(*m_paths, *m_db);
        NoteService notesService(*m_db, m_paths->attachmentsDir());
        OrganizeService organize(*m_db);
        AppContext ctx{*m_paths, *m_db, settings, theme, library, annotations, comments, search, backups, notesService, organize};
        std::unique_ptr<ui::MainWindow> window;
        bool ok = true;
        ok &= timed(QStringLiteral("Fenêtre principale (accueil compris)"), 4000, [&] {
            window = std::make_unique<ui::MainWindow>(ctx);
            window->resize(1440, 900);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        });
        for (const char* page : {"library", "notes", "subjects", "collections", "graph", "revisions", "activity", "references",
                                 "home"}) {
            ok &= timed(QStringLiteral("Écran %1").arg(QString::fromLatin1(page)), 2500, [&] {
                window->navigateTo(QString::fromLatin1(page));
                QCoreApplication::processEvents();
            });
        }
        QVERIFY2(ok, "au moins un écran dépasse son plafond (voir les mesures ci-dessus)");
    }

    void cleanupTestCase()
    {
        // Base gardée pour l'analyse des requêtes (EXPLAIN QUERY PLAN) : STUDYLAB_VOLUME_KEEP=/chemin/base.db
        if (const QString keep = qEnvironmentVariable("STUDYLAB_VOLUME_KEEP"); !keep.isEmpty()) {
            m_db.reset();
            QFile::remove(keep);
            QFile::copy(m_paths->databaseFile(), keep);
        }
        qInfo().noquote() << QStringLiteral("Volume x%1 — récapitulatif :").arg(m_scale);
        for (const auto& [label, ms] : std::as_const(m_timings))
            qInfo().noquote() << QStringLiteral("| %1 | %2 ms |").arg(label).arg(ms);
    }
};

QTEST_MAIN(TstVolume)
#include "tst_volume.moc"
