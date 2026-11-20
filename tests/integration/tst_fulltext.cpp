#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/repositories/PageTextRepository.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "engines/document/TextUnits.h"
#include "engines/epub/EpubTextExtractor.h"
#include "engines/pdf/PdfTextExtractor.h"
#include "services/notes/NoteService.h"
#include "services/search/SearchService.h"
#include "services/search/TextIndexer.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;
using namespace studylab::test;

/// Recherche plein texte (V1-5) : extraction PDF/EPUB, stockage page_text, indexation en tâche de fond, groupes
/// « Notes » et « Dans le texte des documents ».
class TstFullText : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    std::unique_ptr<Database> m_db;

    QString libraryDir() const { return m_tmp.filePath(QStringLiteral("library")); }

    DocumentRecord addDocument(const QString& title, DocumentFormat format, const QString& file,
                               StorageMode mode = StorageMode::Managed)
    {
        DocumentRecord d;
        d.title = title;
        d.format = format;
        d.storageMode = mode;
        d.filePath = file;
        d.fileHash = QString::number(qHash(title + file), 16);
        DocumentRepository docs(*m_db);
        if (!docs.insert(d).ok())
            qFatal("insertion impossible");
        return d;
    }

    int textIndexed(qint64 id)
    {
        auto st = m_db->prepare(QStringLiteral("SELECT text_indexed FROM documents WHERE id = ?1")).value();
        st.bind(1, id);
        if (!st.step().value())
            return -99;
        return int(st.columnInt64(0));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QVERIFY(QDir().mkpath(libraryDir()));
    }

    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void normalize()
    {
        QCOMPARE(normalizeExtractedText(QStringLiteral("La produc-\ntion d'entro-\n  pie")),
                 QStringLiteral("La production d'entropie"));
        QCOMPARE(normalizeExtractedText(QStringLiteral("  a\t\tb  \r\n\n\n\n c ")), QStringLiteral("a b\n\nc"));
        // Un tiret suivi d'une majuscule ou d'un espace n'est pas une césure.
        QCOMPARE(normalizeExtractedText(QStringLiteral("Jean-\nPaul")), QStringLiteral("Jean-Paul"));
        QVERIFY(normalizeExtractedText(QStringLiteral(" \n \n ")).isEmpty());
    }

    void xhtmlText()
    {
        const QByteArray x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Titre cach\xc3\xa9</title>"
                             "<style>p{color:red}</style></head>\n"
                             "<body><h1>Chapitre 1</h1><p>Le premier <em>principe</em>&#160;: l&#8217;\xc3\xa9nergie "
                             "se conserve.</p>\n<script>var x = 1;</script><ul><li>un</li><li>deux</li></ul></body></html>";
        const QString t = EpubTextExtractor::xhtmlText(x);
        QVERIFY2(t.contains(QStringLiteral("Le premier principe")), qPrintable(t));
        QVERIFY(t.contains(QStringLiteral("l’énergie se conserve")));
        QVERIFY(t.contains(QStringLiteral("Chapitre 1\n")));
        QVERIFY(t.contains(QStringLiteral("un\n")));
        QVERIFY(!t.contains(QStringLiteral("Titre caché")));
        QVERIFY(!t.contains(QStringLiteral("color")));
        QVERIFY(!t.contains(QStringLiteral("var x")));
        // XML mal formé : repli sur l'analyseur HTML.
        const QString loose = EpubTextExtractor::xhtmlText("<p>Texte <b>sans fermeture<p>Suite &amp; fin");
        QVERIFY2(loose.contains(QStringLiteral("sans fermeture")), qPrintable(loose));
        QVERIFY(loose.contains(QStringLiteral("Suite & fin")));
    }

    void extractPdf()
    {
        const QString path = m_tmp.filePath(QStringLiteral("thermo.pdf"));
        PdfSpec spec{QStringLiteral("Thermodynamique"), 3, QStringLiteral("L'entropie d'un système isolé croît.")};
        QVERIFY(writePdf(path, spec));
        auto r = PdfTextExtractor::extract(path, nullptr);
        QVERIFY(r.ok());
        const QStringList pages = r.value();
        QCOMPARE(pages.size(), 3);
        QVERIFY2(pages.at(1).contains(QStringLiteral("page 2")), qPrintable(pages.at(1)));
        QVERIFY(pages.at(2).contains(QStringLiteral("entropie")));

        std::atomic_bool cancel{true};
        QVERIFY(!PdfTextExtractor::extract(path, &cancel).ok());
        QVERIFY(!PdfTextExtractor::extract(m_tmp.filePath(QStringLiteral("absent.pdf")), nullptr).ok());
    }

    void extractEpub()
    {
        const QString path = m_tmp.filePath(QStringLiteral("livre.epub"));
        EpubSpec spec;
        spec.title = QStringLiteral("Livre");
        spec.chapters = {QStringLiteral("<h1>Un</h1><p>Le premier chapitre parle de diffusion.</p>"),
                         QStringLiteral("<p>Le second traite de la conduction thermique.</p>")};
        QVERIFY(writeEpub(path, spec));
        auto r = EpubTextExtractor::extract(path);
        QVERIFY(r.ok());
        const QStringList sections = r.value();
        QVERIFY(sections.size() >= 2);
        const int conduction = int(sections.indexOf(QRegularExpression(QStringLiteral(".*conduction.*"),
                                                                       QRegularExpression::DotMatchesEverythingOption)));
        const int diffusion = int(sections.indexOf(QRegularExpression(QStringLiteral(".*diffusion.*"),
                                                                      QRegularExpression::DotMatchesEverythingOption)));
        QVERIFY(diffusion >= 0);
        QCOMPARE(conduction, diffusion + 1);
    }

    void repository()
    {
        const DocumentRecord pdf = addDocument(QStringLiteral("Thermo"), DocumentFormat::Pdf, QStringLiteral("a.pdf"));
        const DocumentRecord epub = addDocument(QStringLiteral("Roman"), DocumentFormat::Epub,
                                                QStringLiteral("/ailleurs/b.epub"), StorageMode::Linked);
        PageTextRepository repo(*m_db);
        auto pending = repo.pending().value();
        QCOMPARE(pending.size(), 2);
        QCOMPARE(repo.progress().value(), qMakePair(0, 2));
        const auto linked = std::find_if(pending.begin(), pending.end(), [&](const auto& p) { return p.id == epub.id; });
        QVERIFY(linked != pending.end());
        QVERIFY(linked->linked);
        QCOMPARE(linked->format, QStringLiteral("epub"));

        QVERIFY(repo.replace(pdf.id, {QStringLiteral("Première page"), QString(), QStringLiteral("Le rendement de Carnot")})
                    .ok());
        QCOMPARE(textIndexed(pdf.id), 1);
        QCOMPARE(repo.text(pdf.id, 2).value(), QStringLiteral("Le rendement de Carnot"));
        QVERIFY(!repo.text(pdf.id, 1).ok());  // page vide omise
        QVERIFY(repo.markFailed(epub.id).ok());
        QCOMPARE(textIndexed(epub.id), -1);
        QVERIFY(repo.pending().value().isEmpty());
        QCOMPARE(repo.progress().value(), qMakePair(2, 2));

        // Remplacer n'empile pas : l'ancien texte disparaît de l'index.
        QVERIFY(repo.replace(pdf.id, {QStringLiteral("Cycle de Rankine")}).ok());
        SearchService search(*m_db);
        QVERIFY(search.search(QStringLiteral("carnot")).value().passages.isEmpty());
        QCOMPARE(search.search(QStringLiteral("rankine")).value().passages.size(), 1);

        QVERIFY(repo.resetAll().ok());
        QCOMPARE(repo.pending().value().size(), 2);
        QCOMPARE(search.search(QStringLiteral("rankine")).value().passages.size(), 1);  // conservé jusqu'au remplacement
    }

    void searchPassages()
    {
        const DocumentRecord pdf = addDocument(QStringLiteral("Thermo"), DocumentFormat::Pdf, QStringLiteral("a.pdf"));
        const DocumentRecord epub = addDocument(QStringLiteral("Roman"), DocumentFormat::Epub, QStringLiteral("b.epub"));
        PageTextRepository repo(*m_db);
        QStringList pages;
        for (int i = 0; i < 5; ++i)
            pages << QStringLiteral("Page %1 : l'entropie augmente toujours dans un système isolé.").arg(i + 1);
        QVERIFY(repo.replace(pdf.id, pages).ok());
        QVERIFY(repo.replace(epub.id, {QStringLiteral("Prologue"), QStringLiteral("La notion d'entropie selon Boltzmann.")})
                    .ok());

        SearchService search(*m_db);
        const SearchResults r = search.search(QStringLiteral("ENTROPIE")).value();
        QCOMPARE(r.passages.size(), 3);  // au plus deux passages par document
        int pdfHits = 0;
        for (const SearchHit& h : r.passages) {
            QCOMPARE(h.kind, SearchHit::Kind::Passage);
            QVERIFY(SearchService::plain(h.snippet).contains(QStringLiteral("entropie")));
            QVERIFY(h.snippet.contains(SearchHit::kMarkStart));
            if (h.documentId == pdf.id) {
                ++pdfHits;
                QCOMPARE(h.format, DocumentFormat::Pdf);
                QCOMPARE(h.location, QStringLiteral("p. %1").arg(h.unit + 1));
                QCOMPARE(h.documentTitle, QStringLiteral("Thermo"));
            } else {
                QCOMPARE(h.documentId, epub.id);
                QCOMPARE(h.unit, 1);
                QCOMPARE(h.location, QStringLiteral("section 2"));
            }
        }
        QCOMPARE(pdfHits, 2);
        QCOMPARE(search.search(QStringLiteral("boltzmann")).value().passages.size(), 1);

        // Document à la corbeille : ses passages ne sortent plus.
        DocumentRepository docs(*m_db);
        QVERIFY(docs.remove(epub.id).ok());
        QCOMPARE(search.search(QStringLiteral("boltzmann")).value().passages.size(), 0);
    }

    void searchNotes()
    {
        NoteService notes(*m_db, m_tmp.filePath(QStringLiteral("attachments")));
        auto n = notes.create(QStringLiteral("Cycle de Carnot"), NoteType::Concept, {},
                              QStringLiteral("---\ntags: [thermo]\n---\n# Cycle de Carnot\n\nDeux isothermes et "
                                             "deux **adiabatiques** ; voir [[Entropie]]."));
        QVERIFY(n.ok());
        SearchService search(*m_db);
        SearchResults r = search.search(QStringLiteral("adiabatiques")).value();
        QCOMPARE(r.notes.size(), 1);
        const SearchHit& h = r.notes.first();
        QCOMPARE(h.kind, SearchHit::Kind::Note);
        QCOMPARE(h.itemId, n.value().id);
        QCOMPARE(h.documentTitle, QStringLiteral("Cycle de Carnot"));
        QCOMPARE(h.location, noteTypeLabel(NoteType::Concept));
        const QString plain = SearchService::plain(h.snippet);
        QVERIFY2(!plain.contains(QStringLiteral("**")) && !plain.contains(QStringLiteral("[[")), qPrintable(plain));
        QVERIFY2(plain.contains(QStringLiteral("deux adiabatiques ; voir Entropie.")), qPrintable(plain));
        // Le titre suffit.
        QCOMPARE(search.search(QStringLiteral("carnot")).value().notes.size(), 1);
        QCOMPARE(r.total(), 1);
    }

    void indexer()
    {
        QVERIFY(writePdf(libraryDir() + QStringLiteral("/c.pdf"),
                         {QStringLiteral("Mécanique"), 2, QStringLiteral("Le moment cinétique se conserve.")}));
        EpubSpec spec;
        spec.title = QStringLiteral("Optique");
        spec.chapters = {QStringLiteral("<p>La réfraction suit la loi de Snell-Descartes.</p>")};
        const QString linked = m_tmp.filePath(QStringLiteral("optique.epub"));
        QVERIFY(writeEpub(linked, spec));
        const DocumentRecord pdf = addDocument(QStringLiteral("Mécanique"), DocumentFormat::Pdf, QStringLiteral("c.pdf"));
        const DocumentRecord epub = addDocument(QStringLiteral("Optique"), DocumentFormat::Epub, linked, StorageMode::Linked);
        const DocumentRecord missing = addDocument(QStringLiteral("Absent"), DocumentFormat::Pdf, QStringLiteral("x.pdf"));

        TextIndexer indexer(*m_db, libraryDir());
        QSignalSpy finished(&indexer, &TextIndexer::finished);
        QSignalSpy progress(&indexer, &TextIndexer::progressChanged);
        indexer.schedule(0);
        QVERIFY(finished.wait(30000));
        QVERIFY(!indexer.isRunning());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(0).toInt(), 3);
        QCOMPARE(progress.last().at(1).toInt(), 3);
        QCOMPARE(textIndexed(pdf.id), 1);
        QCOMPARE(textIndexed(epub.id), 1);
        QCOMPARE(textIndexed(missing.id), -1);

        SearchService search(*m_db);
        auto r = search.search(QStringLiteral("cinétique")).value();
        QCOMPARE(r.passages.size(), 2);
        QCOMPARE(r.passages.first().documentId, pdf.id);
        r = search.search(QStringLiteral("refraction")).value();
        QCOMPARE(r.passages.size(), 1);
        QCOMPARE(r.passages.first().format, DocumentFormat::Epub);

        // Réindexer : tout repasse, sans doublon.
        finished.clear();
        QVERIFY(indexer.reindexAll().ok());
        QVERIFY(finished.wait(30000));
        QCOMPARE(search.search(QStringLiteral("cinétique")).value().passages.size(), 2);
        QCOMPARE(indexer.progress(), qMakePair(3, 3));
    }

    void indexerStop()
    {
        for (int i = 0; i < 3; ++i) {
            const QString file = QStringLiteral("s%1.pdf").arg(i);
            QVERIFY(writePdf(libraryDir() + QLatin1Char('/') + file, {QStringLiteral("Doc %1").arg(i), 2}));
            addDocument(QStringLiteral("Doc %1").arg(i), DocumentFormat::Pdf, file);
        }
        TextIndexer indexer(*m_db, libraryDir());
        indexer.schedule(0);
        QTRY_VERIFY(indexer.isRunning());
        indexer.stop();
        QTRY_VERIFY(!indexer.isRunning());  // le document en cours s'interrompt
        QTest::qWait(200);
        QVERIFY(indexer.progress().first < 3);
        // Reprise.
        QSignalSpy finished(&indexer, &TextIndexer::finished);
        indexer.schedule(0);
        QVERIFY(finished.wait(30000));
        QCOMPARE(indexer.progress(), qMakePair(3, 3));
    }
};

QTEST_MAIN(TstFullText)
#include "tst_fulltext.moc"
