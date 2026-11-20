#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

class TestMigrator : public QObject {
    Q_OBJECT
private slots:
    void bundledMigrationsLoad()
    {
        auto list = Migrator::loadFrom();
        QVERIFY2(list.ok(), qPrintable(list ? QString() : list.error().message));
        QVERIFY(!list.value().isEmpty());
        QCOMPARE(list.value().first().version, 1);
        QCOMPARE(list.value().first().name, QStringLiteral("initial"));
    }

    void freshDatabaseReachesLatest()
    {
        auto db = Database::openInMemory();
        Migrator m(Migrator::loadFrom().value());
        bool hookCalled = false;
        auto v = m.migrate(*db.value(), [&](int, int) {
            hookCalled = true;
            return Result<void>::success();
        });
        QVERIFY2(v.ok(), qPrintable(v ? QString() : v.error().message));
        QCOMPARE(v.value(), m.latestVersion());
        QVERIFY(!hookCalled);  // base vide : pas de sauvegarde préalable

        for (const char* table : {"documents", "annotations", "comments", "notes", "links", "cards", "settings",
                                  "collections", "subjects", "reading_progress"}) {
            const auto n = db.value()->queryInt(
                QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='%1'").arg(QLatin1String(table)));
            QVERIFY2(n.value() == 1, table);
        }
        QVERIFY(db.value()->checkIntegrity(true).value().isEmpty());
        QCOMPARE(db.value()->queryInt(QStringLiteral("SELECT count(*) FROM pragma_foreign_key_check")).value(), 0);
    }

    void secondRunIsNoop()
    {
        auto db = Database::openInMemory();
        Migrator m(Migrator::loadFrom().value());
        QVERIFY(m.migrate(*db.value()).ok());
        QVERIFY(m.migrate(*db.value()).ok());
        QCOMPARE(db.value()->queryInt(QStringLiteral("SELECT count(*) FROM schema_migrations")).value(), m.latestVersion());
    }

    void fullTextSearchIgnoresAccents()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(db).ok());
        QVERIFY(db.exec(QStringLiteral(
                            "INSERT INTO notes(uuid, title, slug, body_md, created_at, updated_at) "
                            "VALUES ('u1', 'Second principe', 'second-principe', 'L''énergie se conserve, l''entropie croît.', 'x', 'x')"))
                    .ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT rowid FROM fts_notes WHERE fts_notes MATCH 'energie'")).value(), 1);
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'entrop*'")).value(), 1);

        // Mise à jour et suppression maintiennent l'index
        QVERIFY(db.exec(QStringLiteral("UPDATE notes SET body_md = 'Rendement de Carnot' WHERE id = 1")).ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'energie'")).value(), 0);
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'carnot'")).value(), 1);
        QVERIFY(db.exec(QStringLiteral("DELETE FROM notes WHERE id = 1")).ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'carnot'")).value(), 0);
    }

    void ftsDocumentsSupportsDeleteAfterMigration2()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(db).ok());
        QVERIFY(Migrator::currentVersion(db).value() >= 2);
        QVERIFY(db.exec(QStringLiteral("INSERT INTO fts_documents(rowid, title, authors) VALUES (7, 'Thermodynamique', 'Çengel')")).ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT rowid FROM fts_documents WHERE fts_documents MATCH 'cengel'")).value(), 7);
        QVERIFY(db.exec(QStringLiteral("DELETE FROM fts_documents WHERE rowid = 7")).ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM fts_documents WHERE fts_documents MATCH 'thermo*'")).value(), 0);
    }

    void cardsStudyColumnsAfterMigration3()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(db).ok());
        QVERIFY(Migrator::currentVersion(db).value() >= 3);
        QVERIFY(db.exec(QStringLiteral("INSERT INTO decks(uuid, name, created_at) VALUES ('d', 'Thermo', 'x')")).ok());
        QVERIFY(db.exec(QStringLiteral("INSERT INTO cards(uuid, deck_id, card_type, front_md, back_md, ordinal, created_at, "
                                       "updated_at) VALUES ('c', 1, 'cloze', '{{c2::Carnot}}', '', 2, 'x', 'x')")).ok());
        QVERIFY(db.exec(QStringLiteral("INSERT INTO card_state(card_id) VALUES (1)")).ok());
        QCOMPARE(db.queryInt(QStringLiteral("SELECT ordinal FROM cards WHERE id = 1")).value(), 2);
        QCOMPARE(db.queryInt(QStringLiteral("SELECT step FROM card_state WHERE card_id = 1")).value(), 0);
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = 'ix_card_reviews_at'")).value(), 1);
    }

    void duplicateDocumentHashRejected()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(db).ok());
        const QString ins = QStringLiteral(
            "INSERT INTO documents(uuid, title, sort_title, format, file_path, file_hash, file_size, added_at, updated_at) "
            "VALUES ('%1', 'T', 't', 'pdf', 'a.pdf', 'abc', 10, 'x', 'x')");
        QVERIFY(db.exec(ins.arg(QStringLiteral("u1"))).ok());
        QVERIFY(!db.exec(ins.arg(QStringLiteral("u2"))).ok());
        QVERIFY(!db.exec(QStringLiteral("INSERT INTO documents(uuid, title, sort_title, format, file_path, file_hash, file_size, "
                                        "added_at, updated_at) VALUES ('u3','T','t','exe','a','h2',1,'x','x')"))
                     .ok());  // format inconnu refusé
    }

    void failedMigrationRollsBack()
    {
        QTemporaryDir dir;
        auto opened = Database::open(dir.filePath(QStringLiteral("m.db")));
        Database& db = *opened.value();
        QList<Migration> list = {
            {1, QStringLiteral("ok"), QStringLiteral("CREATE TABLE a(x);")},
            {2, QStringLiteral("broken"), QStringLiteral("CREATE TABLE b(x); THIS IS NOT SQL;")},
        };
        auto v = Migrator(list).migrate(db);
        QVERIFY(!v.ok());
        QVERIFY(v.error().message.contains(QStringLiteral("Migration 2")));
        QCOMPARE(Migrator::currentVersion(db).value(), 1);
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name='b'")).value(), 0);
    }

    void hookRunsBeforeUpgradingExistingData()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QList<Migration> v1 = {{1, QStringLiteral("a"), QStringLiteral("CREATE TABLE a(x);")}};
        QVERIFY(Migrator(v1).migrate(db).ok());

        QList<Migration> v2 = v1;
        v2.append({2, QStringLiteral("b"), QStringLiteral("CREATE TABLE b(x);")});
        int from = -1, to = -1;
        auto r = Migrator(v2).migrate(db, [&](int f, int t) {
            from = f;
            to = t;
            return Result<void>::success();
        });
        QVERIFY(r.ok());
        QCOMPARE(from, 1);
        QCOMPARE(to, 2);
    }

    void hookFailureAbortsMigration()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QList<Migration> v1 = {{1, QStringLiteral("a"), QStringLiteral("CREATE TABLE a(x);")}};
        QVERIFY(Migrator(v1).migrate(db).ok());
        QList<Migration> v2 = v1;
        v2.append({2, QStringLiteral("b"), QStringLiteral("CREATE TABLE b(x);")});
        auto r = Migrator(v2).migrate(db, [](int, int) -> Result<void> {
            return Error::make(Error::Code::Io, QStringLiteral("disque plein"));
        });
        QVERIFY(!r.ok());
        QCOMPARE(Migrator::currentVersion(db).value(), 1);
    }

    void newerSchemaIsRefused()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QList<Migration> v3 = {{1, QStringLiteral("a"), QStringLiteral("SELECT 1;")},
                               {3, QStringLiteral("c"), QStringLiteral("SELECT 1;")}};
        QVERIFY(Migrator(v3).migrate(db).ok());
        auto r = Migrator({{1, QStringLiteral("a"), QStringLiteral("SELECT 1;")}}).migrate(db);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, Error::Code::SchemaTooNew);
    }

    void invalidFileNameRejected()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("init.sql")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("SELECT 1;");
        f.close();
        QVERIFY(!Migrator::loadFrom(dir.path()).ok());
    }
};

QTEST_GUILESS_MAIN(TestMigrator)
#include "tst_migrator.moc"
