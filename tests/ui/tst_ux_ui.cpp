#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
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
#include "ui/components/PanelSplitter.h"
#include "ui/reader/ReaderPage.h"
#include "ui/reader/ReaderTab.h"
#include "ui/shell/Sidebar.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;
using namespace studylab::test;

/// Ergonomie : visite de tous les écrans sur un écran d'ordinateur portable courant (1366 × 768).
/// Avec STUDYLAB_SCREENSHOT_DIR, chaque écran est enregistré pour comparer avant et après.
class TstUxUi : public QObject {
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
        QList<qint64> import(const QStringList& files)
        {
            QSignalSpy finished(library.get(), &LibraryService::importFinished);
            library->importPaths(files);
            if (!finished.wait(60000))
                return {};
            return finished.first().at(0).value<ImportReport>().importedIds;
        }
    };

    QTemporaryDir m_src;
    QStringList m_files;

    static void shot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(500);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_ux_ui"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings().clear();
        const QStringList titles = {QStringLiteral("Diagrammes de Bode"), QStringLiteral("Séries de Fourier"),
                                    QStringLiteral("Thermodynamique")};
        for (int i = 0; i < titles.size(); ++i) {
            const QString path = m_src.filePath(QStringLiteral("doc%1.pdf").arg(i));
            PdfSpec spec;
            spec.title = titles[i];
            spec.pages = 6;
            QVERIFY(writePdf(path, spec));
            m_files << path;
        }
    }

    void cleanupTestCase() { QSettings().clear(); }

    void tourAllScreens()
    {
        Env env;
        const auto ids = env.import(m_files);
        QCOMPARE(ids.size(), 3);
        env.notes->create(QStringLiteral("Fonction de transfert"), NoteType::Personal, {},
                          QStringLiteral("# Fonction de transfert\n\nVoir [[Diagrammes de Bode]]."));
        env.notes->create(QStringLiteral("Premier principe"), NoteType::Personal);
        auto subject = env.organize->createSubject(QStringLiteral("Automatique"));
        QVERIFY(subject.ok());
        env.organize->createCollection(QStringLiteral("Partiels"), CollectionKind::Manual);
        StudyService study(*env.db);
        auto deck = study.createDeck(QStringLiteral("Automatique"));
        QVERIFY(deck.ok());
        study.addCards(deck.value().id, CardType::Basic, QStringLiteral("Gain statique ?"), QStringLiteral("H(0)"));

        const QString prefix = qEnvironmentVariable("STUDYLAB_SCREENSHOT_PREFIX");
        ui::MainWindow window(*env.ctx);
        window.resize(1366, 740);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QStringList pages = {QStringLiteral("home"),     QStringLiteral("library"),     QStringLiteral("notes"),
                                   QStringLiteral("subjects"), QStringLiteral("collections"), QStringLiteral("graph"),
                                   QStringLiteral("revisions"), QStringLiteral("references"), QStringLiteral("activity"),
                                   QStringLiteral("settings")};
        for (const QString& id : pages) {
            window.navigateTo(id);
            QCOMPARE(window.currentPage(), id);
            shot(window, prefix + id);
        }
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        QVERIFY(reader);
        window.navigateTo(QStringLiteral("reader"));
        reader->openDocument(ids.first());
        QTest::qWait(800);
        shot(window, prefix + QStringLiteral("reader"));
    }
    /// Navigation réduite (F8), panneaux repliés (F9, F10) : retrouvés tels quels au lancement suivant.
    void panelsCollapseAndPersist()
    {
        Env env;
        env.notes->create(QStringLiteral("Fonction de transfert"), NoteType::Personal);
        const QString prefix = qEnvironmentVariable("STUDYLAB_SCREENSHOT_PREFIX");
        int navDefault = 0;
        {
            ui::MainWindow window(*env.ctx);
            window.resize(1366, 740);
            window.show();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            window.navigateTo(QStringLiteral("notes"));
            auto* panels = window.page(QStringLiteral("notes"))->findChild<ui::PanelSplitter*>();
            QVERIFY(panels);
            QWidget* nav = panels->leadingPanel();
            QWidget* context = panels->trailingPanel();
            QVERIFY(nav && context);
            QVERIFY(panels->isPanelOpen(nav));
            navDefault = panels->panelWidth(nav);
            QCOMPARE(nav->width(), navDefault);  // largeur par défaut appliquée, la zone de travail prend le reste

            QTest::keyClick(&window, Qt::Key_F9);
            QVERIFY(!panels->isPanelOpen(nav));
            QVERIFY(nav->isHidden());
            QVERIFY(panels->railButton(nav)->isVisible());  // chemin du retour toujours visible
            QTest::keyClick(&window, Qt::Key_F10);
            QVERIFY(!panels->isPanelOpen(context));

            auto* sidebar = window.sidebar();
            const int wide = sidebar->width();
            QTest::keyClick(&window, Qt::Key_F8);
            QVERIFY(sidebar->isCompact());
            QVERIFY(sidebar->width() < wide / 2);
            QVERIFY(sidebar->button(QStringLiteral("notes"))->text().isEmpty());
            QCOMPARE(sidebar->button(QStringLiteral("notes"))->toolTip(), QStringLiteral("Mes notes"));
            shot(window, prefix + QStringLiteral("notes-replie"));
        }
        {
            ui::MainWindow window(*env.ctx);
            window.resize(1366, 740);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(window.sidebar()->isCompact());
            window.navigateTo(QStringLiteral("notes"));
            auto* panels = window.page(QStringLiteral("notes"))->findChild<ui::PanelSplitter*>();
            QWidget* nav = panels->leadingPanel();
            QVERIFY(!panels->isPanelOpen(nav));
            QVERIFY(!panels->isPanelOpen(panels->trailingPanel()));
            // Rouvert par la bande : même largeur qu'avant.
            QTest::mouseClick(panels->railButton(nav), Qt::LeftButton);
            QVERIFY(panels->isPanelOpen(nav));
            QTRY_COMPARE(nav->width(), navDefault);
            QVERIFY(!panels->railButton(nav)->isVisible());
            // Le bouton du panneau le replie, et la navigation se déplie par son bouton.
            auto* collapse = nav->findChild<QToolButton*>(QStringLiteral("PanelCollapseButton"));
            QVERIFY(collapse);
            QTest::mouseClick(collapse, Qt::LeftButton);
            QVERIFY(!panels->isPanelOpen(nav));
            QTest::mouseClick(window.sidebar()->compactButton(), Qt::LeftButton);
            QVERIFY(!window.sidebar()->isCompact());
        }
    }

    /// Lecteur : volet de gauche (miniatures) masqué par F9, choix retenu pour le document suivant.
    void readerPanesRemembered()
    {
        Env env;
        const auto ids = env.import(m_files);
        QCOMPARE(ids.size(), 3);
        ui::MainWindow window(*env.ctx);
        window.resize(1366, 740);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        window.navigateTo(QStringLiteral("reader"));
        reader->openDocument(ids.at(0));
        QTRY_VERIFY(reader->currentTab());
        auto* panels = reader->currentTab()->findChild<ui::PanelSplitter*>();
        QVERIFY(panels);
        QTRY_VERIFY(panels->isVisible());
        QVERIFY(panels->isPanelOpen(panels->leadingPanel()));
        QTest::keyClick(&window, Qt::Key_F9);
        QVERIFY(!panels->isPanelOpen(panels->leadingPanel()));
        QTest::keyClick(&window, Qt::Key_F10);
        QVERIFY(!panels->isPanelOpen(panels->trailingPanel()));
        QVERIFY(!reader->currentTab()->paneToggle()->isChecked());

        reader->openDocument(ids.at(1));
        QTRY_VERIFY(reader->currentTab() && reader->currentTab()->documentId() == ids.at(1));
        auto* next = reader->currentTab()->findChild<ui::PanelSplitter*>();
        QVERIFY(!next->isPanelOpen(next->leadingPanel()));
        QVERIFY(!next->isPanelOpen(next->trailingPanel()));
    }
};

QTEST_MAIN(TstUxUi)
#include "tst_ux_ui.moc"
