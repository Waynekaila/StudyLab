#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "ui/library/DocumentDetailPanel.h"
#include "ui/library/LibraryPage.h"
#include "ui/notes/NotesPage.h"
#include "ui/organize/CollectionsPage.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/organize/SmartCollectionDialog.h"
#include "ui/organize/SubjectsPage.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QPainter>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

using namespace studylab;

/// V1, itération 3 : matières hiérarchiques, collections manuelles et intelligentes.
class TstOrganizeUi : public QObject {
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

        qint64 document(const QString& title, const QString& author, ReadingStatus status, int year = 2019)
        {
            db::DocumentRepository repo(*db);
            DocumentRecord d;
            d.title = title;
            d.authors = {author};
            d.year = year;
            d.fileHash = title;
            d.filePath = title + QStringLiteral(".pdf");
            d.status = status;
            if (!repo.insert(d))
                return 0;
            return d.id;
        }

        /// Matières et collections de la maquette 06.
        struct Demo {
            qint64 physique = 0, thermo = 0, fluides = 0, maths = 0;
            qint64 cengel = 0, poly = 0, td = 0;
            qint64 principe = 0, formules = 0, rankine = 0;
        };
        Demo demo()
        {
            Demo d;
            d.maths = organize->createSubject(QStringLiteral("Mathématiques"), 0, QStringLiteral("yellow")).value().id;
            d.physique = organize->createSubject(QStringLiteral("Physique"), 0, QStringLiteral("slate")).value().id;
            d.thermo = organize->createSubject(QStringLiteral("Thermodynamique"), d.physique, QStringLiteral("blue")).value().id;
            d.fluides = organize->createSubject(QStringLiteral("Mécanique des fluides"), d.physique, QStringLiteral("green")).value().id;
            organize->createSubject(QStringLiteral("Résistance des matériaux"), 0, QStringLiteral("pink")).value();
            organize->createSubject(QStringLiteral("Automatique"), 0, QStringLiteral("purple")).value();
            d.cengel = document(QStringLiteral("Thermodynamique, une approche pragmatique"), QStringLiteral("Yunus A. Çengel"),
                                ReadingStatus::Reading);
            d.poly = document(QStringLiteral("Cours Thermo S5 (poly)"), QStringLiteral("Équipe pédagogique"),
                              ReadingStatus::ToReread, 2025);
            d.td = document(QStringLiteral("TD 1 à 6, corrigés"), QStringLiteral("Équipe pédagogique"), ReadingStatus::ToRead, 2025);
            const qint64 white = document(QStringLiteral("Fluid Mechanics"), QStringLiteral("Frank M. White"), ReadingStatus::Finished, 2016);
            for (qint64 doc : {d.cengel, d.poly, d.td})
                (void)organize->attach(d.thermo, OrganizeService::Entity::Document, doc);
            (void)organize->attach(d.fluides, OrganizeService::Entity::Document, white);
            const QString cours = QStringLiteral("Thermodynamique/Cours");
            d.principe = notes->create(QStringLiteral("Second principe"), NoteType::Course, cours).value().id;
            d.formules = notes->create(QStringLiteral("Formules · Entropie"), NoteType::Formula, cours).value().id;
            d.rankine = notes->create(QStringLiteral("Cycle de Rankine"), NoteType::Concept, cours).value().id;
            for (qint64 n : {d.principe, d.formules, d.rankine})
                (void)organize->attach(d.thermo, OrganizeService::Entity::Note, n);
            return d;
        }
    };

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(500);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

private slots:
    void subjectsTreePageAndMenu()
    {
        Env env;
        const auto d = env.demo();
        ui::SubjectsPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        // Arborescence : niveaux, compteurs (sous-matières comprises)
        QTreeWidgetItem* physique = page.itemFor(d.physique);
        QVERIFY(physique);
        QCOMPARE(physique->childCount(), 2);
        QCOMPARE(physique->text(1), QStringLiteral("7"));  // 4 documents + 3 notes
        QCOMPARE(page.itemFor(d.thermo)->text(1), QStringLiteral("6"));

        // Page de la matière
        page.showSubject(d.thermo);
        QCOMPARE(page.titleLabel()->text(), QStringLiteral("Thermodynamique"));
        QCOMPARE(page.pathLabel()->text(), QStringLiteral("Physique › Thermodynamique"));
        QCOMPARE(page.statValues().at(0)->text(), QStringLiteral("3"));
        QCOMPARE(page.statValues().at(2)->text(), QStringLiteral("3"));
        QCOMPARE(page.documentRows().size(), 3);
        QCOMPARE(page.noteRows().size(), 3);
        page.setTab(ui::SubjectsPage::Tab::Notes);
        QVERIFY(page.documentRows().isEmpty());
        QCOMPARE(page.noteRows().size(), 3);

        // Clic : ouvrir
        QSignalSpy openNote(&page, &ui::SubjectsPage::openNoteRequested);
        page.noteRows().first()->click();
        QCOMPARE(openNote.size(), 1);
        page.setTab(ui::SubjectsPage::Tab::Overview);
        QSignalSpy openDoc(&page, &ui::SubjectsPage::openDocumentRequested);
        page.documentRows().first()->click();
        QCOMPARE(openDoc.size(), 1);

        // Sous-matière créée depuis la page, puis menu : couleur, archivage
        const qint64 cycles = page.createSubject(QStringLiteral("Cycles"), d.thermo);
        QVERIFY(cycles > 0);
        QCOMPARE(page.currentSubjectId(), cycles);
        QCOMPARE(page.itemFor(cycles)->parent(), page.itemFor(d.thermo));
        QMenu* menu = page.contextMenuFor(cycles);
        menu->findChild<QAction*>(QStringLiteral("color-pink"))->trigger();
        QCOMPARE(env.organize->subject(cycles).value().color, QStringLiteral("pink"));
        page.contextMenuFor(cycles)->findChild<QAction*>(QStringLiteral("archive"))->trigger();
        QVERIFY(!page.itemFor(cycles));  // masquée
        QVERIFY(env.organize->subject(cycles).value().archived);

        // Déplacement (glisser-déposer) : Mécanique des fluides au premier niveau, en tête
        emit page.tree()->itemMoved(page.itemFor(d.fluides), nullptr, 0);
        QCOMPARE(env.organize->subject(d.fluides).value().parentId, 0);
        QCOMPARE(page.tree()->topLevelItem(0)->data(0, Qt::UserRole).toLongLong(), d.fluides);
    }

    void collectionsManualAndSmart()
    {
        Env env;
        const auto d = env.demo();
        ui::CollectionsPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        // Liste de lecture : documents et notes, ordre modifiable
        const qint64 prepa = page.createCollection(QStringLiteral("Prépa examen S5"), CollectionKind::ReadingList);
        QVERIFY(prepa > 0);
        QVERIFY(env.organize->addToCollection(prepa, OrganizeService::Entity::Document, d.cengel));
        QVERIFY(env.organize->addToCollection(prepa, OrganizeService::Entity::Note, d.principe));
        QVERIFY(env.organize->addToCollection(prepa, OrganizeService::Entity::Document, d.td));
        QTRY_COMPARE(page.itemRows().size(), 3);
        page.itemMenu(2)->findChild<QAction*>(QStringLiteral("up"))->trigger();
        QCOMPARE(env.organize->collectionItems(prepa).value().at(1).id, d.td);
        QTRY_COMPARE(page.itemRows().at(1)->accessibleName(), QStringLiteral("TD 1 à 6, corrigés"));
        page.itemMenu(0)->findChild<QAction*>(QStringLiteral("remove"))->trigger();
        QTRY_COMPARE(page.itemRows().size(), 2);

        // Collection intelligente via l'éditeur : règles, nombre de résultats en direct
        ui::SmartCollectionDialog dialog(*env.ctx, 0);
        QCOMPARE(dialog.ruleCount(), 1);  // « Statut n'est pas Terminé, Archivé »
        QCOMPARE(dialog.resultLabel()->text(), QStringLiteral("3 résultats"));
        QVERIFY(!dialog.saveButton()->isEnabled());
        dialog.nameEdit()->setText(QStringLiteral("Thermo non terminés"));
        QVERIFY(dialog.saveButton()->isEnabled());
        dialog.addRule({SmartRule::Field::Subject, SmartRule::Op::Is, {env.organize->subject(d.physique).value().uuid}});
        QCOMPARE(dialog.resultLabel()->text(), QStringLiteral("3 résultats"));
        dialog.addRule({SmartRule::Field::Status, SmartRule::Op::Is, {QStringLiteral("reading")}});
        QCOMPARE(dialog.resultLabel()->text(), QStringLiteral("1 résultat"));
        dialog.matchCombo()->setCurrentIndex(1);  // au moins une
        QCOMPARE(dialog.resultLabel()->text(), QStringLiteral("4 résultats"));
        dialog.matchCombo()->setCurrentIndex(0);
        dialog.removeRule(2);
        QCOMPARE(dialog.ruleCount(), 2);
        const qint64 smart = dialog.save();
        QVERIFY(smart > 0);

        page.setSmartEditor([](qint64 id) { return id; });
        page.editSmartCollection(smart);
        QCOMPARE(page.currentCollectionId(), smart);
        QCOMPARE(page.titleLabel()->text(), QStringLiteral("Thermo non terminés"));
        QVERIFY(page.rulesLabel()->text().contains(QStringLiteral("Matière est Physique")));
        QVERIFY(page.rulesLabel()->text().contains(QStringLiteral("Statut n'est pas Terminé, Archivé")));
        QCOMPARE(page.itemRows().size(), 3);
        QVERIFY(!page.itemMenu(0) || page.itemRows().first()->contextMenuPolicy() != Qt::CustomContextMenu);

        // Recalculée quand un document change de statut
        QVERIFY(env.library->setStatus(d.td, ReadingStatus::Finished));
        page.showCollection(smart);
        QCOMPARE(page.itemRows().size(), 2);
        QCOMPARE(page.list()->topLevelItemCount(), 2);
    }

    void attachFromLibraryAndNotes()
    {
        Env env;
        const auto d = env.demo();
        const qint64 prepa = env.organize->createCollection(QStringLiteral("Prépa"), CollectionKind::Manual).value().id;

        // Bibliothèque : panneau d'un document
        ui::DocumentDetailPanel panel(*env.library, *env.theme);
        panel.setOrganize(env.organize.get());
        panel.resize(340, 900);
        panel.show();
        panel.showDocument(d.td);
        QVERIFY(panel.subjectsButton()->isVisible());
        QCOMPARE(panel.organizeChips()->findChildren<QLabel*>().size(), 1);  // Thermodynamique
        QMenu* subjects = panel.subjectsButton()->menu();
        emit subjects->aboutToShow();
        QAction* maths = nullptr;
        for (QAction* a : subjects->actions())
            if (a->text() == QStringLiteral("Mathématiques"))
                maths = a;
        QVERIFY(maths);
        QVERIFY(!maths->isChecked());
        maths->setChecked(true);
        QVERIFY(env.organize->subjectsOf(OrganizeService::Entity::Document, d.td).value().contains(d.maths));
        QMenu* collections = panel.collectionsButton()->menu();
        emit collections->aboutToShow();
        QCOMPARE(collections->actions().size(), 1);
        collections->actions().first()->setChecked(true);
        QCOMPARE(env.organize->collectionItems(prepa).value().size(), 1);
        QTRY_COMPARE(panel.organizeChips()->findChildren<QLabel*>().size(), 3);

        // Notes : sous-menus du clic droit
        ui::NotesPage notes(*env.ctx);
        QMenu menu;
        notes.addOrganizeMenus(&menu, d.principe);
        QMenu* noteSubjects = menu.findChild<QMenu*>(QStringLiteral("NoteSubjectsMenu"));
        QVERIFY(noteSubjects);
        QAction* thermo = nullptr;
        for (QAction* a : noteSubjects->actions())
            if (a->text() == QStringLiteral("Physique › Thermodynamique"))
                thermo = a;
        QVERIFY(thermo && thermo->isChecked());
        thermo->setChecked(false);
        QVERIFY(!env.organize->subjectsOf(OrganizeService::Entity::Note, d.principe).value().contains(d.thermo));
        QMenu* noteCollections = menu.findChild<QMenu*>(QStringLiteral("NoteCollectionsMenu"));
        noteCollections->actions().first()->setChecked(true);
        QCOMPARE(env.organize->collectionItems(prepa).value().size(), 2);
    }

    void navigationFromMainWindow()
    {
        Env env;
        const auto d = env.demo();
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("subjects"));
        auto* subjects = qobject_cast<ui::SubjectsPage*>(window.page(QStringLiteral("subjects")));
        QVERIFY(subjects);
        subjects->showSubject(d.thermo);
        subjects->noteRows().first()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        auto* notes = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QVERIFY(notes->currentNoteId() > 0);
    }

    void screenshots_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("clair") << false;
        QTest::newRow("sombre") << true;
    }

    void screenshots()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Capture seulement (STUDYLAB_SCREENSHOT_DIR)");
        QFETCH(bool, dark);
        Env env(dark);
        const auto d = env.demo();
        const qint64 prepa = env.organize->createCollection(QStringLiteral("Prépa examen S5"), CollectionKind::Manual).value().id;
        env.organize->createCollection(QStringLiteral("Liste de lecture · Vacances"), CollectionKind::ReadingList).value();
        env.organize->createCollection(QStringLiteral("Projet · Banc d'essai PAC"), CollectionKind::Project).value();
        SmartQuery q;
        q.rules << SmartRule{SmartRule::Field::Subject, SmartRule::Op::Is, {env.organize->subject(d.thermo).value().uuid}}
                << SmartRule{SmartRule::Field::Status, SmartRule::Op::IsNot, {QStringLiteral("finished"), QStringLiteral("archived")}}
                << SmartRule{SmartRule::Field::AddedWithin, SmartRule::Op::Is, {QStringLiteral("90")}};
        const qint64 smart = env.organize->createCollection(QStringLiteral("Thermo non terminés"), CollectionKind::Smart, q).value().id;
        for (qint64 doc : {d.cengel, d.poly})
            (void)env.organize->addToCollection(prepa, OrganizeService::Entity::Document, doc);
        (void)env.organize->addToCollection(prepa, OrganizeService::Entity::Note, d.principe);

        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("subjects"));
        qobject_cast<ui::SubjectsPage*>(window.page(QStringLiteral("subjects")))->showSubject(d.thermo);
        maybeScreenshot(window, dark ? QStringLiteral("v1-3-matieres-sombre") : QStringLiteral("v1-3-matieres"));

        window.navigateTo(QStringLiteral("collections"));
        auto* collections = qobject_cast<ui::CollectionsPage*>(window.page(QStringLiteral("collections")));
        collections->showCollection(smart);
        auto* dialog = new ui::SmartCollectionDialog(*env.ctx, smart, &window);
        dialog->show();
        dialog->move(window.geometry().center() - QPoint(dialog->width() / 2, dialog->height() / 2));
        QTest::qWait(300);
        maybeScreenshot(window, dark ? QStringLiteral("v1-3-collections-sombre") : QStringLiteral("v1-3-collections"));
        // La fenêtre de dialogue est à part : capture composée
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        QImage shot = window.grab().toImage();
        {
            QPainter p(&shot);
            const QPoint at = dialog->mapTo(&window, QPoint(0, 0));
            Q_UNUSED(at);
            const QPoint pos((window.width() - dialog->width()) / 2, (window.height() - dialog->height()) / 2);
            const QPixmap grab = dialog->grab();
            const QRect frame(pos, dialog->size());
            p.fillRect(frame.adjusted(-1, -1, 1, 1).translated(0, 6), QColor(0, 0, 0, 40));
            p.drawPixmap(pos, grab);
            p.setPen(env.theme->palette().border);
            p.drawRect(frame.adjusted(0, 0, -1, -1));
        }
        shot.save(dir + QLatin1Char('/') + (dark ? QStringLiteral("v1-3-collections-sombre") : QStringLiteral("v1-3-collections"))
                  + QStringLiteral(".png"));
        dialog->close();
    }
};

QTEST_MAIN(TstOrganizeUi)
#include "tst_organize_ui.moc"
