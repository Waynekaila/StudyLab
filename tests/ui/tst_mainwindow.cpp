#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Navigation.h"
#include "ui/shell/SearchPalette.h"
#include "ui/shell/Shortcuts.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QListWidget>
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
        NoteService notes(database, paths.attachmentsDir());
        OrganizeService organize(database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups, notes, organize};
        ui::MainWindow window(ctx);
        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // Accueil par défaut, avec le vrai nombre de documents
        QCOMPARE(window.currentPage(), QStringLiteral("home"));
        auto* home = qobject_cast<ui::HomePage*>(window.page(QStringLiteral("home")));
        QVERIFY(home);
        QVERIFY2(home->subtitle()->text().contains(QStringLiteral("0 document")), qPrintable(home->subtitle()->text()));
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

    void navigationAndShortcutsConfigurable()
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
        LibraryService library(database, paths);
        AnnotationService annotations(database);
        CommentService comments(database);
        SearchService search(database);
        BackupService backups(paths, database);
        NoteService notes(database, paths.attachmentsDir());
        OrganizeService organize(database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups, notes, organize};

        // Disposition enregistrée par une version antérieure : identifiant inconnu ignoré, entrées absentes
        // replacées après celle qui les précède par défaut.
        settings.setValue(ui::Navigation::kSettingKey,
                          QJsonObject{{QStringLiteral("order"), QJsonArray{QStringLiteral("settings"), QStringLiteral("home"),
                                                                          QStringLiteral("disparu")}},
                                      {QStringLiteral("hidden"), QJsonArray{QStringLiteral("settings"), QStringLiteral("graph")}}});
        const ui::Navigation::Layout old = ui::Navigation::layout(settings);
        QCOMPARE(old.order.mid(0, 4), (QStringList{QStringLiteral("settings"), QStringLiteral("home"),
                                                   QStringLiteral("library"), QStringLiteral("reader")}));
        QCOMPARE(old.order.size(), ui::Navigation::entries().size());
        QCOMPARE(old.hidden, (QSet<QString>{QStringLiteral("graph")}));  // les Paramètres ne se masquent pas
        ui::Navigation::reset(settings);
        QVERIFY(ui::Navigation::isDefault(settings));

        ui::MainWindow window(ctx);
        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* sidebar = window.findChild<ui::Sidebar*>();
        QCOMPARE(sidebar->entryIds().size(), 11);
        auto* page = qobject_cast<ui::SettingsPage*>(window.page(QStringLiteral("settings")));
        page->categories()->setCurrentRow(3);  // Navigation et raccourcis
        QTest::mouseClick(sidebar->button(QStringLiteral("settings")), Qt::LeftButton);
        QVERIFY(!page->navigationResetButton()->isEnabled());

        // Masquer le Graphe, remonter les Révisions en tête : la barre suit aussitôt.
        QListWidget* list = page->navigationList();
        QCOMPARE(list->count(), 11);
        const auto row = [list](const QString& id) {
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toString() == id)
                    return i;
            return -1;
        };
        QVERIFY(!(list->item(row(QStringLiteral("settings")))->flags() & Qt::ItemIsUserCheckable));
        list->item(row(QStringLiteral("graph")))->setCheckState(Qt::Unchecked);
        QVERIFY(!sidebar->entryIds().contains(QStringLiteral("graph")));
        QVERIFY(!sidebar->button(QStringLiteral("graph")));
        list->setCurrentRow(row(QStringLiteral("revisions")));
        auto* up = page->findChildren<QPushButton*>().at(0);
        for (QPushButton* b : page->findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Monter"))
                up = b;
        for (int i = 0; i < 7; ++i)
            up->click();
        QCOMPARE(sidebar->entryIds().first(), QStringLiteral("revisions"));
        QCOMPARE(row(QStringLiteral("revisions")), 0);
        QVERIFY(!up->isEnabled());
        QVERIFY(page->navigationResetButton()->isEnabled());
        QCOMPARE(sidebar->current(), QStringLiteral("settings"));
        QVERIFY(sidebar->button(QStringLiteral("settings"))->isChecked());
        maybeScreenshot(window, QStringLiteral("v2-6-navigation-raccourcis"));

        // Un espace masqué reste accessible par son raccourci (Graphe : Ctrl+7).
        QTest::keyClick(&window, Qt::Key_7, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), QStringLiteral("graph"));
        QTest::keyClick(&window, Qt::Key_Comma, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), QStringLiteral("settings"));

        // Raccourci modifié : appliqué tout de suite, l'ancien ne fait plus rien, l'infobulle suit.
        auto* palette = window.findChild<ui::SearchPalette*>();
        QVERIFY(palette);
        page->shortcutEdit(QStringLiteral("app.search"))->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        page->commitShortcut(QStringLiteral("app.search"));
        QCOMPARE(settings.value(QStringLiteral("shortcuts/app.search")).toString(), QStringLiteral("Ctrl+Shift+F"));
        QVERIFY(sidebar->searchButton()->toolTip().contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F).toString(QKeySequence::NativeText)));
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        QVERIFY(!palette->isVisible());
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(palette->isVisible());
        palette->hide();

        // Refus : déjà utilisé, touche seule, touche réservée ; le champ revient à la touche en vigueur.
        page->shortcutEdit(QStringLiteral("nav.home"))->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        page->commitShortcut(QStringLiteral("nav.home"));
        QVERIFY(page->shortcutStatus()->isVisibleTo(page));
        QVERIFY2(page->shortcutStatus()->text().contains(QStringLiteral("Rechercher partout")), qPrintable(page->shortcutStatus()->text()));
        QCOMPARE(page->shortcutEdit(QStringLiteral("nav.home"))->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_1));
        QCOMPARE(ui::Shortcuts::key(settings, QStringLiteral("nav.home")), QKeySequence(Qt::CTRL | Qt::Key_1));
        QVERIFY(!ui::Shortcuts::validate(settings, QStringLiteral("reader.find"), QKeySequence(Qt::Key_K)).isEmpty());
        QVERIFY(!ui::Shortcuts::validate(settings, QStringLiteral("reader.find"), QKeySequence(QKeySequence::Copy)).isEmpty());
        QVERIFY(ui::Shortcuts::validate(settings, QStringLiteral("reader.find"), QKeySequence(Qt::Key_F3)).isEmpty());
        // Groupes séparés : Ctrl+S sert à la fois aux notes et (ici) à la recherche dans le lecteur.
        QVERIFY(ui::Shortcuts::validate(settings, QStringLiteral("reader.find"), QKeySequence(Qt::CTRL | Qt::Key_S)).isEmpty());
        // Révisions : touche seule permise, mais pas en double dans la séance.
        QVERIFY(ui::Shortcuts::setKey(settings, QStringLiteral("revisions.reveal"), QKeySequence(Qt::Key_R)));
        QVERIFY(!ui::Shortcuts::setKey(settings, QStringLiteral("revisions.edit"), QKeySequence(Qt::Key_R)));
        auto* hints = window.page(QStringLiteral("revisions"))->findChild<QLabel*>(QStringLiteral("SessionKeyHints"));
        QVERIFY(hints);
        QVERIFY2(hints->text().startsWith(QStringLiteral("R : retourner")), qPrintable(hints->text()));

        // Retirer un raccourci (croix du champ) : enregistré vide, plus d'action.
        page->shortcutEdit(QStringLiteral("nav.home"))->clear();
        QVERIFY(ui::Shortcuts::key(settings, QStringLiteral("nav.home")).isEmpty());
        QCOMPARE(settings.value(QStringLiteral("shortcuts/nav.home")).toString(), QString());
        QTest::keyClick(&window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), QStringLiteral("settings"));

        // Tout rétablir, navigation par défaut.
        QVERIFY(page->shortcutsResetButton()->isEnabled());
        page->shortcutsResetButton()->click();
        QCOMPARE(ui::Shortcuts::key(settings, QStringLiteral("app.search")), QKeySequence(Qt::CTRL | Qt::Key_K));
        QCOMPARE(page->shortcutEdit(QStringLiteral("nav.home"))->keySequence(), QKeySequence(Qt::CTRL | Qt::Key_1));
        QVERIFY(!page->shortcutsResetButton()->isEnabled());
        QVERIFY(hints->text().startsWith(QKeySequence(Qt::Key_Space).toString(QKeySequence::NativeText) + QStringLiteral(" : retourner")));
        page->navigationResetButton()->click();
        QVERIFY(ui::Navigation::isDefault(settings));
        QCOMPARE(sidebar->entryIds().first(), QStringLiteral("home"));
        QVERIFY(sidebar->button(QStringLiteral("graph")));
        QTest::keyClick(&window, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), QStringLiteral("home"));
        QVERIFY(sidebar->button(QStringLiteral("home"))->isChecked());
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
        NoteService notes(database, paths.attachmentsDir());
        OrganizeService organize(database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups, notes, organize};
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
        NoteService notes(database, paths.attachmentsDir());
        OrganizeService organize(database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups, notes, organize};
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
