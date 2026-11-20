#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "engines/pdf/PdfAnnotationIO.h"
#include "engines/pdf/PdfRenderQueue.h"
#include "services/pdfinterop/PdfInteropService.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/library/LibraryPage.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfSearchModel>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::test;

class TstPdfReader : public QObject {
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
            ctx = std::make_unique<AppContext>(AppContext{paths, *db, *settings, *theme, *library, *annotations, *comments, *search, *backups, *notes, *organize});
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

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(600);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    QTemporaryDir m_files;
    QString m_pdf;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("cours.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Thermodynamique"), 6,
                                 QStringLiteral("Le second principe introduit l'entropie.")}));
    }

    void renderQueueProducesPreviewAndTiles()
    {
        QPdfDocument doc;
        QCOMPARE(doc.load(m_pdf), QPdfDocument::Error::None);
        PdfRenderQueue queue(&doc);
        QSignalSpy ready(&queue, &PdfRenderQueue::tileReady);
        const PdfTileKey preview{0, PdfRenderQueue::kPreviewWidth, -1, -1};
        const PdfTileKey tile{1, 1200, 1, 0};
        queue.setWanted({preview, tile});
        QTRY_VERIFY(!queue.cached(preview).isNull());
        QTRY_VERIFY(!queue.cached(tile).isNull());
        QCOMPARE(queue.cached(preview).width(), PdfRenderQueue::kPreviewWidth);
        QCOMPARE(queue.cached(tile).size(), queue.tileRect(tile).size());
        QCOMPARE(queue.tileRect(tile).left(), PdfRenderQueue::kTileSize);
        // Une demande remplacée avant d'être rendue est abandonnée
        QList<PdfTileKey> many;
        for (int p = 0; p < 6; ++p)
            for (int c = 0; c < 3; ++c)
                many << PdfTileKey{p, 2400, c, 1};
        queue.setWanted(many);
        queue.setWanted({});
        QTRY_COMPARE(queue.pendingCount(), 0);
        QVERIFY(ready.count() < 2 + many.size());
    }

    void viewLayoutZoomRotationAndPosition()
    {
        Env env;
        QPdfDocument doc;
        QCOMPARE(doc.load(m_pdf), QPdfDocument::Error::None);
        PdfRenderQueue queue(&doc);
        ui::PdfView view(*env.theme);
        view.resize(900, 700);
        view.setDocument(&doc, &queue);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(view.pageCount(), 6);

        // Ajuster à la largeur : la page occupe la largeur moins les marges
        QCOMPARE(view.zoomMode(), ui::PdfView::ZoomMode::FitWidth);
        QVERIFY(qAbs(view.pageRect(0).width() - (view.viewport()->width() - 48)) <= 2);

        view.goToPage(3);
        QCOMPARE(view.currentPage(), 3);
        QVERIFY(view.canGoBack());
        view.goBack();
        QCOMPARE(view.currentPage(), 0);
        view.goForward();
        QCOMPARE(view.currentPage(), 3);

        // La position (page + décalage) survit à un changement de zoom
        view.setPosition({2, 100});
        QCOMPARE(view.position().page, 2);
        QVERIFY(qAbs(view.position().offset - 100) < 2);
        view.setZoom(2.0);
        QCOMPARE(view.zoomMode(), ui::PdfView::ZoomMode::Custom);
        QCOMPARE(view.currentPage(), 2);
        const ui::PdfPosition before = view.position();
        QCOMPARE(before.page, 2);
        view.setZoomMode(ui::PdfView::ZoomMode::FitWidth);
        QCOMPARE(view.position().page, 2);
        QVERIFY(qAbs(view.position().offset - before.offset) < 2);

        // Rotation : largeur et hauteur échangées
        const QSizeF portrait = view.pageRect(0).size();
        view.setZoom(1.0);
        const QSizeF atOne = view.pageRect(0).size();
        view.setRotation(90);
        QVERIFY(qAbs(view.pageRect(0).width() - atOne.height()) < 1);
        QVERIFY(qAbs(view.pageRect(0).height() - atOne.width()) < 1);
        // Correspondance aller-retour des coordonnées avec rotation
        const QPoint vp = view.mapFromPage(0, QPointF(50, 80));
        const QPointF back = view.mapToPage(0, vp);
        QVERIFY(qAbs(back.x() - 50) < 1.5 && qAbs(back.y() - 80) < 1.5);
        view.setRotation(0);
        Q_UNUSED(portrait)

        // Double page : couverture seule, puis pages 2-3 côte à côte
        view.setDoublePage(true);
        QCOMPARE(view.pageRect(1).top(), view.pageRect(2).top());
        QVERIFY(view.pageRect(2).left() > view.pageRect(1).right());
        QVERIFY(view.pageRect(1).top() > view.pageRect(0).bottom());
    }

    void selectionCopyAndSearch()
    {
        Env env;
        QPdfDocument doc;
        QCOMPARE(doc.load(m_pdf), QPdfDocument::Error::None);
        PdfRenderQueue queue(&doc);
        ui::PdfView view(*env.theme);
        view.resize(900, 700);
        view.setDocument(&doc, &queue);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const QSizeF size = doc.pagePointSize(0);
        view.selectText(0, QPointF(0, 0), 0, QPointF(size.width(), size.height()));
        QVERIFY(view.hasSelection());
        QVERIFY2(view.selectedText().contains(QStringLiteral("entropie")), qPrintable(view.selectedText()));
        view.copySelection();
        QCOMPARE(QGuiApplication::clipboard()->text(), view.selectedText());
        // Sélection sur plusieurs pages
        view.selectText(1, QPointF(0, 0), 2, QPointF(size.width(), size.height()));
        QVERIFY(view.selectedText().contains(QStringLiteral("page 2")));
        QVERIFY(view.selectedText().contains(QStringLiteral("page 3")));
        view.clearSelection();
        QVERIFY(!view.hasSelection());

        // Sélection à la souris
        const QPoint a = view.mapFromPage(0, QPointF(30, 50));
        const QPoint b = view.mapFromPage(0, QPointF(size.width() - 10, 100));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, a);
        QTest::mouseMove(view.viewport(), b);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, b);
        QVERIFY(view.hasSelection());

        QPdfSearchModel search;
        search.setDocument(&doc);
        view.setSearchModel(&search);
        search.setSearchString(QStringLiteral("entropie"));
        QTRY_COMPARE(search.rowCount({}), 6);
        view.setCurrentSearchResult(4);
        QCOMPARE(view.currentPage(), 4);
    }

    void readerTabRestoresPosition()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        QVERIFY(id > 0);
        {
            ui::ReaderPage reader(*env.ctx);
            reader.resize(1200, 800);
            reader.show();
            QVERIFY(QTest::qWaitForWindowExposed(&reader));
            reader.openDocument(id);
            QCOMPARE(reader.tabCount(), 1);
            auto* tab = qobject_cast<ui::PdfReaderTab*>(reader.currentTab());
            QTRY_VERIFY(tab->isLoaded());
            QCOMPARE(env.library->document(id).value().status, ReadingStatus::Reading);  // « À lire » → « En cours »
            QVERIFY(!env.library->document(id).value().lastOpenedAt.isEmpty());

            // Aller à la page 5 par le champ de page, zoom 150 %
            tab->pageEdit()->setText(QStringLiteral("5"));
            QTest::keyClick(tab->pageEdit(), Qt::Key_Return);
            QCOMPARE(tab->view()->currentPage(), 4);
            tab->view()->setZoom(1.5);
            tab->view()->setRotation(180);
            // Ouvrir le même document réactive l'onglet existant
            reader.openDocument(id);
            QCOMPARE(reader.tabCount(), 1);
            // Les miniatures se remplissent
            QCOMPARE(tab->thumbnails()->model()->rowCount(), 6);
        }  // fermeture : position enregistrée
        const auto pos = env.library->readingPosition(id);
        QVERIFY(pos.has_value());
        QCOMPARE(pos->locator.value(QStringLiteral("type")).toString(), QStringLiteral("pdf"));
        QVERIFY(qAbs(pos->progress - 5.0 / 6.0) < 0.01);

        // Nouvelle session : l'onglet est rouvert à la même page, même zoom et rotation
        ui::ReaderPage reader(*env.ctx);
        reader.resize(1200, 800);
        reader.show();
        QVERIFY(QTest::qWaitForWindowExposed(&reader));
        QCOMPARE(reader.tabCount(), 1);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader.currentTab());
        QTRY_VERIFY(tab->isLoaded());
        QCOMPARE(tab->view()->currentPage(), 4);
        QCOMPARE(tab->view()->rotation(), 180);
        QVERIFY(qAbs(tab->view()->zoom() - 1.5) < 0.01);
        QCOMPARE(tab->zoomCombo()->currentText(), QStringLiteral("150 %"));

        // Recherche depuis la barre
        tab->showSearch();
        tab->searchEdit()->setText(QStringLiteral("entropie"));
        QTest::keyClick(tab->searchEdit(), Qt::Key_Return);
        QTRY_VERIFY(tab->searchCountLabel()->text().contains(QStringLiteral("sur 6")));

        reader.closeTab(0);
        QCOMPARE(reader.tabCount(), 0);
        QCOMPARE(env.settings->value(QStringLiteral("reader/openTabs")).toArray().size(), 0);
    }

    void missingFileShowsMessage()
    {
        Env env;
        QTemporaryDir tmp;
        const QString copy = tmp.filePath(QStringLiteral("lie.pdf"));
        QVERIFY(QFile::copy(m_pdf, copy));
        QSignalSpy finished(env.library.get(), &LibraryService::importFinished);
        env.library->importPaths({copy}, StorageMode::Linked);
        QVERIFY(finished.wait(30000));
        const qint64 id = finished.first().at(0).value<ImportReport>().importedIds.first();
        QVERIFY(QFile::remove(copy));
        ui::PdfReaderTab tab(*env.ctx, id);
        const auto r = tab.ensureLoaded();
        QVERIFY(!r.ok());
        QVERIFY(r.error().message.contains(QStringLiteral("introuvable")));
    }

    void opensLargePdfQuickly()
    {
        // Critère du MVP : un PDF de 1 500 pages s'ouvre en moins d'une seconde.
        QTemporaryDir tmp;
        const QString big = tmp.filePath(QStringLiteral("gros.pdf"));
        QVERIFY(writePdf(big, {QStringLiteral("Gros document"), 1500, QStringLiteral("Texte")}));
        Env env;
        const qint64 id = env.import(big);
        QVERIFY(id > 0);
        ui::PdfReaderTab tab(*env.ctx, id);
        tab.resize(1200, 800);
        QElapsedTimer t;
        t.start();
        QVERIFY(tab.ensureLoaded().ok());
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        QCOMPARE(tab.view()->pageCount(), 1500);
        QTRY_VERIFY(tab.view()->viewport()->isVisible());
        const qint64 openMs = t.elapsed();
        qInfo() << "Ouverture d'un PDF de 1 500 pages :" << openMs << "ms";
        QVERIFY2(openMs < 1000, "ouverture > 1 s");
        // Saut au milieu du document
        tab.view()->goToPage(1200);
        QCOMPARE(tab.view()->currentPage(), 1200);
    }

    /// PDF annoté « ailleurs » : un surlignage jaune commenté et une note, sur la première page.
    QString writeForeignAnnotatedPdf(const QString& name)
    {
        const QString plain = m_files.filePath(name + QStringLiteral("-brut.pdf"));
        if (!writePdf(plain, {QStringLiteral("Mécanique des fluides"), 3,
                              QStringLiteral("Le nombre de Reynolds compare l'inertie et la viscosité.")}))
            return {};
        QFile in(plain);
        if (!in.open(QIODevice::ReadOnly))
            return {};
        pdf::PdfAnnotationOut hl;
        hl.page = 0;
        hl.type = AnnotationType::Highlight;
        hl.fill = QColor(255, 236, 64);
        hl.stroke = QColor(230, 190, 0);
        hl.rects = {QRectF(0.11, 0.150, 0.70, 0.026)};
        hl.contents = QStringLiteral("Définition à connaître");
        pdf::PdfAnnotationOut note;
        note.page = 0;
        note.type = AnnotationType::Note;
        note.fill = QColor(120, 170, 255);
        note.stroke = QColor(60, 120, 230);
        note.rects = {QRectF(0.8, 0.2, 0.01, 0.01)};
        note.contents = QStringLiteral("Revoir l'exemple du tuyau");
        auto written = pdf::writeAnnotatedCopy(in.readAll(), {hl, note});
        if (!written)
            return {};
        const QString path = m_files.filePath(name + QStringLiteral(".pdf"));
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return {};
        out.write(written.value().data);
        return path;
    }

    void importsAndExportsFileAnnotations()
    {
        Env env;
        const QString file = writeForeignAnnotatedPdf(QStringLiteral("fluides"));
        QVERIFY(!file.isEmpty());
        const qint64 id = env.import(file);
        QVERIFY(id > 0);
        {
            ui::ReaderPage reader(*env.ctx);
            reader.resize(1200, 800);
            reader.show();
            QVERIFY(QTest::qWaitForWindowExposed(&reader));
            reader.openDocument(id);
            auto* tab = qobject_cast<ui::PdfReaderTab*>(reader.currentTab());
            QTRY_VERIFY(tab->isLoaded());
            // Le bandeau propose l'import ; « Ignorer » le fait disparaître pour ce document.
            QTRY_VERIFY(tab->fileBanner()->isVisible());
            QVERIFY(tab->fileBannerText()->text().contains(QStringLiteral("2 annotations")));
            QVERIFY(tab->fileBannerImportButton()->isVisible());
            QVERIFY(tab->fileAnnotationsVisible());
            tab->fileBannerIgnoreButton()->click();
            QVERIFY(!tab->fileBanner()->isVisible());
            // Les actions du menu existent et sont actives.
            QStringList actions;
            for (QAction* a : tab->moreMenu()->actions())
                if (!a->isSeparator())
                    actions << a->text();
            QCOMPARE(actions.size(), 3);
            QVERIFY(actions[0].contains(QStringLiteral("Importer")));
            QVERIFY(actions[1].contains(QStringLiteral("copie annotée")));
        }
        {
            ui::ReaderPage reader(*env.ctx);
            reader.resize(1200, 800);
            reader.show();
            QVERIFY(QTest::qWaitForWindowExposed(&reader));
            reader.openDocument(id);
            auto* tab = qobject_cast<ui::PdfReaderTab*>(reader.currentTab());
            QSignalSpy scanned(tab, &ui::PdfReaderTab::fileAnnotationsScanned);
            QTRY_VERIFY(tab->isLoaded());
            QTRY_COMPARE(scanned.size(), 1);
            QCOMPARE(scanned.first().first().toInt(), 2);
            QVERIFY(!tab->fileBanner()->isVisible());  // ignoré : plus proposé

            // Import par le menu
            auto imported = tab->importFileAnnotations();
            QVERIFY(imported.ok());
            QCOMPARE(imported.value().imported, 2);
            QVERIFY(imported.value().fileFullyImported);
            QVERIFY(tab->fileBanner()->isVisible());
            QVERIFY(tab->fileBannerText()->text().contains(QStringLiteral("2 annotations importées")));
            QVERIFY(!tab->fileAnnotationsVisible());  // plus dessinées en double
            auto list = env.annotations->list(id);
            QVERIFY(list.ok());
            QCOMPARE(list.value().size(), 2);
            bool sawNote = false;
            for (const auto& r : list.value()) {
                if (r.type == AnnotationType::Highlight) {
                    QCOMPARE(r.color, AnnotationColor::Yellow);
                    QCOMPARE(r.note, QStringLiteral("Définition à connaître"));
                    QVERIFY2(r.selectedText.contains(QStringLiteral("Reynolds")), qPrintable(r.selectedText));
                } else {
                    QCOMPARE(r.type, AnnotationType::Note);
                    QCOMPARE(r.color, AnnotationColor::Blue);
                    sawNote = true;
                }
            }
            QVERIFY(sawNote);
            // Rien de nouveau au second import.
            QCOMPARE(tab->importFileAnnotations().value().imported, 0);

            // Nouvelle annotation StudyLab, puis copie annotée.
            QVERIFY(tab->addArea(1, QRectF(0.1, 0.3, 0.5, 0.2)).ok());
            const QString copy = m_files.filePath(QStringLiteral("fluides (annoté).pdf"));
            auto exported = tab->exportAnnotatedCopy(copy);
            QVERIFY2(exported.ok(), qPrintable(exported.ok() ? QString() : exported.error().message));
            QCOMPARE(exported.value().annotations, 1);
            QCOMPARE(exported.value().alreadyPresent, 2);
            QVERIFY(tab->fileBannerText()->text().contains(QStringLiteral("Copie annotée enregistrée")));
            auto scan = PdfInteropService::readFile(copy);
            QVERIFY(scan.ok());
            QCOMPARE(scan.value().annotations.size(), 3);
            QCOMPARE(scan.value().annotations.last().page, 1);
            QPdfDocument check;
            QCOMPARE(check.load(copy), QPdfDocument::Error::None);
            QCOMPARE(check.pageCount(), 3);
            // Le fichier de la bibliothèque n'a pas changé.
            QVERIFY(!tab->exportAnnotatedCopy(env.library->documentFile(env.library->document(id).value())).ok());

            // Afficher de nouveau les annotations du fichier : réglage mémorisé.
            tab->setFileAnnotationsVisible(true);
            QVERIFY(tab->fileAnnotationsVisible());
            tab->setFileAnnotationsVisible(false);
        }
        // Réouverture : plus rien à importer, annotations du fichier toujours masquées.
        ui::ReaderPage reader(*env.ctx);
        reader.resize(1200, 800);
        reader.show();
        QVERIFY(QTest::qWaitForWindowExposed(&reader));
        reader.openDocument(id);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader.currentTab());
        QSignalSpy scanned(tab, &ui::PdfReaderTab::fileAnnotationsScanned);
        QTRY_VERIFY(tab->isLoaded());
        QTRY_COMPARE(scanned.size(), 1);
        QCOMPARE(scanned.first().first().toInt(), 0);
        QVERIFY(!tab->fileAnnotationsVisible());
        QVERIFY(!tab->fileBanner()->isVisible());
    }

    void screenshots()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Captures non demandées (STUDYLAB_SCREENSHOT_DIR)");
        Env env;
        const qint64 id = env.import(m_pdf);
        env.settings->setValue(QStringLiteral("ui/lastPage"), QStringLiteral("reader"));
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        reader->openDocument(id);
        QTRY_VERIFY(reader->currentTab()->isLoaded());
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->currentTab());
        tab->view()->selectText(0, QPointF(20, 40), 0, QPointF(300, 70));
        maybeScreenshot(window, QStringLiteral("iteration3-lecteur-pdf"));
        env.settings->setThemeMode(ThemeMode::Dark);
        tab->showSearch();
        tab->searchEdit()->setText(QStringLiteral("entropie"));
        QTest::keyClick(tab->searchEdit(), Qt::Key_Return);
        maybeScreenshot(window, QStringLiteral("iteration3-lecteur-pdf-sombre"));

        // V2-5 : PDF annoté ailleurs, bandeau d'import puis annotations importées.
        env.settings->setThemeMode(ThemeMode::Light);
        const qint64 annotated = env.import(writeForeignAnnotatedPdf(QStringLiteral("capture")));
        reader->openDocument(annotated);
        auto* other = qobject_cast<ui::PdfReaderTab*>(reader->currentTab());
        QTRY_VERIFY(other->isLoaded());
        QTRY_VERIFY(other->fileBanner()->isVisible());
        maybeScreenshot(window, QStringLiteral("v2-5-annotations-du-pdf"));
        QVERIFY(other->importFileAnnotations().ok());
        maybeScreenshot(window, QStringLiteral("v2-5-annotations-importees"));
        // Copie annotée ouverte par le moteur PDF lui-même (ce que montre un autre lecteur).
        QVERIFY(other->addArea(0, QRectF(0.11, 0.2, 0.5, 0.12)).ok());
        const QString copy = m_files.filePath(QStringLiteral("capture (annoté).pdf"));
        QVERIFY(other->exportAnnotatedCopy(copy).ok());
        maybeScreenshot(window, QStringLiteral("v2-5-copie-annotee-enregistree"));
        QPdfDocument rendered;
        QCOMPARE(rendered.load(copy), QPdfDocument::Error::None);
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QImage page = rendered.render(0, (rendered.pagePointSize(0) * 2).toSize(), options);
        QImage paper(page.size(), QImage::Format_RGB32);
        paper.fill(Qt::white);
        QPainter(&paper).drawImage(0, 0, page);
        paper.save(qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR") + QStringLiteral("/v2-5-copie-annotee-page.png"));
    }
};

QTEST_MAIN(TstPdfReader)
#include "tst_pdf_reader.moc"
