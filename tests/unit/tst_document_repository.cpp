#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"

#include <QElapsedTimer>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

class TstDocumentRepository : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;

    DocumentRecord make(const QString& title, const QString& hash, QStringList authors = {}, QStringList tags = {})
    {
        DocumentRecord r;
        r.title = title;
        r.fileHash = hash;
        r.filePath = hash + QStringLiteral(".pdf");
        r.fileSize = 1234;
        r.pageCount = 10;
        r.authors = std::move(authors);
        r.tags = std::move(tags);
        return r;
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void insertAndFind()
    {
        DocumentRepository repo(*m_db);
        auto r = make(QStringLiteral("  La Thermodynamique  "), QStringLiteral("h1"),
                      {QStringLiteral("Yunus A. Çengel"), QStringLiteral("Michael A. Boles"), QStringLiteral("Yunus A. Çengel")},
                      {QStringLiteral("thermo"), QStringLiteral("Physique"), QStringLiteral("THERMO")});
        r.year = 2019;
        r.isbn = QStringLiteral("978-0-07-339817-4");
        QVERIFY(repo.insert(r).ok());
        QVERIFY(r.id > 0);
        QVERIFY(!r.uuid.isEmpty());

        auto found = repo.findById(r.id);
        QVERIFY(found.ok());
        const DocumentRecord& f = found.value();
        QCOMPARE(f.title, QStringLiteral("La Thermodynamique"));
        QCOMPARE(f.sortTitle, QStringLiteral("thermodynamique"));
        QCOMPARE(f.authors, (QStringList{QStringLiteral("Yunus A. Çengel"), QStringLiteral("Michael A. Boles")}));
        QCOMPARE(f.tags, (QStringList{QStringLiteral("Physique"), QStringLiteral("thermo")}));
        QCOMPARE(f.year, 2019);
        QCOMPARE(f.status, ReadingStatus::ToRead);
        QVERIFY(repo.findByHash(QStringLiteral("h1")).value().has_value());
        QVERIFY(!repo.findByHash(QStringLiteral("nope")).value().has_value());
        QCOMPARE(repo.count().value(), 1);
        QVERIFY(repo.activeHashes().value().contains(QStringLiteral("h1")));

        auto dup = make(QStringLiteral("Copie"), QStringLiteral("h1"));
        QVERIFY(!repo.insert(dup).ok());  // empreinte déjà présente
    }

    void searchAndFilters()
    {
        DocumentRepository repo(*m_db);
        auto a = make(QStringLiteral("Thermodynamique"), QStringLiteral("a"), {QStringLiteral("Çengel")}, {QStringLiteral("physique")});
        auto b = make(QStringLiteral("Écologie générale"), QStringLiteral("b"), {QStringLiteral("Dajoz")});
        auto c = make(QStringLiteral("Analyse"), QStringLiteral("c"), {QStringLiteral("Bourbaki")}, {QStringLiteral("maths")});
        c.format = DocumentFormat::Epub;
        QVERIFY(repo.insert(a).ok());
        QVERIFY(repo.insert(b).ok());
        QVERIFY(repo.insert(c).ok());
        QVERIFY(repo.setFavorite(b.id, true).ok());
        QVERIFY(repo.setStatus(c.id, ReadingStatus::Reading).ok());

        auto ids = [&](const DocumentQuery& q) {
            QList<qint64> out;
            const auto rows = repo.list(q);
            for (const auto& d : rows.value())
                out << d.id;
            return out;
        };
        DocumentQuery q;
        q.text = QStringLiteral("cengel");
        QCOMPARE(ids(q), QList<qint64>{a.id});
        q.text = QStringLiteral("ecol");
        QCOMPARE(ids(q), QList<qint64>{b.id});
        q.text = QStringLiteral("phys");  // tag
        QCOMPARE(ids(q), QList<qint64>{a.id});
        q.text = QStringLiteral("\"); DROP TABLE documents; --");
        QVERIFY(repo.list(q).ok());

        DocumentQuery fav;
        fav.favoritesOnly = true;
        QCOMPARE(ids(fav), QList<qint64>{b.id});
        DocumentQuery st;
        st.status = ReadingStatus::Reading;
        QCOMPARE(ids(st), QList<qint64>{c.id});
        DocumentQuery fmt;
        fmt.format = DocumentFormat::Epub;
        QCOMPARE(ids(fmt), QList<qint64>{c.id});
        DocumentQuery tag;
        tag.tag = QStringLiteral("MATHS");
        QCOMPARE(ids(tag), QList<qint64>{c.id});

        DocumentQuery byTitle;
        byTitle.sort = DocumentQuery::Sort::Title;
        QCOMPARE(ids(byTitle), (QList<qint64>{c.id, b.id, a.id}));
        DocumentQuery byAuthor;
        byAuthor.sort = DocumentQuery::Sort::Author;
        QCOMPARE(ids(byAuthor), (QList<qint64>{c.id, a.id, b.id}));
        QCOMPARE(repo.documentTags().value(), (QStringList{QStringLiteral("maths"), QStringLiteral("physique")}));
    }

    void updateKeepsIndexInSync()
    {
        DocumentRepository repo(*m_db);
        auto a = make(QStringLiteral("Mécanique"), QStringLiteral("a"), {QStringLiteral("Landau")}, {QStringLiteral("physique")});
        QVERIFY(repo.insert(a).ok());
        a.title = QStringLiteral("Mécanique quantique");
        a.authors = {QStringLiteral("Cohen-Tannoudji")};
        a.tags = {QStringLiteral("quantique")};
        a.favorite = true;
        QVERIFY(repo.update(a).ok());

        DocumentQuery q;
        q.text = QStringLiteral("landau");
        QVERIFY(repo.list(q).value().isEmpty());
        q.text = QStringLiteral("tannoudji");
        QCOMPARE(repo.list(q).value().size(), 1);
        QCOMPARE(repo.findById(a.id).value().tags, QStringList{QStringLiteral("quantique")});
        QVERIFY(repo.findById(a.id).value().favorite);
        QCOMPARE(repo.documentTags().value(), QStringList{QStringLiteral("quantique")});

        QVERIFY(repo.remove(a.id).ok());
        QCOMPARE(repo.count().value(), 0);
        QVERIFY(repo.list(q).value().isEmpty());
        QVERIFY(repo.documentTags().value().isEmpty());
        // Le fichier peut être importé à nouveau après retrait
        auto again = make(QStringLiteral("Mécanique"), QStringLiteral("a"));
        QVERIFY(repo.insert(again).ok());
        QCOMPARE(repo.remove(424242).error().code, Error::Code::NotFound);
    }

    void fiveThousandDocumentsFilterQuickly()
    {
        DocumentRepository repo(*m_db);
        for (int i = 0; i < 5000; ++i) {
            auto r = make(QStringLiteral("Document %1 %2").arg(i).arg(i % 7 == 0 ? QStringLiteral("entropie") : QStringLiteral("algèbre")),
                          QString::number(i), {QStringLiteral("Auteur %1").arg(i % 50)}, {QStringLiteral("t%1").arg(i % 20)});
            QVERIFY(repo.insert(r).ok());
        }
        QElapsedTimer t;
        t.start();
        DocumentQuery q;
        q.text = QStringLiteral("entropie");
        q.sort = DocumentQuery::Sort::Title;
        const auto rows = repo.list(q).value();
        const qint64 ms = t.elapsed();
        QCOMPARE(rows.size(), 715);
        qInfo() << "Filtre plein texte sur 5 000 documents :" << ms << "ms";
        QVERIFY2(ms < 100, "filtre > 100 ms");
    }
};

QTEST_GUILESS_MAIN(TstDocumentRepository)
#include "tst_document_repository.moc"
