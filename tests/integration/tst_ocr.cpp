#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/repositories/PageTextRepository.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "engines/ocr/OcrPage.h"
#include "engines/ocr/TesseractOcr.h"
#include "services/search/OcrIndexer.h"
#include "services/search/SearchService.h"
#include "services/search/TextIndexer.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;
using namespace studylab::test;

/// OCR facultatif (V1-7b) : format des mots reconnus, Tesseract chargé à l'exécution, file d'OCR des PDF numérisés.
/// Sans Tesseract (CI Windows), les tests qui en ont besoin sont ignorés : l'application doit alors fonctionner sans.
class TstOcr : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    std::unique_ptr<Database> m_db;

    QString libraryDir() const { return m_tmp.filePath(QStringLiteral("library")); }

    qint64 addPdf(const QString& title, const PdfSpec& spec)
    {
        const QString file = title + QStringLiteral(".pdf");
        if (!writePdf(QDir(libraryDir()).filePath(file), spec))
            qFatal("PDF non écrit");
        DocumentRecord d;
        d.title = title;
        d.format = DocumentFormat::Pdf;
        d.filePath = file;
        d.fileHash = QString::number(qHash(title), 16);
        d.pageCount = spec.pages;
        DocumentRepository docs(*m_db);
        if (!docs.insert(d).ok())
            qFatal("insertion impossible");
        return d.id;
    }

    int textIndexed(qint64 id)
    {
        auto st = m_db->prepare(QStringLiteral("SELECT text_indexed FROM documents WHERE id = ?1")).value();
        st.bind(1, id);
        if (!st.step().value())
            return -99;
        return int(st.columnInt64(0));
    }

    void indexNativeText()
    {
        TextIndexer indexer(*m_db, libraryDir());
        QSignalSpy done(&indexer, &TextIndexer::finished);
        indexer.schedule(0);
        QVERIFY(done.wait(30000));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(QDir().mkpath(libraryDir()));
        qInfo() << "Tesseract" << (TesseractOcr::isAvailable() ? TesseractOcr::version() : TesseractOcr::unavailableReason())
                << TesseractOcr::installedLanguages();
    }

    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void pageFormat()
    {
        OcrPage page;
        page.words << OcrWord{QStringLiteral("Premier"), QRectF(0.1, 0.1, 0.2, 0.02), OcrWord::None}
                   << OcrWord{QStringLiteral("principe"), QRectF(0.32, 0.1, 0.2, 0.02), OcrWord::None}
                   << OcrWord{QStringLiteral("suite"), QRectF(0.1, 0.13, 0.1, 0.02), OcrWord::Line}
                   << OcrWord{QStringLiteral("Fin."), QRectF(0.1, 0.2, 0.08, 0.02), OcrWord::Paragraph};
        QCOMPARE(page.text(), QStringLiteral("Premier principe\nsuite\n\nFin."));
        QCOMPARE(page.offsets(), (QList<int>{0, 8, 17, 24}));
        const OcrPage back = OcrPage::fromJson(page.toJson());
        QCOMPARE(back.words.size(), 4);
        QCOMPARE(back.text(), page.text());
        QCOMPARE(back.words.at(1).box, QRectF(0.32, 0.1, 0.2, 0.02));
        QVERIFY(OcrPage::fromJson(QStringLiteral("pas du json")).isEmpty());
    }

    void repositoryQueue()
    {
        // Sans Tesseract : la file se repère en base (pages sans texte des PDF déjà indexés).
        PdfSpec scan;
        scan.title = QStringLiteral("Scan");
        scan.scanned = true;
        const qint64 scanned = addPdf(QStringLiteral("Scan"), scan);
        const qint64 native = addPdf(QStringLiteral("Natif"), PdfSpec{QStringLiteral("Natif"), 2,
                                                                      QStringLiteral("Le second principe de la thermodynamique.")});
        indexNativeText();
        QCOMPARE(textIndexed(scanned), 1);
        QCOMPARE(textIndexed(native), 1);
        PageTextRepository repo(*m_db);
        auto pending = repo.ocrPending();
        QVERIFY(pending.ok());
        QCOMPARE(pending.value().size(), 1);
        QCOMPARE(pending.value().first().id, scanned);
        QCOMPARE(repo.ocrProgress().value(), qMakePair(0, 1));

        QVERIFY(repo.storeOcr(scanned, 0, QStringLiteral("Texte reconnu de la page"), QStringLiteral("[]")).ok());
        QVERIFY(repo.storeOcr(native, 0, QStringLiteral("ne remplace pas le texte natif"), QStringLiteral("[]")).ok());
        QVERIFY(repo.text(native, 0).value().contains(QStringLiteral("thermodynamique")));
        QCOMPARE(repo.unitsWithText(scanned).value(), QList<int>{0});
        QVERIFY(repo.markOcrDone(scanned).ok());
        QCOMPARE(textIndexed(scanned), 2);
        QCOMPARE(repo.ocrProgress().value(), qMakePair(1, 0));
        QCOMPARE(repo.ocrWords(scanned).value().size(), 1);

        // Le texte reconnu est cherché comme le texte natif.
        SearchService search(*m_db);
        auto hits = search.search(QStringLiteral("reconnu"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().passages.size(), 1);
        QCOMPARE(hits.value().passages.first().documentId, scanned);

        // « Relancer » : le texte OCR est oublié, le PDF revient dans la file ; la réindexation native ne le touche pas.
        QVERIFY(repo.resetOcr().ok());
        QCOMPARE(textIndexed(scanned), 1);
        QVERIFY(!repo.text(scanned, 0).ok());
        QCOMPARE(repo.ocrPending().value().size(), 1);
    }

    void recognizeImage()
    {
        if (!TesseractOcr::isAvailable())
            QSKIP("Tesseract absent : OCR désactivé");
        PdfSpec spec;
        spec.title = QStringLiteral("Cours");
        spec.pages = 1;
        spec.scanned = true;
        const QString path = m_tmp.filePath(QStringLiteral("scan.pdf"));
        QVERIFY(writePdf(path, spec));
        QList<OcrPage> pages;
        auto r = OcrIndexer::recognizePdf(path, {0}, TesseractOcr::defaultLanguages(), nullptr,
                                          [&pages](int, const OcrPage& p) { pages << p; });
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().message));
        QCOMPARE(pages.size(), 1);
        const QString text = pages.first().text();
        QVERIFY2(text.contains(QStringLiteral("principe")) && text.contains(QStringLiteral("thermodynamique")),
                 qPrintable(text));
        for (const OcrWord& w : pages.first().words) {
            QVERIFY(QRectF(0, 0, 1, 1).contains(w.box));
            QVERIFY(w.box.width() > 0 && w.box.height() > 0);
        }
        // Le titre est en haut de la page, à gauche.
        QVERIFY(pages.first().words.first().box.top() < 0.25);
        QVERIFY(pages.first().words.first().box.left() < 0.2);

        // Langue non installée : repli sur les langues par défaut.
        TesseractOcr ocr;
        QVERIFY(ocr.init({QStringLiteral("xx-inexistante")}).ok());
    }

    void backgroundQueue()
    {
        if (!TesseractOcr::isAvailable())
            QSKIP("Tesseract absent : OCR désactivé");
        PdfSpec spec;
        spec.title = QStringLiteral("Polycopie");
        spec.pages = 2;
        spec.scanned = true;
        spec.bodyText = QStringLiteral("Le cycle de Carnot est réversible.");
        const qint64 id = addPdf(QStringLiteral("Polycopie"), spec);
        indexNativeText();

        OcrIndexer ocr(*m_db, libraryDir());
        QSignalSpy pages(&ocr, &OcrIndexer::pageRecognized);
        QSignalSpy done(&ocr, &OcrIndexer::finished);
        ocr.setEnabled(false);
        ocr.schedule(0);
        QVERIFY(!done.wait(300));  // désactivé : rien ne part
        ocr.setEnabled(true);
        ocr.schedule(0);
        QVERIFY(done.wait(120000));
        QCOMPARE(pages.size(), 2);
        QCOMPARE(textIndexed(id), 2);
        PageTextRepository repo(*m_db);
        QVERIFY(repo.text(id, 1).value().contains(QStringLiteral("Carnot")));
        const auto words = repo.ocrWords(id).value();
        QCOMPARE(words.size(), 2);
        QVERIFY(OcrPage::fromJson(words.at(1).second).text().contains(QStringLiteral("réversible")));

        SearchService search(*m_db);
        auto hits = search.search(QStringLiteral("carnot reversible"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().passages.size(), 2);

        // Arrêt en cours de route : les pages lues restent, la suite reprend.
        QVERIFY(repo.resetOcr().ok());
        ocr.schedule(0);
        QVERIFY(pages.wait(60000));
        ocr.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!ocr.isRunning(), 60000);
        QVERIFY(repo.unitsWithText(id).value().size() >= 1);
        done.clear();
        ocr.schedule(0);
        QVERIFY(done.wait(120000));
        QCOMPARE(textIndexed(id), 2);
        QCOMPARE(repo.unitsWithText(id).value().size(), 2);
    }
};

QTEST_MAIN(TstOcr)
#include "tst_ocr.moc"
