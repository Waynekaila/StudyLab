#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

class TestDatabase : public QObject {
    Q_OBJECT
private slots:
    void opensFileInWalMode()
    {
        QTemporaryDir dir;
        auto db = Database::open(dir.filePath(QStringLiteral("t.db")));
        QVERIFY2(db.ok(), qPrintable(db ? QString() : db.error().message));
        QCOMPARE(db.value()->queryString(QStringLiteral("PRAGMA journal_mode")).value(), QStringLiteral("wal"));
        QCOMPARE(db.value()->queryInt(QStringLiteral("PRAGMA foreign_keys")).value(), 1);
    }

    void hasFts5AndJson()
    {
        auto db = Database::openInMemory();
        QVERIFY(db.ok());
        QVERIFY(db.value()->hasCompileOption(QStringLiteral("ENABLE_FTS5")));
        QVERIFY(db.value()->exec(QStringLiteral("CREATE VIRTUAL TABLE t USING fts5(x)")).ok());
        QCOMPARE(db.value()->queryInt(QStringLiteral("SELECT json_valid('{\"a\":1}')")).value(), 1);
        QVERIFY(Database::sqliteVersion().startsWith(QStringLiteral("3.")));
    }

    void bindAndReadRoundTrip()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n TEXT)")).ok());
        auto ins = db.prepare(QStringLiteral("INSERT INTO t VALUES (?1, ?2, ?3, ?4, ?5)"));
        QVERIFY(ins.ok());
        const QString unicode = QStringLiteral("Çengel · Thermodynamique — ΔS ≥ 0 « entropie » 🔥");
        ins.value().bind(1, qint64(1) << 40).bind(2, 2.5).bind(3, unicode).bind(4, QByteArray("\x00\x01\x02", 3)).bind(5, QString());
        QVERIFY(ins.value().run().ok());
        QCOMPARE(db.changes(), 1);
        QCOMPARE(db.lastInsertRowId(), 1);

        auto sel = db.prepare(QStringLiteral("SELECT i, r, s, b, n FROM t"));
        QVERIFY(sel.value().step().value());
        QCOMPARE(sel.value().columnInt64(0), qint64(1) << 40);
        QCOMPARE(sel.value().columnDouble(1), 2.5);
        QCOMPARE(sel.value().columnText(2), unicode);
        QCOMPARE(sel.value().columnBlob(3), QByteArray("\x00\x01\x02", 3));
        QVERIFY(sel.value().isNull(4));
        QVERIFY(!sel.value().step().value());
    }

    void namedParameters()
    {
        auto opened = Database::openInMemory();
        auto st = opened.value()->prepare(QStringLiteral("SELECT :a + :b"));
        auto& s = st.value();
        s.bind(s.parameterIndex(":a"), 2).bind(s.parameterIndex(":b"), 3);
        QVERIFY(s.step().value());
        QCOMPARE(s.columnInt64(0), 5);
    }

    void prepareErrorIsReported()
    {
        auto opened = Database::openInMemory();
        auto st = opened.value()->prepare(QStringLiteral("SELEC nonsense"));
        QVERIFY(!st.ok());
        QVERIFY(st.error().message.contains(QStringLiteral("syntax")));
    }

    void transactionRollsBackWithoutCommit()
    {
        auto opened = Database::openInMemory();
        Database& db = *opened.value();
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE t(x)")).ok());
        {
            Transaction tx(db);
            QVERIFY(tx.isActive());
            QVERIFY(db.exec(QStringLiteral("INSERT INTO t VALUES (1)")).ok());
        }
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM t")).value(), 0);
        {
            Transaction tx(db);
            QVERIFY(db.exec(QStringLiteral("INSERT INTO t VALUES (1)")).ok());
            QVERIFY(tx.commit().ok());
        }
        QCOMPARE(db.queryInt(QStringLiteral("SELECT count(*) FROM t")).value(), 1);
    }

    void backupAndIntegrity()
    {
        QTemporaryDir dir;
        auto opened = Database::open(dir.filePath(QStringLiteral("a.db")));
        Database& db = *opened.value();
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE t(x); INSERT INTO t VALUES ('hello');")).ok());
        QVERIFY(db.checkIntegrity(true).value().isEmpty());

        const QString target = dir.filePath(QStringLiteral("backup.db"));
        QVERIFY(db.backupTo(target).ok());
        QVERIFY(QFile::exists(target));
        QVERIFY(!db.backupTo(target).ok());  // ne jamais écraser une sauvegarde

        auto copy = Database::open(target, Database::Mode::ReadOnly);
        QVERIFY(copy.ok());
        QCOMPARE(copy.value()->queryString(QStringLiteral("SELECT x FROM t")).value(), QStringLiteral("hello"));
        QVERIFY(!copy.value()->exec(QStringLiteral("INSERT INTO t VALUES ('no')")).ok());
    }

    void openFailsOnUnwritablePath()
    {
        auto db = Database::open(QStringLiteral("/nonexistent-dir/x/y.db"));
        QVERIFY(!db.ok());
        QCOMPARE(db.error().code, Error::Code::Database);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
