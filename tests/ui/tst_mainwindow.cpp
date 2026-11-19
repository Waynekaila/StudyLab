#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/search/SearchService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;

/// Démarrage complet (base + migrations + thème + fenêtre) dans un dossier temporaire.
class TestMainWindow : public QObject {
    Q_OBJECT
private:
    // Captures d'écran facultatives : STUDYLAB_SCREENSHOT_DIR=/chemin
    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(300);  // fin du fondu enchaîné
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    void startupCreatesAndMigratesDatabase()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        auto r = openAndMigrate(paths);
        QVERIFY2(r.ok(), qPrintable(r ? QString() : r.error().message));
        QVERIFY(r.value().schemaVersion >= 1);
        QVERIFY(!r.value().previousRunCrashed);
        QVERIFY(QFile::exists(paths.databaseFile()));
        QVERIFY(QDir(paths.backupsDir()).exists());
    }

    void crashMarkerTriggersIntegrityCheck()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        { auto first = openAndMigrate(paths); QVERIFY(first.ok()); }
        markRunning(paths);  // simule une fermeture brutale (marqueur jamais supprimé)
        auto second = openAndMigrate(paths);
        QVERIFY(second.ok());
        QVERIFY(second.value().previousRunCrashed);
        QVERIFY(second.value().integrityProblems.isEmpty());
        markStopped(paths);
        QVERIFY(!QFile::exists(paths.runningMarkerFile()));
    }

    void navigationThemeAndSettings()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        auto startup = openAndMigrate(paths);
        QVERIFY(startup.ok());
        db::Database& database = *startup.value().db;
        SettingsService settings(database);
        settings.setThemeMode(ThemeMode::Light);
        ui::ThemeManager theme(settings);
        theme.apply();
        QVERIFY(!theme.isDark());

        LibraryService library(database, paths);
        AnnotationService annotations(database);
        CommentService comments(database);
        SearchService search(database);
        BackupService backups(paths, database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups};
        ui::MainWindow window(ctx);
        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // Accueil par défaut, avec le vrai nombre de documents
        QCOMPARE(window.currentPage(), QStringLiteral("home"));
        auto* home = qobject_cast<ui::HomePage*>(window.page(QStringLiteral("home")));
        QVERIFY(home);
        QCOMPARE(home->documentCountLabel()->text(), QStringLiteral("0"));
        maybeScreenshot(window, QStringLiteral("accueil-clair"));

        // Navigation par la barre latérale
        auto* sidebar = window.findChild<ui::Sidebar*>();
        QVERIFY(sidebar);
        QTest::mouseClick(sidebar->button(QStringLiteral("settings")), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("settings"));
        QCOMPARE(settings.value(QStringLiteral("ui/lastPage")).toString(), QStringLiteral("settings"));
        maybeScreenshot(window, QStringLiteral("parametres-clair"));

        // Thème sombre depuis l'écran Paramètres
        auto* page = qobject_cast<ui::SettingsPage*>(window.page(QStringLiteral("settings")));
        QVERIFY(page);
        auto* darkButton = page->themeControl()->findChildren<QPushButton*>().at(2);
        QTest::mouseClick(darkButton, Qt::LeftButton);
        QCOMPARE(settings.themeMode(), ThemeMode::Dark);
        QVERIFY(theme.isDark());
        QCOMPARE(QApplication::palette().color(QPalette::Window), ui::Palette::darkPalette().bg);
        maybeScreenshot(window, QStringLiteral("parametres-sombre"));

        // Échelle de l'interface
        page->scaleCombo()->setCurrentIndex(3);
        emit page->scaleCombo()->activated(3);
        QCOMPARE(settings.uiScale(), 1.25);
        QCOMPARE(theme.px(100), 125);

        // Animations
        QVERIFY(page->animationsSwitch()->isChecked());
        QTest::mouseClick(page->animationsSwitch(), Qt::LeftButton);
        QVERIFY(!settings.animationsEnabled());

        // Retour à l'accueil en sombre
        settings.setUiScale(1.0);
        QTest::mouseClick(sidebar->button(QStringLiteral("home")), Qt::LeftButton);
        QCOMPARE(window.currentPage(), QStringLiteral("home"));
        maybeScreenshot(window, QStringLiteral("accueil-sombre"));
    }

    void lastPageRestored()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        auto startup = openAndMigrate(paths);
        db::Database& database = *startup.value().db;
        SettingsService settings(database);
        settings.setValue(QStringLiteral("ui/lastPage"), QStringLiteral("settings"));
        ui::ThemeManager theme(settings);
        theme.apply();
        LibraryService library(database, paths);
        AnnotationService annotations(database);
        CommentService comments(database);
        SearchService search(database);
        BackupService backups(paths, database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups};
        ui::MainWindow window(ctx);
        QCOMPARE(window.currentPage(), QStringLiteral("settings"));
    }

    void manualBackupFromSettings()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        auto startup = openAndMigrate(paths);
        db::Database& database = *startup.value().db;
        SettingsService settings(database);
        ui::ThemeManager theme(settings);
        theme.apply();
        LibraryService library(database, paths);
        AnnotationService annotations(database);
        CommentService comments(database);
        SearchService search(database);
        BackupService backups(paths, database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups};
        ui::SettingsPage page(ctx);
        QPushButton* backup = nullptr;
        for (auto* b : page.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("Créer une sauvegarde"))
                backup = b;
        }
        QVERIFY(backup);
        backup->click();
        // Sauvegarde quotidienne du démarrage + sauvegarde manuelle
        const QStringList files = QDir(paths.backupsDir()).entryList({QStringLiteral("studylab-manuelle-*.db")});
        QCOMPARE(files.size(), 1);
        QCOMPARE(QDir(paths.backupsDir()).entryList({QStringLiteral("studylab-auto-*.db")}).size(), 1);
        auto copy = db::Database::open(paths.backupsDir() + QLatin1Char('/') + files.first(), db::Database::Mode::ReadOnly);
        QVERIFY(copy.ok());
        QVERIFY(copy.value()->queryInt(QStringLiteral("SELECT max(version) FROM schema_migrations")).value() >= 1);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
