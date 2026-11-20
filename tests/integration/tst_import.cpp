#include "core/AppPaths.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "engines/epub/EpubPackage.h"
#include "engines/pdf/PdfMetadataReader.h"
#include "services/library/FileInspector.h"
#include "services/library/LibraryService.h"
#include "support/Fixtures.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::test;

class TstImport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_src;

    static ImportReport runImport(LibraryService& lib, const QStringList& paths, StorageMode mode = StorageMode::Managed)
    {
        QSignalSpy finished(&lib, &LibraryService::importFinished);
        lib.importPaths(paths, mode);
        if (!finished.wait(30000))
            qFatal("import trop long");
        return finished.takeFirst().at(0).value<ImportReport>();
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImportReport>();
        QVERIFY(m_tmp.isValid());
        m_src = m_tmp.filePath(QStringLiteral("sources"));
        QVERIFY(QDir().mkpath(m_src + QStringLiteral("/cours/sous-dossier")));
        QVERIFY(writePdf(m_src + QStringLiteral("/thermo.pdf"), {QStringLiteral("Thermodynamique appliquée"), 3}));
        QVERIFY(writePdf(m_src + QStringLiteral("/cours/sans_titre_cours-2.pdf"), {QStringLiteral("untitled"), 1}));
        QVERIFY(writePdf(m_src + QStringLiteral("/cours/sous-dossier/analyse.pdf"), {QStringLiteral("Analyse réelle"), 2}));
        QVERIFY(QFile::copy(m_src + QStringLiteral("/thermo.pdf"), m_src + QStringLiteral("/cours/copie de thermo.pdf")));
        EpubSpec e3;
        e3.title = QStringLiteral("L'Écume des jours");
        e3.authors = {QStringLiteral("Boris Vian")};
        e3.isbn = QStringLiteral("978-2-07-036818-1");
        e3.subjects = {QStringLiteral("Roman")};
        QVERIFY(writeEpub(m_src + QStringLiteral("/cours/vian.epub"), e3));
        EpubSpec e2;
        e2.title = QStringLiteral("Candide");
        e2.authors = {QStringLiteral("Voltaire")};
        e2.epub3 = false;
        QVERIFY(writeEpub(m_src + QStringLiteral("/candide.epub"), e2));
        EpubSpec drm;
        drm.title = QStringLiteral("Protégé");
        drm.drm = true;
        QVERIFY(writeEpub(m_src + QStringLiteral("/drm.epub"), drm));
        QFile fake(m_src + QStringLiteral("/faux.pdf"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("Ceci n'est pas un PDF");
        fake.close();
        QFile notes(m_src + QStringLiteral("/cours/notes.txt"));  // ignoré lors du parcours d'un dossier
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("txt");
    }

    void detectsFormatsByContent()
    {
        QCOMPARE(FileInspector::detectFormat(m_src + QStringLiteral("/thermo.pdf")), DocumentFormat::Pdf);
        QCOMPARE(FileInspector::detectFormat(m_src + QStringLiteral("/candide.epub")), DocumentFormat::Epub);
        QVERIFY(!FileInspector::detectFormat(m_src + QStringLiteral("/faux.pdf")).has_value());
        QCOMPARE(FileInspector::detectFormat(m_src + QStringLiteral("/cours/notes.txt")), DocumentFormat::Txt);
        QCOMPARE(FileInspector::detectFormat(QStringLiteral(STUDYLAB_TEST_DATA "/text/cours.md")), DocumentFormat::Markdown);
        QFile binary(m_tmp.filePath(QStringLiteral("binaire.txt")));
        QVERIFY(binary.open(QIODevice::WriteOnly));
        binary.write(QByteArray("abc\0def", 7));
        binary.close();
        QVERIFY(!FileInspector::detectFormat(binary.fileName()).has_value());
        QCOMPARE(FileInspector::sha256(m_src + QStringLiteral("/thermo.pdf")).value(),
                 FileInspector::sha256(m_src + QStringLiteral("/cours/copie de thermo.pdf")).value());
        QCOMPARE(FileInspector::sha256(m_src + QStringLiteral("/thermo.pdf")).value().size(), 64);
    }

    void readsPdfMetadata()
    {
        auto md = PdfMetadataReader::read(m_src + QStringLiteral("/thermo.pdf"));
        QVERIFY2(md.ok(), qPrintable(md.ok() ? QString() : md.error().message));
        QCOMPARE(md.value().title, QStringLiteral("Thermodynamique appliquée"));
        QCOMPARE(md.value().pageCount, 3);
        QVERIFY(!md.value().cover.isNull());
        QCOMPARE(md.value().cover.width(), kCoverWidth);
        QVERIFY(md.value().cover.height() > md.value().cover.width());  // A5 portrait

        auto junk = PdfMetadataReader::read(m_src + QStringLiteral("/cours/sans_titre_cours-2.pdf"));
        QCOMPARE(junk.value().title, QStringLiteral("sans titre cours 2"));
        QVERIFY(!PdfMetadataReader::read(m_src + QStringLiteral("/faux.pdf")).ok());
    }

    void readsEpubMetadata()
    {
        auto pkg = EpubPackage::open(m_src + QStringLiteral("/cours/vian.epub"));
        QVERIFY2(pkg.ok(), qPrintable(pkg.ok() ? QString() : pkg.error().message));
        const DocumentMetadata md = pkg.value()->metadata();
        QCOMPARE(md.title, QStringLiteral("L'Écume des jours"));
        QCOMPARE(md.authors, QStringList{QStringLiteral("Boris Vian")});  // l'illustrateur est ignoré
        QCOMPARE(md.isbn, QStringLiteral("9782070368181"));
        QCOMPARE(md.year, 2019);
        QCOMPARE(md.language, QStringLiteral("fr"));
        QCOMPARE(md.publisher, QStringLiteral("Éditions Test"));
        QCOMPARE(md.description, QStringLiteral("Un résumé."));
        QCOMPARE(md.keywords, QStringList{QStringLiteral("Roman")});
        QCOMPARE(md.pageCount, 2);
        QCOMPARE(pkg.value()->coverHref(), QStringLiteral("OEBPS/Images/couverture finale.png"));
        QCOMPARE(md.cover.width(), kCoverWidth);
        QCOMPARE(pkg.value()->spine().first().href, QStringLiteral("OEBPS/Text/ch1.xhtml"));

        auto epub2 = EpubPackage::open(m_src + QStringLiteral("/candide.epub"));
        QVERIFY(epub2.ok());
        QCOMPARE(epub2.value()->version(), QStringLiteral("2.0"));
        QVERIFY(!epub2.value()->metadata().cover.isNull());  // <meta name="cover">
        QCOMPARE(epub2.value()->metadata().authors, QStringList{QStringLiteral("Voltaire")});

        auto drm = EpubPackage::open(m_src + QStringLiteral("/drm.epub"));
        QVERIFY(!drm.ok());
        QVERIFY(drm.error().message.contains(QStringLiteral("DRM")));
    }

    void importsFoldersFilesAndReportsProblems()
    {
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);
        QSignalSpy changed(&lib, &LibraryService::documentsChanged);

        const ImportReport r = runImport(lib, {m_src + QStringLiteral("/thermo.pdf"), m_src + QStringLiteral("/cours"),
                                               m_src + QStringLiteral("/candide.epub"), m_src + QStringLiteral("/drm.epub"),
                                               m_src + QStringLiteral("/faux.pdf")});
        QCOMPARE(r.requested, 8);  // notes.txt ignoré dans le dossier
        QCOMPARE(r.importedIds.size(), 5);  // thermo, sans titre, analyse, vian, candide
        QCOMPARE(r.duplicates.size(), 1);   // copie de thermo
        QVERIFY(r.duplicates.first().reason.contains(QStringLiteral("deux fois")));
        QCOMPARE(r.failures.size(), 2);     // DRM + faux PDF
        QVERIFY(!r.canceled);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(lib.count(), 5);

        // Fichiers gérés copiés sous library/ab/<sha>.pdf, couvertures écrites
        const auto all = lib.documents();
        for (const auto& d : all.value()) {
            QVERIFY2(QFileInfo::exists(lib.documentFile(d)), qPrintable(d.title));
            QVERIFY(d.filePath.startsWith(d.fileHash.left(2) + QLatin1Char('/')));
            QVERIFY2(QFileInfo::exists(lib.coverFile(d)), qPrintable(d.title));
        }

        // Réimport : tout est doublon de la bibliothèque, avec le titre existant
        const ImportReport again = runImport(lib, {m_src + QStringLiteral("/thermo.pdf")});
        QCOMPARE(again.importedIds.size(), 0);
        QCOMPARE(again.duplicates.size(), 1);
        QVERIFY(again.duplicates.first().reason.contains(QStringLiteral("Thermodynamique appliquée")));
    }

    void importsTextAndMarkdown()
    {
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);
        const ImportReport r = runImport(lib, {QStringLiteral(STUDYLAB_TEST_DATA "/text/cours.md"),
                                               QStringLiteral(STUDYLAB_TEST_DATA "/text/notes-cp1252.txt")});
        QCOMPARE(r.importedIds.size(), 2);
        QVERIFY(r.failures.isEmpty());
        const DocumentRecord md = lib.document(r.importedIds.at(0)).value();
        QCOMPARE(md.format, DocumentFormat::Markdown);
        QCOMPARE(md.title, QStringLiteral("Thermodynamique — cours"));
        QCOMPARE(md.authors.size(), 2);
        QCOMPARE(md.pageCount, 3);  // sections
        QVERIFY(md.filePath.endsWith(QStringLiteral(".md")));
        QVERIFY(QFileInfo::exists(lib.documentFile(md)));
        const DocumentRecord txt = lib.document(r.importedIds.at(1)).value();
        QCOMPARE(txt.format, DocumentFormat::Txt);
        QVERIFY(txt.filePath.endsWith(QStringLiteral(".txt")));
        // Le fichier copié dans la bibliothèque se lit comme l'original.
        QVERIFY(EpubPackage::open(lib.documentFile(txt)).ok());
    }

    void importsWordAndImages()
    {
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);

        QTemporaryDir src;
        const QString docx = src.filePath(QStringLiteral("fluides.docx"));
        const QString png = src.filePath(QStringLiteral("tableau_noir.png"));
        const QString jpg = src.filePath(QStringLiteral("photo.JPG"));
        QVERIFY(writeDocx(docx));
        QVERIFY(writeImage(png, QStringLiteral("Théorème de Bernoulli"), 1200, 900, 150));
        QVERIFY(writeImage(jpg, QStringLiteral("Photo"), 4000, 3000, 0));  // sans résolution utile
        QCOMPARE(FileInspector::detectFormat(docx), DocumentFormat::Docx);
        QCOMPARE(FileInspector::detectFormat(png), DocumentFormat::Image);
        QCOMPARE(FileInspector::detectFormat(jpg), DocumentFormat::Image);

        const ImportReport r = runImport(lib, {docx, png, jpg});
        QVERIFY(r.failures.isEmpty());
        QCOMPARE(r.importedIds.size(), 3);
        const DocumentRecord word = lib.document(r.importedIds.at(0)).value();
        QCOMPARE(word.format, DocumentFormat::Docx);
        QCOMPARE(word.title, QStringLiteral("Physique des fluides"));
        QCOMPARE(word.authors.size(), 2);
        QCOMPARE(word.pageCount, 3);  // sections
        QVERIFY(word.filePath.endsWith(QStringLiteral(".docx")));
        QCOMPARE(lib.readableFile(word).value(), lib.documentFile(word));  // lu tel quel

        const DocumentRecord image = lib.document(r.importedIds.at(1)).value();
        QCOMPARE(image.format, DocumentFormat::Image);
        QVERIFY(isPaged(image.format));
        QCOMPARE(image.title, QStringLiteral("tableau noir"));
        QCOMPARE(image.pageCount, 1);
        QVERIFY(image.filePath.endsWith(QStringLiteral(".png")));  // l'extension de l'image est conservée
        QVERIFY(QFileInfo::exists(lib.documentFile(image)));

        // Lecture : un PDF d'affichage construit dans le cache, à la taille de l'image (1200 × 900 px à 150 dpi).
        const QString pdf = LibraryService::imagePdfFile(paths, image);
        QVERIFY(!QFileInfo::exists(pdf));
        auto readable = lib.readableFile(image);
        QVERIFY2(readable.ok(), qPrintable(readable.ok() ? QString() : readable.error().message));
        QCOMPARE(readable.value(), pdf);
        QVERIFY(pdf.startsWith(paths.cacheDir()));
        auto md = PdfMetadataReader::read(pdf);
        QVERIFY(md.ok());
        QCOMPARE(md.value().pageCount, 1);
        const QDateTime built = QFileInfo(pdf).lastModified();
        QCOMPARE(lib.readableFile(image).value(), pdf);  // réutilisé
        QCOMPARE(QFileInfo(pdf).lastModified(), built);
        QFile::remove(pdf);
        QVERIFY(lib.readableFile(image).ok());  // reconstruit s'il a été effacé
        QVERIFY(QFileInfo::exists(pdf));

        const DocumentRecord photo = lib.document(r.importedIds.at(2)).value();
        QVERIFY(photo.filePath.endsWith(QStringLiteral(".jpg")));
        QVERIFY(lib.readableFile(photo).ok());

        // Parcours d'un dossier : les images n'y sont pas prises (photos, icônes…), le .docx oui.
        QTemporaryDir folder;
        QVERIFY(writeDocx(folder.filePath(QStringLiteral("autre.docx")), QStringLiteral("Un autre texte.")));
        QVERIFY(writeImage(folder.filePath(QStringLiteral("icone.png")), QStringLiteral("i"), 64, 64));
        const ImportReport f = runImport(lib, {folder.path()});
        QCOMPARE(f.importedIds.size(), 1);
        QCOMPARE(lib.document(f.importedIds.first()).value().format, DocumentFormat::Docx);
    }

    void linkedModeKeepsFileInPlace()
    {
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);
        const ImportReport r = runImport(lib, {m_src + QStringLiteral("/candide.epub")}, StorageMode::Linked);
        QCOMPARE(r.importedIds.size(), 1);
        const auto d = lib.document(r.importedIds.first()).value();
        QCOMPARE(d.storageMode, StorageMode::Linked);
        QCOMPARE(lib.documentFile(d), QFileInfo(m_src + QStringLiteral("/candide.epub")).canonicalFilePath());
        QVERIFY(QDir(paths.libraryDir()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }

    void largeImportStaysResponsive()
    {
        // 120 PDF distincts : l'interface (boucle d'événements) reste réactive pendant l'import.
        QTemporaryDir many;
        for (int i = 0; i < 120; ++i)
            QVERIFY(writePdf(many.filePath(QStringLiteral("doc-%1.pdf").arg(i, 3, 10, QLatin1Char('0'))),
                             {QStringLiteral("Document %1").arg(i), 1, QStringLiteral("Contenu %1").arg(i)}));
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);

        QElapsedTimer gap;
        qint64 worstGap = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QObject::connect(&heartbeat, &QTimer::timeout, [&] {
            worstGap = qMax(worstGap, gap.restart());
        });
        QSignalSpy progress(&lib, &LibraryService::importProgress);
        QSignalSpy finished(&lib, &LibraryService::importFinished);
        gap.start();
        heartbeat.start();
        lib.importPaths({many.path()});
        QVERIFY(finished.wait(60000));
        heartbeat.stop();
        QCOMPARE(finished.first().at(0).value<ImportReport>().importedIds.size(), 120);
        QVERIFY(progress.count() > 0);
        qInfo() << "Plus long blocage de la boucle d'événements pendant l'import :" << worstGap << "ms";
        QVERIFY2(worstGap < 250, "l'interface a gelé pendant l'import");
    }

    void cancelStopsImport()
    {
        QTemporaryDir many;
        for (int i = 0; i < 60; ++i)
            QVERIFY(writePdf(many.filePath(QStringLiteral("c-%1.pdf").arg(i)), {QStringLiteral("C %1").arg(i), 1,
                                                                                  QStringLiteral("x%1").arg(i)}));
        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);
        QSignalSpy finished(&lib, &LibraryService::importFinished);
        lib.importPaths({many.path()});
        lib.cancelImport();
        QVERIFY(finished.wait(30000));
        const auto report = finished.first().at(0).value<ImportReport>();
        QVERIFY(report.canceled);
        QVERIFY(report.importedIds.size() < 60);
        QCOMPARE(lib.count(), report.importedIds.size());
    }
};

QTEST_MAIN(TstImport)
#include "tst_import.moc"
