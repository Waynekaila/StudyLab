#include "support/WebEngineSetup.h"

#include "Bootstrap.h"
#include "data/sqlite/Database.h"
#include "services/activity/ActivityService.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "services/study/StudyService.h"
#include "support/Fixtures.h"
#include "ui/components/Charts.h"
#include "ui/components/SegmentedControl.h"
#include "ui/pages/ActivityPage.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/reader/ReadingClock.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;
using namespace studylab::test;

/// Tableau de bord et écran Activité (V2-4) : temps de lecture mesuré dans le lecteur, objectif de la semaine,
/// révisions du jour par matière, progression par matière, favoris, notes récentes, journal filtrable.
class TstActivityUi : public QObject {
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

        explicit Env(bool dark = false)
        {
            auto startup = openAndMigrate(paths);
            db = std::move(startup.value().db);
            settings = std::make_unique<SettingsService>(*db);
            settings->setThemeMode(dark ? ThemeMode::Dark : ThemeMode::Light);
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

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(900);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }


    static ui::PdfReaderTab* openPdf(ui::MainWindow& window, qint64 id)
    {
        window.navigateTo(QStringLiteral("reader"));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        reader->openDocument(id);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->tabFor(id));
        QDeadlineTimer deadline(30000);
        while (tab && !tab->isLoaded() && !deadline.hasExpired())
            QTest::qWait(20);
        return tab;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(kWebEngineReady);
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("cengel.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Thermodynamique Çengel"), 6}));
    }

    void readingTimeAndDashboard()
    {
        Env env;
        const qint64 doc = env.import(m_pdf);
        QVERIFY(doc > 0);
        const qint64 subject = env.organize->createSubject(QStringLiteral("Thermodynamique"), 0, QStringLiteral("blue")).value().id;
        QVERIFY(env.organize->attach(subject, OrganizeService::Entity::Document, doc));
        QVERIFY(env.library->setFavorite(doc, true));
        const auto note = env.notes->create(QStringLiteral("Second principe"), NoteType::Concept, QString());
        QVERIFY(note);

        ui::MainWindow window(*env.ctx);
        window.resize(1440, 960);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(env.ctx->activity);  // créé par la fenêtre
        QVERIFY(window.sidebar()->button(QStringLiteral("activity")));

        // Lecture : une minute active et trois pages vues
        ui::PdfReaderTab* tab = openPdf(window, doc);
        QVERIFY(tab && tab->isLoaded());
        ui::ReadingClock* clock = tab->readingClock();
        QVERIFY(clock && clock->enabled());
        clock->setAssumeActive(true);
        clock->markInput();
        clock->tick(65000);
        tab->view()->goToPage(2);
        tab->view()->goToPage(3);
        QTRY_VERIFY(clock->pages().size() >= 3);
        clock->flush();
        QVERIFY(clock->sessionId() > 0);
        const auto today = env.ctx->activity->days(env.ctx->activity->today(), env.ctx->activity->today());
        QCOMPARE(today.value().first().seconds, 65);
        QCOMPARE(today.value().first().pages, 3);
        // Inactif plus de deux minutes : rien ne s'ajoute
        clock->setAssumeActive(false);
        clock->tick(5000);  // onglet ouvert mais fenêtre hors écran (offscreen) ou inactif
        QVERIFY(clock->seconds() <= 70);

        // Une carte à réviser dans la matière
        StudyService& study = *window.study();
        Deck deck = study.createDeck(QStringLiteral("Cycles")).value();
        deck.subjectId = subject;
        QVERIFY(study.updateDeck(deck));
        QVERIFY(study.addCards(deck.id, CardType::Basic, QStringLiteral("Rendement de Carnot ?"),
                               QStringLiteral("$1 - T_F/T_C$")));

        window.navigateTo(QStringLiteral("home"));
        auto* home = qobject_cast<ui::HomePage*>(window.page(QStringLiteral("home")));
        QVERIFY(home);
        home->refresh();
        QVERIFY2(home->subtitle()->text().contains(QStringLiteral("1 carte à réviser")), qPrintable(home->subtitle()->text()));
        QCOMPARE(home->resumeTitle()->text(), QStringLiteral("Thermodynamique Çengel"));
        QCOMPARE(home->dueLabel()->text(), QStringLiteral("1"));
        QCOMPARE(home->revisionRows().size(), 1);
        QVERIFY(home->revisionRows().first()->accessibleName().startsWith(QStringLiteral("Thermodynamique")));
        QVERIFY(home->startReviewButton()->isVisible());
        QCOMPARE(home->goalRing()->value(), 3);
        QCOMPARE(home->goalRing()->target(), 0);
        QVERIFY2(home->weekReading()->text().contains(QStringLiteral("1 min")), qPrintable(home->weekReading()->text()));
        QVERIFY(home->dailyGoals()->text().contains(QStringLiteral("Aucun objectif")));
        QCOMPARE(home->subjectRows().size(), 1);
        QVERIFY(home->subjectRows().first()->accessibleName().startsWith(QStringLiteral("Thermodynamique : ")));
        QCOMPARE(home->favoriteButtons().size(), 1);
        QCOMPARE(home->noteChips().size(), 1);
        QCOMPARE(home->noteChips().first()->text(), QStringLiteral("Second principe"));

        // Objectifs
        ui::GoalsDialog* goals = home->editGoals();
        QVERIFY(goals);
        QCOMPARE(goals->pagesBox()->value(), 0);
        goals->pagesBox()->setValue(10);
        goals->minutesBox()->setValue(20);
        QVERIFY(goals->save());
        goals->accept();
        QTRY_COMPARE(home->goalRing()->target(), 10);
        QCOMPARE(home->goalRing()->caption(), QStringLiteral("/ 10 p."));
        QVERIFY2(home->dailyGoals()->text().contains(QStringLiteral("Lecture aujourd'hui : 1 / 20 min")),
                 qPrintable(home->dailyGoals()->text()));
        maybeScreenshot(window, QStringLiteral("v2-4-accueil"));

        // Les liens du tableau de bord
        QTest::mouseClick(home->noteChips().first(), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        window.navigateTo(QStringLiteral("home"));
        QTest::mouseClick(home->favoriteButtons().first(), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        window.navigateTo(QStringLiteral("home"));
        QTest::mouseClick(home->revisionRows().first(), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("revisions"));
    }

    void activityScreen()
    {
        Env env;
        const qint64 doc = env.import(m_pdf);
        QVERIFY(doc > 0);
        ActivityService activity(*env.db);
        env.ctx->activity = &activity;
        db::ReadingSessionRow s;
        s.documentId = doc;
        s.seconds = 25 * 60;
        s.pages = {0, 1, 2, 3};
        QVERIFY(activity.saveSession(s));
        AnnotationRecord a;
        a.documentId = doc;
        a.type = AnnotationType::Highlight;
        a.locator = QJsonObject{{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("page"), 1}};
        a.unitIndex = 1;
        a.selectedText = QStringLiteral("entropie");
        const auto ann = env.annotations->create(a);
        QVERIFY(ann);
        QVERIFY(env.notes->create(QStringLiteral("Cycle de Rankine"), NoteType::Course, QString()));

        {
            ui::MainWindow window(*env.ctx);
            window.resize(1440, 960);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.navigateTo(QStringLiteral("activity"));
            auto* page = qobject_cast<ui::ActivityPage*>(window.page(QStringLiteral("activity")));
            QVERIFY(page);
            QCOMPARE(page->period(), ui::ActivityPage::Period::Week);
            QCOMPARE(page->tileValue(0)->text(), QStringLiteral("25 min"));
            QCOMPARE(page->tileCaption(0)->text(), QStringLiteral("1 jour actif"));
            QCOMPARE(page->tileValue(1)->text(), QStringLiteral("4"));
            QCOMPARE(page->tileValue(2)->text(), QStringLiteral("1"));
            QCOMPARE(page->tileValue(4)->text(), QStringLiteral("1"));
            QCOMPARE(page->chart()->values().size(), 7);
            QCOMPARE(page->chart()->values().last(), 25);
            QVERIFY(page->heatmap()->levels().size() > 150);
            QVERIFY(page->heatmap()->levels().last() > 0);
            QCOMPARE(page->streakLabel()->text(), QStringLiteral("Série en cours : 1 jour"));
            QCOMPARE(page->dayHeaders().size(), 1);
            QCOMPARE(page->dayHeaders().first()->text(), QStringLiteral("AUJOURD'HUI"));
            // Lecture, annotation, note, import (l'ouverture n'a pas eu lieu)
            QStringList names;
            for (QPushButton* r : page->journalRows())
                names << r->accessibleName();
            QVERIFY2(names.join(QLatin1Char('|')).contains(QStringLiteral("Lu « Thermodynamique Çengel »")), qPrintable(names.join(QLatin1Char('|'))));
            QCOMPARE(page->journalRows().size(), 4);
            maybeScreenshot(window, QStringLiteral("v2-4-activite"));

            page->setFilter(ui::ActivityPage::Filter::Notes);
            QCOMPARE(page->journalRows().size(), 1);
            QVERIFY(page->journalRows().first()->accessibleName().startsWith(QStringLiteral("Note créée : « Cycle de Rankine »")));
            QTest::mouseClick(page->journalRows().first(), Qt::LeftButton);
            QCOMPARE(window.currentPage(), QStringLiteral("notes"));

            window.navigateTo(QStringLiteral("activity"));
            page->setFilter(ui::ActivityPage::Filter::Annotations);
            QCOMPARE(page->journalRows().size(), 1);
            QTest::mouseClick(page->journalRows().first(), Qt::LeftButton);
            QCOMPARE(window.currentPage(), QStringLiteral("reader"));

            window.navigateTo(QStringLiteral("activity"));
            page->setFilter(ui::ActivityPage::Filter::Reviews);
            QVERIFY(page->journalRows().isEmpty());
            QVERIFY(page->emptyLabel()->isVisible());
            page->setFilter(ui::ActivityPage::Filter::All);
            page->setPeriod(ui::ActivityPage::Period::Month);
            QCOMPARE(page->chart()->values().size(), 30);
            page->setPeriod(ui::ActivityPage::Period::Year);
            QCOMPARE(page->chart()->values().size(), 12);
            QCOMPARE(page->chart()->values().last(), 25);
            QCOMPARE(page->periodControl()->currentIndex(), 2);

            // Mesure désactivée : le lecteur ne compte plus rien
            auto* settings = qobject_cast<ui::SettingsPage*>(window.page(QStringLiteral("settings")));
            QVERIFY(settings && settings->trackReadingSwitch()->isChecked());
            settings->trackReadingSwitch()->setChecked(false);
            QCOMPARE(env.settings->value(SettingKeys::TrackReading, true).toBool(), false);
            ui::PdfReaderTab* tab = openPdf(window, doc);
            QVERIFY(tab);
            QVERIFY(!tab->readingClock()->enabled());
            tab->readingClock()->setAssumeActive(true);
            tab->readingClock()->tick(60000);
            QCOMPARE(tab->readingClock()->seconds(), 0);
        }
        env.ctx->activity = nullptr;
    }

    void activityDark()
    {
        if (qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR").isEmpty())
            QSKIP("Capture seulement");
        Env env(true);
        const qint64 doc = env.import(m_pdf);
        ActivityService activity(*env.db);
        env.ctx->activity = &activity;
        // Quelques semaines de lecture pour la capture
        for (int d = 40; d >= 0; --d) {
            if (d % 3 == 1)
                continue;
            activity.setClock([d] { return QDateTime::currentDateTimeUtc().addDays(-d); });
            db::ReadingSessionRow s;
            s.documentId = doc;
            s.seconds = (10 + (d * 7) % 45) * 60;
            s.pages = {d % 6, (d + 1) % 6};
            QVERIFY(activity.saveSession(s));
        }
        activity.setClock({});
        {
            ui::MainWindow window(*env.ctx);
            window.resize(1440, 960);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.navigateTo(QStringLiteral("activity"));
            auto* page = qobject_cast<ui::ActivityPage*>(window.page(QStringLiteral("activity")));
            page->setPeriod(ui::ActivityPage::Period::Month);
            maybeScreenshot(window, QStringLiteral("v2-4-activite-sombre"));
        }
        env.ctx->activity = nullptr;
    }
};

QTEST_MAIN(TstActivityUi)
#include "tst_activity_ui.moc"
