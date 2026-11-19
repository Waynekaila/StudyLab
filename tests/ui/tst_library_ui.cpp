#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/components/SegmentedControl.h"
#include "ui/library/DocumentDetailPanel.h"
#include "ui/library/EditDocumentDialog.h"
#include "ui/library/LibraryModel.h"
#include "ui/library/LibraryPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>

using namespace studylab;
using namespace studylab::test;

/// Écran Bibliothèque de bout en bout : import réel, grille, filtres, détail, glisser-déposer.
class TstLibraryUi : public QObject {
    Q_OBJECT

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(300);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

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
    };

    static void waitImport(LibraryService& lib)
    {
        QSignalSpy finished(&lib, &LibraryService::importFinished);
        QVERIFY(finished.wait(30000));
    }

    // Documents sans fichier (métadonnées seules) pour remplir la grille comme dans les maquettes.
    static void seedCatalogue(db::Database& db)
    {
        db::DocumentRepository repo(db);
        const struct {
            const char* title;
            const char* author;
            ReadingStatus status;
            double progress;
            const char* tag;
        } items[] = {
            {"Mécanique des fluides", "Sébastien Candel", ReadingStatus::Reading, 0.35, "fluides"},
            {"Feedback Systems", "Karl Johan Åström", ReadingStatus::Reading, 0.4, "automatique"},
            {"Analyse numérique", "Alfio Quarteroni", ReadingStatus::Paused, 0.18, "maths"},
            {"Transferts thermiques", "Frank P. Incropera", ReadingStatus::ToRead, 0, "thermo"},
            {"Méthode des éléments finis", "Gouri Dhatt", ReadingStatus::ToReread, 0, "maths"},
            {"Signaux et systèmes", "Alan V. Oppenheim", ReadingStatus::Finished, 1.0, "automatique"},
            {"Conception mécanique", "Joseph E. Shigley", ReadingStatus::ToRead, 0, "meca"},
        };
        int i = 0;
        for (const auto& it : items) {
            DocumentRecord r;
            r.title = QString::fromUtf8(it.title);
            r.authors = {QString::fromUtf8(it.author)};
            r.status = it.status;
            r.tags = {QString::fromUtf8(it.tag)};
            r.fileHash = QStringLiteral("seed%1").arg(i);
            r.filePath = QStringLiteral("seed%1.pdf").arg(i);
            r.fileSize = 4'000'000 + i * 1'000'000;
            r.pageCount = 300 + i * 40;
            r.addedAt = QStringLiteral("2026-10-%1T10:00:00Z").arg(10 + i);
            QVERIFY(repo.insert(r).ok());
            if (it.progress > 0)
                QVERIFY(db.exec(QStringLiteral("INSERT INTO reading_progress(document_id, locator_json, progress, updated_at) "
                                               "VALUES (%1, '{}', %2, 'x')").arg(r.id).arg(it.progress))
                            .ok());
            ++i;
        }
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
    }

    void emptyLibraryShowsInvitation()
    {
        Env env;
        ui::LibraryPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QVERIFY(page.emptyState()->isVisible());
        QCOMPARE(page.model()->rowCount(), 0);
    }

    void importFilterSelectEdit()
    {
        Env env;
        QTemporaryDir src;
        QVERIFY(writePdf(src.filePath(QStringLiteral("thermo.pdf")), {QStringLiteral("Thermodynamique : une approche pragmatique"), 4}));
        EpubSpec e;
        e.title = QStringLiteral("Candide");
        e.authors = {QStringLiteral("Voltaire")};
        e.subjects = {QStringLiteral("philosophie")};
        QVERIFY(writeEpub(src.filePath(QStringLiteral("candide.epub")), e));

        ui::MainWindow window(*env.ctx);
        window.resize(1400, 860);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // La navigation propose la Bibliothèque
        auto* sidebar = window.findChild<ui::Sidebar*>();
        QVERIFY(sidebar->button(QStringLiteral("library")));
        QTest::mouseClick(sidebar->button(QStringLiteral("library")), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("library"));
        auto* page = qobject_cast<ui::LibraryPage*>(window.page(QStringLiteral("library")));
        QVERIFY(page);

        // Glisser-déposer d'un dossier sur la fenêtre → import (copie par défaut)
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(src.path())});
        QDragEnterEvent enter(QPoint(400, 300), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(400, 300), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QSignalSpy finished(env.library.get(), &LibraryService::importFinished);
        QCoreApplication::sendEvent(&window, &drop);
        QVERIFY(finished.wait(30000));
        QCOMPARE(page->model()->rowCount(), 2);
        QVERIFY(page->importStatusLabel()->text().contains(QStringLiteral("2 importés")));
        QVERIFY(!page->emptyState()->isVisible());
        const auto first = page->model()->recordAt(0);
        QCOMPARE(first->storageMode, StorageMode::Managed);

        // Recherche (accents ignorés) avec anti-rebond
        page->searchEdit()->setText(QStringLiteral("voltaire"));
        QTRY_COMPARE(page->model()->rowCount(), 1);
        QCOMPARE(page->model()->recordAt(0)->title, QStringLiteral("Candide"));
        page->searchEdit()->setText(QStringLiteral("philo"));  // tag issu de dc:subject
        QTRY_COMPARE(page->model()->rowCount(), 1);
        page->searchEdit()->clear();
        QTRY_COMPARE(page->model()->rowCount(), 2);

        // Sélection → panneau de détail
        const int row = page->model()->rowOf(page->model()->recordAt(0)->id);
        const QRect itemRect = page->view()->visualRect(page->model()->index(row));
        QTest::mouseClick(page->view()->viewport(), Qt::LeftButton, {}, itemRect.center());
        QTRY_VERIFY(page->detailPanel()->isVisible());
        const qint64 id = page->detailPanel()->documentId();
        QCOMPARE(id, page->model()->recordAt(row)->id);

        // Statut depuis le détail, puis filtre par statut
        auto* status = page->detailPanel()->statusCombo();
        status->setCurrentIndex(status->findData(int(ReadingStatus::Reading)));
        emit status->activated(status->currentIndex());
        QCOMPARE(env.library->document(id).value().status, ReadingStatus::Reading);
        page->statusFilter()->setCurrentIndex(page->statusFilter()->findData(int(ReadingStatus::Reading)));
        emit page->statusFilter()->activated(page->statusFilter()->currentIndex());
        QCOMPARE(page->model()->rowCount(), 1);
        page->statusFilter()->setCurrentIndex(0);
        emit page->statusFilter()->activated(0);

        // Favori
        QTest::mouseClick(page->detailPanel()->favoriteButton(), Qt::LeftButton);
        QVERIFY(env.library->document(id).value().favorite);
        page->favoritesFilter()->click();
        QCOMPARE(page->model()->rowCount(), 1);
        page->favoritesFilter()->click();
        QCOMPARE(page->model()->rowCount(), 2);

        // Bascule liste / grille mémorisée
        auto* listButton = page->viewToggle()->findChildren<QPushButton*>().at(1);
        QTest::mouseClick(listButton, Qt::LeftButton);
        QCOMPARE(page->view()->viewMode(), QListView::ListMode);
        QCOMPARE(env.settings->value(SettingKeys::LibraryView).toString(), QStringLiteral("list"));
        maybeScreenshot(window, QStringLiteral("bibliotheque-liste"));
    }

    void editDialogProducesCleanRecord()
    {
        DocumentRecord r;
        r.title = QStringLiteral("Ancien");
        ui::EditDocumentDialog dlg(r, {QStringLiteral("thermo")});
        dlg.titleEdit()->setText(QStringLiteral("  Thermodynamique  "));
        dlg.authorsEdit()->setText(QStringLiteral("Çengel, Yunus A.; Michael A. Boles ;"));
        dlg.tagsEdit()->setText(QStringLiteral("#entropie, cycles , ,exergie"));
        const DocumentRecord out = dlg.result();
        QCOMPARE(out.title, QStringLiteral("Thermodynamique"));
        QCOMPARE(out.authors, (QStringList{QStringLiteral("Çengel, Yunus A."), QStringLiteral("Michael A. Boles")}));
        QCOMPARE(out.tags, (QStringList{QStringLiteral("entropie"), QStringLiteral("cycles"), QStringLiteral("exergie")}));
    }

    void linkModeFromSettings()
    {
        Env env;
        ui::SettingsPage settingsPage(*env.ctx);
        QTest::mouseClick(settingsPage.importModeControl()->findChildren<QPushButton*>().at(1), Qt::LeftButton);
        QCOMPARE(env.settings->value(SettingKeys::ImportMode).toString(), QStringLiteral("link"));

        QTemporaryDir src;
        QVERIFY(writePdf(src.filePath(QStringLiteral("a.pdf")), {QStringLiteral("Lié"), 1}));
        ui::LibraryPage page(*env.ctx);
        QSignalSpy finished(env.library.get(), &LibraryService::importFinished);
        page.importPaths({src.filePath(QStringLiteral("a.pdf"))});
        QVERIFY(finished.wait(30000));
        QCOMPARE(env.library->documents().value().first().storageMode, StorageMode::Linked);
    }

    void screenshotsGrid()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Captures non demandées (STUDYLAB_SCREENSHOT_DIR)");
        Env env;
        seedCatalogue(*env.db);
        QTemporaryDir src;
        QVERIFY(writePdf(src.filePath(QStringLiteral("thermo.pdf")), {QStringLiteral("Thermodynamique"), 3}));
        EpubSpec e;
        e.title = QStringLiteral("Candide ou l'Optimisme");
        e.authors = {QStringLiteral("Voltaire")};
        QVERIFY(writeEpub(src.filePath(QStringLiteral("candide.epub")), e));
        env.library->importPaths({src.path()});
        waitImport(*env.library);

        env.settings->setValue(QStringLiteral("ui/lastPage"), QStringLiteral("library"));
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* page = qobject_cast<ui::LibraryPage*>(window.page(QStringLiteral("library")));
        page->selectDocument(page->model()->recordAt(0)->id);
        maybeScreenshot(window, QStringLiteral("bibliotheque-grille"));
        env.settings->setThemeMode(ThemeMode::Dark);
        maybeScreenshot(window, QStringLiteral("bibliotheque-grille-sombre"));
    }
};

QTEST_MAIN(TstLibraryUi)
#include "tst_library_ui.moc"
