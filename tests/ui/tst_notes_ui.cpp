#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "core/domain/Annotation.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "ui/notes/NoteEditor.h"
#include "ui/notes/NotesPage.h"
#include "support/Fixtures.h"
#include "ui/reader/MarkdownView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/reader/ReaderTab.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QClipboard>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>

using namespace studylab;

namespace {

constexpr int kTimeout = 20000;

QVariant js(ui::MarkdownView* view, const QString& expression)
{
    QVariant out;
    bool done = false;
    view->evaluate(expression, [&](const QVariant& v) {
        out = v;
        done = true;
    });
    QDeadlineTimer deadline(kTimeout);
    while (!done && !deadline.hasExpired())
        QTest::qWait(10);
    return out;
}

const QString kPrincipe = QStringLiteral(
    "---\n"
    "matière: \"[[Thermodynamique]]\"\n"
    "tags: [entropie, examen]\n"
    "---\n\n"
    "L'entropie $S$ est une fonction d'état. Pour une transformation réversible :\n\n"
    "$$dS = \\frac{\\delta Q_{rev}}{T}$$\n\n"
    "> L'entropie d'un système isolé ne peut que croître ou rester constante.\n\n"
    "## Inégalité de Clausius\n\n"
    "$\\oint \\frac{\\delta Q}{T} \\le 0$, voir [[Cycle de Carnot]] et [[Production d'entropie]].\n\n"
    "## À retenir\n\n"
    "| Cas | $\\Delta S$ |\n|---|---|\n| Réversible | $= 0$ |\n| Irréversible | $> 0$ |\n\n#à-revoir\n");

}  // namespace

/// V1, itération 1 : espace de notes (arborescence, éditeur, aperçu, rétroliens, liens non résolus, images).
class TstNotesUi : public QObject {
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
            ctx = std::make_unique<AppContext>(
                AppContext{paths, *db, *settings, *theme, *library, *annotations, *comments, *search, *backups, *notes, *organize});
        }

        /// Espace de notes de démonstration (maquette 05-notes).
        struct Demo {
            qint64 principe = 0, carnot = 0, rankine = 0, formules = 0, revision = 0, exergie = 0;
        };
        qint64 import(const QString& file)
        {
            QSignalSpy finished(library.get(), &LibraryService::importFinished);
            library->importPaths({file});
            if (!finished.wait(60000))
                return 0;
            const auto report = finished.first().at(0).value<ImportReport>();
            return report.importedIds.isEmpty() ? 0 : report.importedIds.first();
        }
        AnnotationRecord annotate(qint64 documentId, int page, double y, const QString& text)
        {
            PdfLocator l;
            l.page = page;
            l.rects = {QRectF(0.1, y, 0.8, 0.02)};
            l.exact = text;
            AnnotationRecord r;
            r.documentId = documentId;
            r.unitIndex = page;
            r.sortKey = annotationSortKey(page, y);
            r.locator = l.toJson();
            r.selectedText = text;
            return annotations->create(r).value();
        }

        Demo demo()
        {
            Demo d;
            const QString cours = QStringLiteral("Thermodynamique/Cours");
            d.carnot = notes->create(QStringLiteral("Cycle de Carnot"), NoteType::Concept, cours).value().id;
            d.principe = notes->create(QStringLiteral("Second principe"), NoteType::Course, cours).value().id;
            d.rankine = notes->create(QStringLiteral("Cycle de Rankine"), NoteType::Concept, cours).value().id;
            d.formules = notes->create(QStringLiteral("Formules · Entropie"), NoteType::Formula, cours).value().id;
            d.revision = notes->create(QStringLiteral("Fiche révision · Ch. 7"), NoteType::Revision, cours).value().id;
            d.exergie = notes->create(QStringLiteral("Exergie"), NoteType::Concept, QStringLiteral("Thermodynamique")).value().id;
            notes->create(QStringLiteral("Bernoulli"), NoteType::Course, QStringLiteral("Mécanique des fluides")).value();
            notes->create(QStringLiteral("Lieu de Bode"), NoteType::Course, QStringLiteral("Automatique")).value();
            notes->create(QStringLiteral("Boîte de réception"), NoteType::Personal).value();
            notes->save(d.principe, QStringLiteral("Second principe"), kPrincipe).value();
            notes->save(d.carnot, QStringLiteral("Cycle de Carnot"),
                        QStringLiteral("Le rendement découle du [[Second principe]] appliqué à deux sources.\n")).value();
            notes->save(d.exergie, QStringLiteral("Exergie"),
                        QStringLiteral("La destruction d'exergie est liée à la création d'entropie ([[Second principe]]).\n#cycles\n")).value();
            return d;
        }
    };

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(800);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    static bool waitPreview(ui::NotesPage* page, const QString& needle)
    {
        QDeadlineTimer deadline(kTimeout);
        while (!deadline.hasExpired()) {
            if (page->preview()->isReady() && js(page->preview(), QStringLiteral("StudyLab.text()")).toString().contains(needle))
                return true;
            QTest::qWait(50);
        }
        return false;
    }

private slots:
    void initTestCase() { qRegisterMetaType<ImportReport>(); }

    void emptyStateAndNewNote()
    {
        Env env;
        ui::MainWindow window(*env.ctx);
        window.resize(1280, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.sidebar()->button(QStringLiteral("notes")));
        window.sidebar()->button(QStringLiteral("notes"))->click();
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        auto* page = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QVERIFY(page);
        QCOMPARE(page->currentNoteId(), 0);
        auto* create = page->findChild<QPushButton*>(QStringLiteral("EmptyNewNote"));
        QVERIFY(create && create->isVisible());
        maybeScreenshot(window, QStringLiteral("v1-1-notes-vide"));

        create->click();
        QVERIFY(page->currentNoteId() > 0);
        QCOMPARE(page->titleEdit()->text(), QStringLiteral("Sans titre"));
        QVERIFY(page->titleEdit()->hasSelectedText());
        QCOMPARE(page->visibleNoteIds().size(), 1);

        // Titre puis texte : enregistrés sans action de l'utilisateur.
        page->titleEdit()->setFocus();
        QTest::keyClicks(page->titleEdit(), QStringLiteral("Energie interne"));
        QTest::keyClick(page->titleEdit(), Qt::Key_Return);
        QTRY_COMPARE(env.notes->find(page->currentNoteId()).value().title, QStringLiteral("Energie interne"));
        page->editor()->setFocus();
        QTest::keyClicks(page->editor(), QStringLiteral("U est une fonction d'etat. #thermo"));
        QVERIFY(page->isDirty());
        QTRY_VERIFY_WITH_TIMEOUT(!page->isDirty(), 5000);
        QCOMPARE(env.notes->find(page->currentNoteId()).value().body, QStringLiteral("U est une fonction d'etat. #thermo"));
        QCOMPARE(page->saveStateLabel()->text(), QStringLiteral("Enregistré"));
        QTRY_COMPARE(page->tagButtons().size(), 1);
        QCOMPARE(page->tagButtons().first()->text(), QStringLiteral("#thermo"));
    }

    void previewLinksBacklinksAndOutline()
    {
        Env env;
        const auto d = env.demo();
        ui::NotesPage page(*env.ctx);
        page.resize(1280, 820);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QVERIFY(page.openNote(d.principe));
        QCOMPARE(page.mode(), ui::NotesPage::Mode::Split);
        QCOMPARE(page.typeButton()->text(), noteTypeLabel(NoteType::Course));
        QVERIFY(waitPreview(&page, QStringLiteral("Inégalité de Clausius")));

        // Aperçu : titre, puces (dossier, tags), formules KaTeX, liens [[…]] résolus ou non.
        const QString html = js(page.preview(), QStringLiteral("StudyLab.html()")).toString();
        QVERIFY(html.contains(QStringLiteral("class=\"katex")));
        QVERIFY(html.contains(QStringLiteral("<table>")));
        QVERIFY(html.contains(QStringLiteral(">#examen<")));
        QVERIFY(html.contains(QStringLiteral(">Cours<")));
        QVERIFY(!html.contains(QStringLiteral("matière:")));  // front matter masqué
        const QVariantList links = js(page.preview(), QStringLiteral("StudyLab.wikiLinks()")).toList();
        QCOMPARE(links.size(), 2);
        QCOMPARE(links.at(0).toMap().value(QStringLiteral("unresolved")).toBool(), false);
        QCOMPARE(links.at(1).toMap().value(QStringLiteral("target")).toString(), QStringLiteral("Production d'entropie"));
        QCOMPARE(links.at(1).toMap().value(QStringLiteral("unresolved")).toBool(), true);

        // Panneau de contexte
        QCOMPARE(page.backlinkCountLabel()->text(), QStringLiteral("2"));
        QCOMPARE(page.backlinkButtons().size(), 2);
        QCOMPARE(page.outlineButtons().size(), 2);
        QCOMPARE(page.outlineButtons().at(0)->text(), QStringLiteral("Inégalité de Clausius"));
        QCOMPARE(page.createLinkButtons().size(), 1);
        // Partagé : éditeur et aperçu côte à côte, à parts égales au premier affichage.
        QVERIFY(page.preview()->width() > 200);
        QVERIFY(qAbs(page.preview()->width() - page.editor()->parentWidget()->width()) < 40);
        // Tags disposés en lignes, sans chevauchement.
        const auto tagButtons = page.tagButtons();
        for (int i = 1; i < tagButtons.size(); ++i)
            QVERIFY(!tagButtons.at(i)->geometry().intersects(tagButtons.at(i - 1)->geometry()));

        // Plan → ligne du titre dans l'éditeur
        page.outlineButtons().at(1)->click();
        QCOMPARE(page.editor()->textCursor().block().text(), QStringLiteral("## À retenir"));

        // « créer » : la note manquante est créée dans le même dossier et ouverte.
        page.createLinkButtons().first()->click();
        const qint64 production = page.currentNoteId();
        QVERIFY(production != d.principe);
        QCOMPARE(env.notes->find(production).value().title, QStringLiteral("Production d'entropie"));
        QCOMPARE(env.notes->find(production).value().folder, QStringLiteral("Thermodynamique/Cours"));
        QTRY_COMPARE(page.backlinkButtons().size(), 1);

        // Rétrolien → note qui cite
        page.backlinkButtons().first()->click();
        QCOMPARE(page.currentNoteId(), d.principe);
        QTRY_COMPARE(page.createLinkButtons().size(), 0);

        // Clic sur un [[lien]] de l'aperçu
        QVERIFY(waitPreview(&page, QStringLiteral("Inégalité de Clausius")));
        QVERIFY(js(page.preview(), QStringLiteral("StudyLab.clickWiki('Cycle de Carnot')")).toBool());
        QTRY_COMPARE(page.currentNoteId(), d.carnot);

        // Renommer la note ouverte met à jour les liens de celles qui la citent.
        page.titleEdit()->setText(QStringLiteral("Cycle de Carnot idéal"));
        emit page.titleEdit()->textEdited(page.titleEdit()->text());
        QVERIFY(page.saveNow());
        QVERIFY(env.notes->find(d.principe).value().body.contains(QStringLiteral("[[Cycle de Carnot idéal]]")));

        // Modes d'affichage
        page.setMode(ui::NotesPage::Mode::Preview);
        QVERIFY(!page.editor()->isVisible());
        QVERIFY(page.preview()->isVisible());
        page.setMode(ui::NotesPage::Mode::Source);
        QVERIFY(page.editor()->isVisible());
        QVERIFY(!page.preview()->isVisible());
        QCOMPARE(env.settings->value(QStringLiteral("notes/mode")).toInt(), 0);
    }

    void treeTagsFilterDeleteAndUndo()
    {
        Env env;
        const auto d = env.demo();
        ui::NotesPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        QCOMPARE(page.visibleNoteIds().size(), 9);
        // Dossiers d'abord, la note de la racine en dernier.
        QCOMPARE(page.tree()->topLevelItem(page.tree()->topLevelItemCount() - 1)->text(0), QStringLiteral("Boîte de réception"));
        QStringList tags;
        for (QToolButton* b : page.tagButtons())
            tags << b->text();
        QVERIFY2(tags.contains(QStringLiteral("#examen")) && tags.contains(QStringLiteral("#cycles")), qPrintable(tags.join(u' ')));

        page.setTagFilter(QStringLiteral("examen"));
        QCOMPARE(page.visibleNoteIds(), QList<qint64>{d.principe});
        page.setTagFilter(QString());
        page.filterEdit()->setText(QStringLiteral("cycle"));
        QCOMPARE(page.visibleNoteIds().size(), 2);
        page.filterEdit()->clear();

        QVERIFY(page.openNote(d.exergie));
        page.deleteNote(d.exergie);
        QVERIFY(page.undoBar()->isVisible());
        QVERIFY(!page.visibleNoteIds().contains(d.exergie));
        QVERIFY(page.currentNoteId() != d.exergie);
        page.undoDelete();
        QVERIFY(page.visibleNoteIds().contains(d.exergie));
        QCOMPARE(page.currentNoteId(), d.exergie);
    }

    void pastedImageAndUnsavedTextSurviveClose()
    {
        Env env;
        const qint64 id = env.notes->create(QStringLiteral("Schéma"), NoteType::Personal).value().id;
        {
            ui::NotesPage page(*env.ctx);
            page.resize(1100, 760);
            page.show();
            QVERIFY(QTest::qWaitForWindowExposed(&page));
            QVERIFY(page.openNote(id));

            QImage image(40, 24, QImage::Format_RGB32);
            image.fill(QColor(0x6E, 0x8F, 0xC2));
            // Coller une image (presse-papiers de l'application) : copiée dans attachments/, lien inséré.
            page.editor()->setFocus();
            QApplication::clipboard()->setImage(image);
            QSignalSpy inserted(page.editor(), &ui::NoteEditor::imageInserted);
            page.editor()->paste();
            QCOMPARE(inserted.count(), 1);
            QCOMPARE(inserted.first().at(1).toString(), QStringLiteral("png"));
            QVERIFY(page.editor()->toPlainText().contains(QStringLiteral("![](attachments/")));
            QVERIFY(waitPreview(&page, QStringLiteral("Schéma")));
            QTRY_VERIFY_WITH_TIMEOUT(js(page.preview(), QStringLiteral("StudyLab.html()")).toString()
                                         .contains(QStringLiteral("src=\"data:image/png;base64,")), kTimeout);

            // Texte tapé puis fermeture immédiate, avant l'enregistrement différé.
            page.editor()->moveCursor(QTextCursor::End);
            QTest::keyClicks(page.editor(), QStringLiteral("Legende"));
            QVERIFY(page.isDirty());
        }
        const QString body = env.notes->find(id).value().body;
        QVERIFY(body.contains(QStringLiteral("![](attachments/")));
        QVERIFY(body.endsWith(QStringLiteral("Legende")));
    }

    void notesFromReadingOpenTheirSources()
    {
        Env env;
        QTemporaryDir files;
        const QString pdf = files.filePath(QStringLiteral("carnot.pdf"));
        QVERIFY(test::writePdf(pdf, {QStringLiteral("Reflexions sur la puissance motrice du feu"), 3}));
        const qint64 doc = env.import(pdf);
        QVERIFY(doc > 0);
        const AnnotationRecord ann = env.annotate(doc, 1, 0.3, QStringLiteral("La puissance motrice du feu est independante des agents."));
        env.settings->setValue(QStringLiteral("ui/lastPage"), QStringLiteral("library"));
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        auto* notes = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        window.navigateTo(QStringLiteral("reader"));
        reader->openDocument(doc);
        QTRY_VERIFY(reader->currentTab() && reader->currentTab()->isLoaded());

        // Annotation → note de lecture ouverte dans l'espace de notes, avec sa source.
        const qint64 id = reader->currentTab()->createNoteFromAnnotations({ann.id});
        QVERIFY(id > 0);
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));
        QCOMPARE(notes->currentNoteId(), id);
        QCOMPARE(env.notes->find(id).value().folder, QStringLiteral("Notes de lecture"));
        QCOMPARE(notes->sourceButtons().size(), 1);
        QVERIFY(notes->sourceButtons().first()->accessibleName().startsWith(QStringLiteral("Reflexions sur la puissance motrice du feu · p. 2")));

        // Aperçu : la citation renvoie au passage, le [[document]] est reconnu comme tel.
        QVERIFY(waitPreview(notes, QStringLiteral("independante des agents")));
        const QStringList appLinks = js(notes->preview(), QStringLiteral("StudyLab.appLinks()")).toStringList();
        QCOMPARE(appLinks.size(), 1);
        QVERIFY(appLinks.first().startsWith(QStringLiteral("studylab://open?doc=")));
        const QVariantList links = js(notes->preview(), QStringLiteral("StudyLab.wikiLinks()")).toList();
        QCOMPARE(links.size(), 1);
        QVERIFY(links.first().toMap().value(QStringLiteral("document")).toBool());

        // Source → lecteur, sur le document
        notes->sourceButtons().first()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        QCOMPARE(reader->currentTab()->documentId(), doc);

        // [[document]] dans l'aperçu → lecteur ; lien studylab:// → lecteur
        window.navigateTo(QStringLiteral("notes"));
        QVERIFY(js(notes->preview(), QStringLiteral("StudyLab.clickWiki('Reflexions sur la puissance motrice du feu')")).toBool());
        QTRY_COMPARE(window.currentPage(), QStringLiteral("reader"));
        window.navigateTo(QStringLiteral("notes"));
        QVERIFY(js(notes->preview(), QStringLiteral("StudyLab.clickHref('%1')").arg(appLinks.first())).toBool());
        QTRY_COMPARE(window.currentPage(), QStringLiteral("reader"));

        // Autocomplétion « [[ » : notes et documents
        window.navigateTo(QStringLiteral("notes"));
        notes->editor()->setFocus();
        notes->editor()->moveCursor(QTextCursor::End);
        QTest::keyClick(notes->editor(), Qt::Key_Return);
        QTest::keyClicks(notes->editor(), QStringLiteral("Voir [[reflexions sur"));
        QCompleter* completer = notes->editor()->linkCompleter();
        QTRY_VERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->currentCompletion(), QStringLiteral("Reflexions sur la puissance motrice du feu"));
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QVERIFY(!completer->popup()->isVisible());
        QVERIFY(notes->editor()->toPlainText().endsWith(QStringLiteral("Voir [[Reflexions sur la puissance motrice du feu]]")));
    }

    void importObsidianVault()
    {
        Env env;
        const QString vault = env.dir.filePath(QStringLiteral("Mon coffre"));
        auto write = [](const QString& path, const QByteArray& data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
        };
        write(vault + QStringLiteral("/.obsidian/app.json"), "{}");
        write(vault + QStringLiteral("/Thermo/Entropie.md"),
              QStringLiteral("---\ntype: concept\ntags: [thermo]\n---\n\n# Entropie\n\nVoir [[Carnot]].\n").toUtf8());
        write(vault + QStringLiteral("/Thermo/Carnot.md"),
              QStringLiteral("Cycle idéal.\n\n![[absent.png]]\n").toUtf8());
        write(vault + QStringLiteral("/refs.bib"), "@book{cengel2019, title = {Thermodynamique}, year = {2019}}\n");

        ui::NotesPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QVERIFY(!page.importStatusLabel()->isVisible());

        QVERIFY(page.importVault(vault));
        QCOMPARE(page.visibleNoteIds().size(), 2);
        QVERIFY(page.currentNoteId() > 0);
        const QString status = page.importStatusLabel()->text();
        QVERIFY(page.importStatusLabel()->isVisible());
        QVERIFY2(status.contains(QStringLiteral("2 notes")) && status.contains(QStringLiteral("1 référence"))
                     && status.contains(QStringLiteral("1 avertissement")),
                 qPrintable(status));
        QVERIFY(page.importStatusLabel()->toolTip().contains(QStringLiteral("absent.png")));
        // Notes rangées dans un dossier au nom du coffre, sous-dossiers conservés.
        QTreeWidgetItem* top = page.tree()->topLevelItem(0);
        QCOMPARE(top->text(0), QStringLiteral("Mon coffre"));
        QCOMPARE(top->child(0)->text(0), QStringLiteral("Thermo"));
        QCOMPARE(env.notes->find(env.notes->resolve(QStringLiteral("Entropie")).value()).value().type, NoteType::Concept);

        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            page.grab().save(dir + QStringLiteral("/v1-6b-import-coffre.png"));
        }

        // Une seconde fois : tout est déjà là.
        QVERIFY(page.importVault(vault));
        QCOMPARE(page.visibleNoteIds().size(), 2);
        QVERIFY2(page.importStatusLabel()->text().contains(QStringLiteral("2 déjà présente")),
                 qPrintable(page.importStatusLabel()->text()));
        QVERIFY(!page.importVault(env.dir.filePath(QStringLiteral("absent"))));
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
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("notes"));
        auto* page = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QVERIFY(page->openNote(d.principe));
        QVERIFY(waitPreview(page, QStringLiteral("Inégalité de Clausius")));
        maybeScreenshot(window, dark ? QStringLiteral("v1-1-notes-sombre") : QStringLiteral("v1-1-notes"));

        // V1-2 : note de lecture tirée de deux annotations, avec ses sources.
        QTemporaryDir files;
        const QString pdf = files.filePath(QStringLiteral("thermo.pdf"));
        QVERIFY(test::writePdf(pdf, {QStringLiteral("Thermodynamique"), 3}));
        const qint64 doc = env.import(pdf);
        QVERIFY(doc > 0);
        const auto a1 = env.annotate(doc, 0, 0.3, QStringLiteral("L'entropie d'un système isolé ne peut que croître ou rester constante."));
        const auto a2 = env.annotate(doc, 1, 0.5, QStringLiteral("Le rendement d'une machine réversible ne dépend que des températures des sources."));
        (void)env.annotations->setNote(a2.id, QStringLiteral("Lien direct avec le [[Cycle de Carnot]]."));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        window.navigateTo(QStringLiteral("reader"));
        reader->openDocument(doc);
        QTRY_VERIFY(reader->currentTab() && reader->currentTab()->isLoaded());
        QVERIFY(reader->currentTab()->createNoteFromAnnotations({a1.id, a2.id}) > 0);
        QVERIFY(waitPreview(page, QStringLiteral("rester constante")));
        maybeScreenshot(window, dark ? QStringLiteral("v1-2-note-lecture-sombre") : QStringLiteral("v1-2-note-lecture"));
    }
};

QTEST_MAIN(TstNotesUi)
#include "tst_notes_ui.moc"
