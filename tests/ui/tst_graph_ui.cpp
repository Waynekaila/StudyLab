#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/repositories/DocumentRepository.h"
#include "data/repositories/NoteRepository.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/graph/GraphPage.h"
#include "ui/graph/GraphView.h"
#include "ui/notes/NotesPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;

/// V1, itération 4 : graphe de connaissances interactif.
class TstGraphUi : public QObject {
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

        /// Thermodynamique, mécanique des fluides, automatique et mathématiques (maquette 07).
        struct Demo {
            qint64 thermo = 0, fluides = 0, autom = 0, maths = 0;
            qint64 cengel = 0, poly = 0, candel = 0, bode = 0;
            qint64 principe = 0, entropie = 0, carnot = 0, clausius = 0, exergie = 0, reynolds = 0;
        };
        Demo demo(bool rich = false)
        {
            Demo d;
            d.thermo = organize->createSubject(QStringLiteral("Thermodynamique"), 0, QStringLiteral("blue")).value().id;
            d.fluides = organize->createSubject(QStringLiteral("Mécanique des fluides"), 0, QStringLiteral("green")).value().id;
            d.autom = organize->createSubject(QStringLiteral("Automatique"), 0, QStringLiteral("purple")).value().id;
            d.maths = organize->createSubject(QStringLiteral("Mathématiques"), 0, QStringLiteral("yellow")).value().id;
            d.cengel = document(QStringLiteral("Çengel · Thermodynamique"), QStringLiteral("Yunus A. Çengel"), ReadingStatus::Reading);
            d.poly = document(QStringLiteral("Cours Thermo S5"), QStringLiteral("Équipe pédagogique"), ReadingStatus::Reading);
            d.candel = document(QStringLiteral("Candel · Fluides"), QStringLiteral("Sébastien Candel"), ReadingStatus::Reading);
            d.bode = document(QStringLiteral("Feedback Systems"), QStringLiteral("K. J. Åström"), ReadingStatus::Reading);
            const auto note = [this](const QString& title, NoteType type, const QString& body, qint64 subject) {
                const qint64 id = notes->create(title, type, {}, body).value().id;
                if (subject > 0)
                    (void)organize->attach(subject, OrganizeService::Entity::Note, id);
                return id;
            };
            d.entropie = note(QStringLiteral("Entropie"), NoteType::Concept,
                              QStringLiteral("Grandeur d'état extensive. Voir [[Irréversibilités]]."), d.thermo);
            d.carnot = note(QStringLiteral("Cycle de Carnot"), NoteType::Course,
                            QStringLiteral("Rendement maximal entre deux sources. [[Cycle de Rankine]]"), d.thermo);
            d.clausius = note(QStringLiteral("Inégalité de Clausius"), NoteType::Formula,
                              QStringLiteral("Pour un cycle : ∮ δQ/T ≤ 0. [[Cours Thermo S5]]"), d.thermo);
            d.exergie = note(QStringLiteral("Exergie"), NoteType::Course,
                             QStringLiteral("Travail maximal récupérable. [[Pompe à chaleur]]"), d.thermo);
            d.principe = note(QStringLiteral("Second principe"), NoteType::Course,
                              QStringLiteral("# Second principe\n\nL'entropie est une fonction d'état. Pour une transformation "
                                             "réversible, dS = δQrev/T.\n\n[[Entropie]] · [[Cycle de Carnot]] · "
                                             "[[Inégalité de Clausius]] · [[Exergie]] · [[Çengel · Thermodynamique]] · "
                                             "[[Irréversibilités]]"),
                              d.thermo);
            (void)db::NoteRepository(*db).addSources(d.principe, {{QStringLiteral("document"), d.cengel}});
            d.reynolds = note(QStringLiteral("Nombre de Reynolds"), NoteType::Concept,
                              QStringLiteral("Re = ρVL/μ. [[Candel · Fluides]] [[Couche limite]]"), d.fluides);
            for (qint64 doc : {d.cengel, d.poly})
                (void)organize->attach(d.thermo, OrganizeService::Entity::Document, doc);
            (void)organize->attach(d.fluides, OrganizeService::Entity::Document, d.candel);
            (void)organize->attach(d.autom, OrganizeService::Entity::Document, d.bode);
            if (rich) {
                note(QStringLiteral("Irréversibilités"), NoteType::Concept, QStringLiteral("[[Transferts thermiques]]"), d.thermo);
                note(QStringLiteral("Cycle de Rankine"), NoteType::Course, QStringLiteral("[[Pompe à chaleur]]"), d.thermo);
                note(QStringLiteral("Pompe à chaleur"), NoteType::Research, QStringLiteral("[[Cycle de Carnot]]"), d.thermo);
                note(QStringLiteral("Couche limite"), NoteType::Course, QStringLiteral("[[Viscosité]] [[Candel · Fluides]]"), d.fluides);
                note(QStringLiteral("Viscosité"), NoteType::Concept, QStringLiteral("[[Convection]]"), d.fluides);
                note(QStringLiteral("Convection"), NoteType::Concept, QStringLiteral("[[Transferts thermiques]]"), d.fluides);
                const qint64 transferts = document(QStringLiteral("Transferts thermiques"), QStringLiteral("J. Taine"), ReadingStatus::Reading);
                (void)organize->attach(d.thermo, OrganizeService::Entity::Document, transferts);
                note(QStringLiteral("Diagramme de Bode"), NoteType::Course,
                     QStringLiteral("[[Feedback Systems]] [[Réponse fréquentielle]]"), d.autom);
                note(QStringLiteral("Réponse fréquentielle"), NoteType::Course,
                     QStringLiteral("[[Transformée de Laplace]] [[Série de Fourier]] [[Feedback Systems]]"), d.autom);
                note(QStringLiteral("Transformée de Laplace"), NoteType::Formula, QStringLiteral("[[Intégrales curvilignes]]"), d.maths);
                note(QStringLiteral("Série de Fourier"), NoteType::Formula, QStringLiteral("[[Intégrales curvilignes]]"), d.maths);
                note(QStringLiteral("Intégrales curvilignes"), NoteType::Course, QStringLiteral("[[Inégalité de Clausius]]"), d.maths);
            }
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

    static int highlighted(ui::GraphView* view)
    {
        int n = 0;
        for (int i = 0; i < view->graph().nodes.size(); ++i)
            n += view->isHighlighted(i) ? 1 : 0;
        return n;
    }

    static QString key(const char* prefix, qint64 id) { return QStringLiteral("%1:%2").arg(QLatin1String(prefix)).arg(id); }

private slots:
    void initTestCase() { qRegisterMetaType<NoteRecord>(); }

    void filtersAndLayout()
    {
        Env env;
        const auto d = env.demo();
        ui::GraphPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QTRY_VERIFY(!page.layoutRunning());

        // 4 documents, 6 notes ; les liens non résolus sont masqués par défaut.
        ui::GraphView* view = page.view();
        QCOMPARE(view->graph().nodes.size(), 10);
        QVERIFY(page.statusLabel()->text().startsWith(QStringLiteral("10 nœuds · ")));
        QVERIFY(page.statusLabel()->text().endsWith(QStringLiteral("disposition calculée en arrière-plan")));
        const QPointF a = view->nodePosition(view->graph().indexOf(key("n", d.principe)));
        const QPointF b = view->nodePosition(view->graph().indexOf(key("n", d.entropie)));
        QVERIFY(a != b);

        page.typeToggle(GraphNodeKind::Unresolved)->click();
        QCOMPARE(view->graph().nodes.size(), 14);  // Irréversibilités, Rankine, Pompe à chaleur, Couche limite
        page.typeToggle(GraphNodeKind::Document)->click();
        QCOMPARE(view->graph().nodes.size(), 10);
        page.typeToggle(GraphNodeKind::Document)->click();
        page.typeToggle(GraphNodeKind::Unresolved)->click();
        page.typeToggle(GraphNodeKind::Concept)->click();
        QCOMPARE(view->graph().nodes.size(), 8);
        page.typeToggle(GraphNodeKind::Concept)->click();

        // Matières : décocher l'automatique retire Feedback Systems.
        QCheckBox* autom = nullptr;
        for (QCheckBox* box : page.subjectBoxes()) {
            if (box->text() == QStringLiteral("Automatique"))
                autom = box;
        }
        QVERIFY(autom);
        QCOMPARE(page.subjectBoxes().size(), 5);  // + « Sans matière »
        autom->click();
        QCOMPARE(view->graph().nodes.size(), 9);
        QCOMPARE(view->graph().indexOf(key("d", d.bode)), -1);
        autom->click();
        QCOMPARE(view->graph().nodes.size(), 10);

        // Étiquettes, regroupement : relance de la disposition
        page.labelsToggle()->click();
        QVERIFY(!view->labelsVisible());
        page.groupToggle()->click();
        QVERIFY(page.layoutRunning());
        QTRY_VERIFY(!page.layoutRunning());

        // Mise à jour en direct : une nouvelle note liée apparaît.
        QVERIFY(env.notes->create(QStringLiteral("Troisième principe"), NoteType::Course, {},
                                  QStringLiteral("Suite du [[Second principe]]")).ok());
        QCOMPARE(view->graph().nodes.size(), 11);
        QTRY_VERIFY(!page.layoutRunning());
    }

    void selectionCardIsolation()
    {
        Env env;
        const auto d = env.demo();
        ui::GraphPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QTRY_VERIFY(!page.layoutRunning());
        ui::GraphView* view = page.view();
        QVERIFY(page.card()->isHidden());

        // Fiche de la note
        QVERIFY(page.selectNode(key("n", d.principe)));
        QVERIFY(page.card()->isVisible());
        QCOMPARE(page.cardTitle()->text(), QStringLiteral("Second principe"));
        QCOMPARE(page.cardKind()->text(), QStringLiteral("Note · Fiche de cours"));
        QVERIFY(page.cardExcerpt()->text().startsWith(QStringLiteral("L'entropie est une fonction d'état.")));
        QCOMPARE(page.cardStats().at(0)->text(), QStringLiteral("6"));  // liens sortants
        QCOMPARE(page.cardStats().at(1)->text(), QStringLiteral("0"));  // rétroliens
        QCOMPARE(page.cardStats().at(2)->text(), QStringLiteral("1"));  // sources

        // Profondeur : 1 = voisins directs, 2 = leurs voisins aussi
        page.depthControl()->setCurrentIndex(0);
        emit page.depthControl()->currentIndexChanged(0);
        QCOMPARE(highlighted(view), 6);  // la note, 4 notes, Çengel
        page.depthControl()->setCurrentIndex(1);
        emit page.depthControl()->currentIndexChanged(1);
        QCOMPARE(highlighted(view), 7);  // + Cours Thermo S5 (cité par Clausius)

        // Ouvrir
        QSignalSpy openNote(&page, &ui::GraphPage::openNoteRequested);
        page.openButton()->click();
        QCOMPARE(openNote.size(), 1);
        QCOMPARE(openNote.first().first().toLongLong(), d.principe);

        // Isoler, puis tout afficher
        page.isolateButton()->click();
        QCOMPARE(page.isolatedKey(), key("n", d.principe));
        QCOMPARE(view->graph().nodes.size(), 7);
        QVERIFY(page.showAllButton()->isVisible());
        QCOMPARE(page.isolateButton()->text(), QStringLiteral("Tout afficher"));
        QVERIFY(page.statusLabel()->text().startsWith(QStringLiteral("Voisinage de « Second principe » · 7 nœuds")));
        QTRY_VERIFY(!page.layoutRunning());
        page.showAllButton()->click();
        QVERIFY(page.isolatedKey().isEmpty());
        QCOMPARE(view->graph().nodes.size(), 10);
        QCOMPARE(page.selectedKey(), key("n", d.principe));  // la sélection reste

        // Document : chiffres propres
        QVERIFY(page.selectNode(key("d", d.cengel)));
        QCOMPARE(page.cardKind()->text(), QStringLiteral("Document · Yunus A. Çengel"));
        QCOMPARE(page.cardStats().at(0)->text(), QStringLiteral("1"));  // note liée
        QCOMPARE(page.cardStats().at(1)->text(), QStringLiteral("1"));  // note tirée
        QSignalSpy openDoc(&page, &ui::GraphPage::openDocumentRequested);
        page.openButton()->click();
        QCOMPARE(openDoc.size(), 1);

        // Clic sur le fond : plus de sélection
        emit view->nodeClicked(-1);
        QVERIFY(page.card()->isHidden());
        QCOMPARE(highlighted(view), 10);

        // Trouver un nœud
        QVERIFY(page.findNode(QStringLiteral("reynolds")));
        QCOMPARE(page.selectedKey(), key("n", d.reynolds));
        QCOMPARE(page.cardKind()->text(), QStringLiteral("Concept"));
        QVERIFY(!page.findNode(QStringLiteral("introuvable")));

        // Lien non résolu : créer la note
        page.typeToggle(GraphNodeKind::Unresolved)->click();
        QVERIFY(page.selectNode(QStringLiteral("u:couche limite")));
        QCOMPARE(page.openButton()->text(), QStringLiteral("Créer la note"));
        QCOMPARE(page.cardStats().at(0)->text(), QStringLiteral("1"));
        QVERIFY(!page.cardStats().at(1)->isVisible());
        QSignalSpy create(&page, &ui::GraphPage::createNoteRequested);
        page.openButton()->click();
        QCOMPARE(create.size(), 1);
        QCOMPARE(create.first().first().toString(), QStringLiteral("Couche limite"));
    }

    void emptyGraph()
    {
        Env env;
        ui::GraphPage page(*env.ctx);
        QSignalSpy notes(&page, &ui::GraphPage::notesRequested);
        page.resize(900, 600);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QVERIFY(!page.view()->isVisible());
        const auto buttons = page.findChildren<QPushButton*>();
        for (QPushButton* b : buttons) {
            if (b->text() == QStringLiteral("Mes notes") && b->isVisible())
                b->click();
        }
        QCOMPARE(notes.size(), 1);
    }

    void navigationFromMainWindow()
    {
        Env env;
        const auto d = env.demo();
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("graph"));
        auto* graph = qobject_cast<ui::GraphPage*>(window.page(QStringLiteral("graph")));
        QVERIFY(graph);
        QTRY_VERIFY(!graph->layoutRunning());
        QVERIFY(graph->selectNode(key("n", d.entropie)));
        emit graph->view()->nodeActivated(graph->view()->selected());
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        auto* notes = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QCOMPARE(notes->currentNoteId(), d.entropie);

        // Lien non résolu → note créée
        window.navigateTo(QStringLiteral("graph"));
        graph->typeToggle(GraphNodeKind::Unresolved)->click();
        QVERIFY(graph->selectNode(QStringLiteral("u:pompe à chaleur")));
        graph->openButton()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        QCOMPARE(env.notes->find(notes->currentNoteId()).value().title, QStringLiteral("Pompe à chaleur"));
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
        const auto d = env.demo(true);
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("graph"));
        auto* graph = qobject_cast<ui::GraphPage*>(window.page(QStringLiteral("graph")));
        QTRY_VERIFY_WITH_TIMEOUT(!graph->layoutRunning(), 20000);
        QTest::qWait(600);
        graph->selectNode(key("n", d.principe));
        maybeScreenshot(window, dark ? QStringLiteral("v1-4-graphe-sombre") : QStringLiteral("v1-4-graphe"));
    }
};

QTEST_MAIN(TstGraphUi)
#include "tst_graph_ui.moc"
