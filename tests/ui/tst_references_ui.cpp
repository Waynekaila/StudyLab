#include "support/WebEngineSetup.h"

#include "Bootstrap.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/references/ReferenceService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/library/DocumentDetailPanel.h"
#include "ui/library/LibraryPage.h"
#include "ui/notes/NoteEditor.h"
#include "ui/notes/NotesPage.h"
#include "ui/reader/MarkdownView.h"
#include "ui/references/ReferencesPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QAbstractItemView>
#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;
using namespace studylab::test;

namespace {

constexpr int kTimeout = 30000;

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

QString data(const QString& name)
{
    return QStringLiteral(STUDYLAB_TEST_DATA "/references/") + name;
}

}  // namespace

/// Références bibliographiques dans l'application (V1-6) : page Références (import, fiche, clé, export),
/// bouton « Citer » d'un document, citations [[@clé]] et bibliographie dans l'aperçu des notes.
class TstReferencesUi : public QObject {
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

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(900);
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
    void initTestCase()
    {
        QVERIFY(kWebEngineReady);
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("carnot.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Réflexions sur la puissance motrice du feu"), 2}));
    }

    void importEditAndExport()
    {
        Env env;
        const qint64 doc = env.import(m_pdf);
        QVERIFY(doc > 0);
        ui::MainWindow window(*env.ctx);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.sidebar()->button(QStringLiteral("references")));
        window.sidebar()->button(QStringLiteral("references"))->click();
        QCOMPARE(window.currentPage(), QStringLiteral("references"));
        auto* page = qobject_cast<ui::ReferencesPage*>(window.page(QStringLiteral("references")));
        QVERIFY(page);
        QCOMPARE(page->list()->count(), 0);

        QVERIFY(page->importFrom(data(QStringLiteral("biblio.bib"))));
        QCOMPARE(page->list()->count(), 3);
        QVERIFY2(page->navStatus()->text().startsWith(QStringLiteral("3 importée(s) · 1 reliée(s) à un document")),
                 qPrintable(page->navStatus()->text()));
        QVERIFY(page->currentId() > 0);

        // Filtre sans accents ni casse.
        QTest::keyClicks(page->filterField(), QStringLiteral("cengel"));
        QCOMPARE(page->list()->count(), 1);
        page->filterField()->clear();
        QCOMPARE(page->list()->count(), 3);

        // Fiche de l'article de Carnot, relié au PDF importé par son titre.
        QVERIFY(page->showCitekey(QStringLiteral("Carnot1824")));
        QCOMPARE(page->citationLabel()->text(), QStringLiteral("Carnot, 1872"));
        QVERIFY(page->entryLabel()->text().contains(QStringLiteral("<i>Annales scientifiques")));
        QCOMPARE(page->documentBox()->currentData().toLongLong(), doc);
        QVERIFY(page->openDocumentButton()->isVisible());
        maybeScreenshot(window, QStringLiteral("v1-6-references"));

        // Modifier un champ : enregistré au fil de la saisie, aperçu à jour.
        page->pagesField()->clear();
        QTest::keyClicks(page->pagesField(), QStringLiteral("393-400"));
        QVERIFY(page->entryLabel()->text().contains(QStringLiteral("393-400")));
        ReferenceService refs(*env.db, *env.notes);
        QTRY_COMPARE_WITH_TIMEOUT(refs.byCitekey(QStringLiteral("Carnot1824")).value().pages, QStringLiteral("393-400"), 5000);
        QCOMPARE(page->formStatus()->text(), QStringLiteral("Enregistré."));

        // Une note la cite ; changer la clé réécrit la note.
        auto note = env.notes->create(QStringLiteral("Machines thermiques"), NoteType::Course, {},
                                      QStringLiteral("Le rendement est borné ([[@Carnot1824#p. 12]])."));
        QVERIFY(note.ok());
        QVERIFY(page->showCitekey(QStringLiteral("Carnot1824")));
        QCOMPARE(page->citingRows().size(), 1);
        QCOMPARE(page->citingRows().first()->text(), QStringLiteral("Machines thermiques"));
        page->keyField()->setText(QStringLiteral("carnot1824reflexions"));
        emit page->keyField()->editingFinished();
        QCOMPARE(page->formStatus()->text(), QStringLiteral("Clé changée ; 1 note mise à jour."));
        QCOMPARE(env.notes->find(note.value().id).value().body,
                 QStringLiteral("Le rendement est borné ([[@carnot1824reflexions#p. 12]])."));
        // Clé invalide : refusée, l'ancienne revient.
        page->keyField()->setText(QStringLiteral("deux mots"));
        emit page->keyField()->editingFinished();
        QVERIFY(page->formStatus()->text().contains(QStringLiteral("invalide")));
        QCOMPARE(page->keyField()->text(), QStringLiteral("carnot1824reflexions"));
        // Clé générée.
        page->generateKeyButton()->click();
        QCOMPARE(page->keyField()->text(), QStringLiteral("carnot1872reflexions"));

        // Ouvrir la note qui la cite.
        page->citingRows().first()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("notes"));

        // Export BibTeX de toutes les références.
        const QString out = m_files.filePath(QStringLiteral("export.bib"));
        QVERIFY(page->exportTo(out));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(parseBibtex(QString::fromUtf8(f.readAll())).references.size(), 3);

        // Nouvelle référence vide, puis suppression.
        window.navigateTo(QStringLiteral("references"));
        const qint64 created = page->createReference();
        QVERIFY(created > 0);
        QCOMPARE(page->list()->count(), 4);
        QTest::keyClicks(page->titleField(), QStringLiteral("Cours de physique"));
        page->authorsField()->setPlainText(QStringLiteral("Feynman, Richard P."));
        page->yearField()->setText(QStringLiteral("1963"));
        emit page->yearField()->textEdited(QStringLiteral("1963"));
        QVERIFY(page->saveNow());
        QCOMPARE(refs.get(created).value().authors.first().family, QStringLiteral("Feynman"));
        QCOMPARE(page->citationLabel()->text(), QStringLiteral("Feynman, 1963"));
    }

    void citeFromLibrary()
    {
        Env env;
        const qint64 doc = env.import(m_pdf);
        ui::MainWindow window(*env.ctx);
        window.resize(1300, 850);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("library"));
        auto* library = qobject_cast<ui::LibraryPage*>(window.page(QStringLiteral("library")));
        library->detailPanel()->showDocument(doc);
        library->detailPanel()->referenceButton()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("references"));
        auto* page = qobject_cast<ui::ReferencesPage*>(window.page(QStringLiteral("references")));
        QVERIFY(page->currentId() > 0);
        QCOMPARE(page->titleField()->text(), QStringLiteral("Réflexions sur la puissance motrice du feu"));
        QCOMPARE(page->documentBox()->currentData().toLongLong(), doc);
        // Deuxième clic : la même fiche.
        const qint64 first = page->currentId();
        window.navigateTo(QStringLiteral("library"));
        library->detailPanel()->referenceButton()->click();
        QCOMPARE(page->currentId(), first);
        QCOMPARE(page->list()->count(), 1);
    }

    void citationsInNotePreview()
    {
        Env env;
        ReferenceService refs(*env.db, *env.notes);
        QVERIFY(refs.importFile(data(QStringLiteral("biblio.bib"))).ok());
        auto note = env.notes->create(
            QStringLiteral("Second principe"), NoteType::Course, {},
            QStringLiteral("Selon [[@cengel2019#p. 12]], et aussi [[@prigogine|Prigogine]]. Absente : [[@inconnue]]."));
        QVERIFY(note.ok());

        ui::MainWindow window(*env.ctx);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.navigateTo(QStringLiteral("notes"));
        auto* page = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        QVERIFY(page->openNote(note.value().id));
        QVERIFY(waitPreview(page, QStringLiteral("Çengel et Boles, 2019, p. 12")));

        const QVariantList links = js(page->preview(), QStringLiteral("StudyLab.wikiLinks()")).toList();
        QCOMPARE(links.size(), 3);
        QCOMPARE(links.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("(Çengel et Boles, 2019, p. 12)"));
        QVERIFY(links.at(0).toMap().value(QStringLiteral("citation")).toBool());
        QCOMPARE(links.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Prigogine"));
        QVERIFY(links.at(2).toMap().value(QStringLiteral("unresolved")).toBool());
        const QStringList bib = js(page->preview(), QStringLiteral("StudyLab.bibliography()")).toStringList();
        QCOMPARE(bib.size(), 2);
        QVERIFY2(bib.at(0).startsWith(QStringLiteral("Çengel, Y. A. et Boles, M. A. (2019).")), qPrintable(bib.join(u'|')));
        QVERIFY(bib.at(1).startsWith(QStringLiteral("Prigogine, I.")));
        maybeScreenshot(window, QStringLiteral("v1-6-citations"));

        // Autocomplétion après « [[@ ».
        page->editor()->setFocus();
        QTextCursor c = page->editor()->textCursor();
        c.movePosition(QTextCursor::End);
        page->editor()->setTextCursor(c);
        QTest::keyClicks(page->editor(), QStringLiteral(" [[@carn"));
        QTRY_VERIFY(page->editor()->linkCompleter()->popup()->isVisible());
        QCOMPARE(page->editor()->linkCompleter()->currentCompletion(), QStringLiteral("@Carnot1824"));
        QTest::keyClick(page->editor()->linkCompleter()->popup(), Qt::Key_Return);
        QVERIFY(page->editor()->toPlainText().endsWith(QStringLiteral("[[@Carnot1824]]")));

        // Clic sur une citation : la fiche de la référence.
        QVERIFY(js(page->preview(), QStringLiteral("StudyLab.clickWiki('@cengel2019#p. 12')")).toBool());
        QTRY_COMPARE(window.currentPage(), QStringLiteral("references"));
        auto* refsPage = qobject_cast<ui::ReferencesPage*>(window.page(QStringLiteral("references")));
        QCOMPARE(refsPage->keyField()->text(), QStringLiteral("cengel2019"));
    }
};

QTEST_MAIN(TstReferencesUi)
#include "tst_references_ui.moc"
