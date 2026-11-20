#include "support/WebEngineSetup.h"

#include "Bootstrap.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "engines/ocr/TesseractOcr.h"
#include "services/search/OcrIndexer.h"
#include "services/search/SearchService.h"
#include "services/search/TextIndexer.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/notes/NotesPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/reader/EpubReaderTab.h"
#include "ui/reader/EpubView.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SearchPalette.h"
#include "ui/theme/ThemeManager.h"

#include <QDir>
#include <QPdfDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::test;

namespace {
constexpr int kTimeout = 30000;
}

/// Recherche plein texte dans l'application (V1-5) : Ctrl K trouve les notes et le texte des documents, ouvre
/// la note ou le document au bon passage ; état de l'index et réindexation dans les paramètres.
class TstFullTextUi : public QObject {
    Q_OBJECT

    struct Env {
        QTemporaryDir dir;
        AppPaths paths{dir.path()};
        std::unique_ptr<db::Database> db;
        std::unique_ptr<SettingsService> settings;
        std::unique_ptr<ui::ThemeManager> theme;
        std::unique_ptr<LibraryService> library;
        std::unique_ptr<AnnotationService> annotations;
        std::unique_ptr<CommentService> comments;
        std::unique_ptr<SearchService> search;
        std::unique_ptr<BackupService> backups;
        std::unique_ptr<NoteService> notes;
        std::unique_ptr<OrganizeService> organize;
        std::unique_ptr<AppContext> ctx;

        Env()
        {
            auto startup = openAndMigrate(paths);
            db = std::move(startup.value().db);
            settings = std::make_unique<SettingsService>(*db);
            settings->setThemeMode(ThemeMode::Light);
            theme = std::make_unique<ui::ThemeManager>(*settings);
            theme->apply();
            library = std::make_unique<LibraryService>(*db, paths);
            annotations = std::make_unique<AnnotationService>(*db);
            comments = std::make_unique<CommentService>(*db);
            search = std::make_unique<SearchService>(*db);
            backups = std::make_unique<BackupService>(paths, *db);
            notes = std::make_unique<NoteService>(*db, paths.attachmentsDir());
            organize = std::make_unique<OrganizeService>(*db);
            ctx = std::make_unique<AppContext>(AppContext{paths, *db, *settings, *theme, *library, *annotations,
                                                          *comments, *search, *backups, *notes, *organize});
        }

        qint64 import(const QString& file)
        {
            QSignalSpy finished(library.get(), &LibraryService::importFinished);
            library->importPaths({file});
            if (!finished.wait(60000))
                return 0;
            const auto report = finished.first().at(0).value<ImportReport>();
            return report.importedIds.isEmpty() ? 0 : report.importedIds.first();
        }
    };

    QTemporaryDir m_files;
    QString m_pdf;
    QString m_epub;
    QString m_scan;

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(900);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    static bool waitIndexed(ui::MainWindow& window)
    {
        TextIndexer* indexer = window.textIndexer();
        QSignalSpy finished(indexer, &TextIndexer::finished);
        indexer->schedule(0);
        if (!finished.wait(kTimeout))
            return false;
        const auto p = indexer->progress();
        return p.first == p.second;
    }

    /// Ligne de la palette qui porte ce résultat.
    static int rowOf(ui::SearchPalette* palette, SearchHit::Kind kind, qint64 itemId)
    {
        for (int i = 0; i < palette->list()->count(); ++i) {
            const QVariant v = palette->list()->item(i)->data(Qt::UserRole + 1);
            if (!v.isValid())
                continue;
            const auto hit = v.value<SearchHit>();
            if (hit.kind == kind && hit.itemId == itemId)
                return i;
        }
        return -1;
    }

    static void activateRow(ui::SearchPalette* palette, int row)
    {
        palette->list()->setCurrentRow(row);
        QTest::keyClick(palette->input(), Qt::Key_Return);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(kWebEngineReady);
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("carnot.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Machines thermiques"), 4,
                                 QStringLiteral("Le rendement d'une machine ditherme est borné par celui de Carnot.")}));
        m_epub = m_files.filePath(QStringLiteral("optique.epub"));
        EpubSpec e;
        e.title = QStringLiteral("Optique géométrique");
        e.chapters = {QStringLiteral("<h1>Rayons</h1><p>La lumière se propage en ligne droite.</p>"),
                      QStringLiteral("<h1>Dioptres</h1><p>La réfraction obéit à la loi de Snell-Descartes.</p>"),
                      QStringLiteral("<h1>Lentilles</h1><p>Une lentille mince forme une image.</p>")};
        e.tocTitles = {QStringLiteral("Rayons"), QStringLiteral("Dioptres"), QStringLiteral("Lentilles")};
        QVERIFY(writeEpub(m_epub, e));
        m_scan = m_files.filePath(QStringLiteral("polycopie.pdf"));
        PdfSpec scan;
        scan.title = QStringLiteral("Polycopie numérisé");
        scan.pages = 3;
        scan.scanned = true;
        scan.bodyText = QStringLiteral("Le cycle de Carnot comporte deux isothermes et deux adiabatiques.");
        QVERIFY(writePdf(m_scan, scan));
    }

    void paletteFindsNotesAndPassages()
    {
        Env env;
        const qint64 pdf = env.import(m_pdf);
        QVERIFY(pdf > 0);
        auto note = env.notes->create(QStringLiteral("Cycle de Carnot"), NoteType::Concept, {},
                                      QStringLiteral("# Cycle de Carnot\n\nDeux isothermes et deux **adiabatiques**."));
        QVERIFY(note.ok());

        ui::MainWindow window(*env.ctx);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(waitIndexed(window));

        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        ui::SearchPalette* palette = window.searchPalette();
        QTRY_VERIFY(palette->isVisible());
        QTest::keyClicks(palette->input(), QStringLiteral("carnot"));
        QTRY_COMPARE(palette->results().notes.size(), 1);
        QCOMPARE(palette->results().notes.first().itemId, note.value().id);
        QCOMPARE(palette->results().passages.size(), 2);  // deux pages sur quatre au plus
        QVERIFY(palette->results().passages.first().location.startsWith(QStringLiteral("p. ")));
        bool notesHeader = false, passagesHeader = false;
        for (int i = 0; i < palette->list()->count(); ++i) {
            const QString t = palette->list()->item(i)->text();
            notesHeader |= t.startsWith(QStringLiteral("NOTES · 1"));
            passagesHeader |= t.startsWith(QStringLiteral("DANS LE TEXTE DES DOCUMENTS · 2"));
        }
        QVERIFY(notesHeader && passagesHeader);
        maybeScreenshot(window, QStringLiteral("v1-5-recherche"));

        // Une note s'ouvre dans « Mes notes ».
        activateRow(palette, rowOf(palette, SearchHit::Kind::Note, note.value().id));
        QVERIFY(!palette->isVisible());
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        auto* notes = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QVERIFY(notes);
        QTRY_COMPARE(notes->currentNoteId(), note.value().id);

        // Un passage ouvre le document à la page, terme cherché.
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(palette->isVisible());
        palette->search(QStringLiteral("ditherme borne"));
        QVERIFY(!palette->results().passages.isEmpty());
        const SearchHit passage = palette->results().passages.last();
        QVERIFY(passage.unit > 0);
        activateRow(palette, rowOf(palette, SearchHit::Kind::Passage, passage.itemId));
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->tabFor(pdf));
        QVERIFY(tab);
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->currentPage(), passage.unit, kTimeout);
        QCOMPARE(tab->searchEdit()->text(), QStringLiteral("ditherme"));
        QVERIFY(tab->searchEdit()->isVisible());
        QTRY_VERIFY(!tab->searchCountLabel()->text().isEmpty());
        maybeScreenshot(window, QStringLiteral("v1-5-passage"));
    }

    void epubPassageOpensSection()
    {
        Env env;
        const qint64 epub = env.import(m_epub);
        QVERIFY(epub > 0);
        ui::MainWindow window(*env.ctx);
        window.resize(1300, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(waitIndexed(window));

        ui::SearchPalette* palette = window.searchPalette();
        palette->popup();
        palette->search(QStringLiteral("refraction"));
        QCOMPARE(palette->results().passages.size(), 1);
        const SearchHit hit = palette->results().passages.first();
        QCOMPARE(hit.format, DocumentFormat::Epub);
        QVERIFY(hit.unit >= 1);
        activateRow(palette, rowOf(palette, SearchHit::Kind::Passage, hit.itemId));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        auto* tab = qobject_cast<ui::EpubReaderTab*>(reader->tabFor(epub));
        QVERIFY(tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->view() && tab->isOpened(), kTimeout);  // vue créée à l'affichage de l'onglet
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, hit.unit, kTimeout);
        QCOMPARE(tab->searchEdit()->text(), QStringLiteral("refraction"));
        QTRY_VERIFY_WITH_TIMEOUT(tab->view()->searchDone() && tab->view()->searchResultCount() >= 1, kTimeout);
    }

    void scannedPdfIsReadByOcr()
    {
        if (!TesseractOcr::isAvailable())
            QSKIP("Tesseract absent : OCR désactivé");
        Env env;
        const qint64 scan = env.import(m_scan);
        QVERIFY(scan > 0);
        ui::MainWindow window(*env.ctx);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        OcrIndexer* ocr = window.ocrIndexer();
        QVERIFY(ocr && ocr->isEnabled());
        QSignalSpy ocrDone(ocr, &OcrIndexer::finished);
        QVERIFY(waitIndexed(window));
        QVERIFY(ocrDone.wait(120000));
        QCOMPARE(ocr->progress(), qMakePair(1, 0));

        // Ctrl K trouve le texte reconnu et ouvre la page numérisée, terme surligné.
        ui::SearchPalette* palette = window.searchPalette();
        palette->popup();
        palette->search(QStringLiteral("adiabatiques"));
        QCOMPARE(palette->results().passages.size(), 2);  // au plus deux passages par document
        const SearchHit hit = palette->results().passages.first();
        activateRow(palette, rowOf(palette, SearchHit::Kind::Passage, hit.itemId));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->tabFor(scan));
        QVERIFY(tab);
        QTRY_VERIFY_WITH_TIMEOUT(tab->isLoaded(), kTimeout);
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->currentPage(), hit.unit, kTimeout);
        QVERIFY(tab->view()->hasOcrText(hit.unit));
        QTRY_VERIFY(tab->ocrBadge()->isVisible());
        QTRY_COMPARE(tab->view()->searchResultCount(), 3);
        QTRY_VERIFY(tab->view()->currentSearchResult() >= 0);
        QCOMPARE(tab->view()->searchResultPage(tab->view()->currentSearchResult()), hit.unit);
        QCOMPARE(tab->searchCountLabel()->text(), QStringLiteral("%1 sur 3").arg(tab->view()->currentSearchResult() + 1));

        // Sélection et annotation du texte reconnu, comme sur une page native.
        const QRectF page = tab->view()->pageRect(1);
        QVERIFY(!page.isEmpty());
        const QSizeF pts = tab->view()->document()->pagePointSize(1);
        tab->view()->selectText(1, QPointF(0, 0), 1, QPointF(pts.width(), pts.height()));
        QVERIFY2(tab->view()->selectedText().contains(QStringLiteral("isothermes")), qPrintable(tab->view()->selectedText()));
        const auto anchors = tab->view()->selectionAnchors();
        QCOMPARE(anchors.size(), 1);
        QVERIFY(anchors.first().rects.size() >= 2);  // une zone par ligne
        QVERIFY(anchors.first().start == 0);
        auto added = tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Yellow);
        QVERIFY2(added.ok(), added.ok() ? "" : qPrintable(added.error().message));
        QCOMPARE(added.value().size(), 1);
        QVERIFY(added.value().first().selectedText.contains(QStringLiteral("Carnot")));
        // Un clic sans glisser ne sélectionne rien.
        tab->view()->selectText(1, QPointF(100, 100), 1, QPointF(100, 100));
        QVERIFY(!tab->view()->hasSelection());
        tab->view()->goToPage(0);
        maybeScreenshot(window, QStringLiteral("v1-7-ocr"));

        // Paramètres : état de l'OCR, « Relire » remet le document en file.
        auto* settings = qobject_cast<ui::SettingsPage*>(window.page(QStringLiteral("settings")));
        QVERIFY(settings);
        QCOMPARE(settings->ocrStatus()->text(), QStringLiteral("Pages numérisées lues : 1 document."));
        QVERIFY(settings->ocrSwitch()->isChecked());
        QVERIFY(settings->ocrLanguagesButton()->text().contains(QStringLiteral("Français")));
        window.navigateTo(QStringLiteral("settings"));
        settings->categories()->setCurrentRow(2);
        maybeScreenshot(window, QStringLiteral("v1-7-ocr-parametres"));
        ocrDone.clear();
        QTest::mouseClick(settings->ocrRerunButton(), Qt::LeftButton);
        QVERIFY(ocrDone.wait(120000));
        QCOMPARE(settings->ocrStatus()->text(), QStringLiteral("Pages numérisées lues : 1 document."));
        // Désactivé : réglage enregistré, plus de lecture.
        settings->ocrSwitch()->click();
        QVERIFY(!env.settings->value(SettingKeys::OcrEnabled, true).toBool());
        QVERIFY(!ocr->isEnabled());
        QCOMPARE(settings->ocrStatus()->text(), QStringLiteral("OCR désactivé : les pages numérisées ne sont pas lues."));
    }

    void settingsShowIndexState()
    {
        Env env;
        QVERIFY(env.import(m_pdf) > 0);
        QVERIFY(env.import(m_epub) > 0);
        TextIndexer indexer(*env.db, env.paths.libraryDir());
        ui::SettingsPage page(*env.ctx);
        page.setTextIndexer(&indexer);
        page.resize(1100, 800);
        page.show();
        QVERIFY(page.reindexButton()->isEnabled());
        QCOMPARE(page.indexStatus()->text(), QStringLiteral("Indexation en cours : 0 sur 2 documents."));

        QSignalSpy finished(&indexer, &TextIndexer::finished);
        indexer.schedule(0);
        QVERIFY(finished.wait(kTimeout));
        QCOMPARE(page.indexStatus()->text(), QStringLiteral("Texte de tous les documents indexé (2 documents)."));

        finished.clear();
        QTest::mouseClick(page.reindexButton(), Qt::LeftButton);
        QVERIFY(page.indexStatus()->text().startsWith(QStringLiteral("Indexation en cours")));
        QVERIFY(finished.wait(kTimeout));
        QVERIFY(page.indexStatus()->text().startsWith(QStringLiteral("Texte de tous les documents indexé")));
        // Les résultats restent disponibles pendant et après la réindexation.
        QCOMPARE(env.search->search(QStringLiteral("snell")).value().passages.size(), 1);
    }
};

QTEST_MAIN(TstFullTextUi)
#include "tst_fulltext_ui.moc"
