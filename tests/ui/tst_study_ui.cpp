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
#include "ui/notes/NoteEditor.h"
#include "ui/notes/NotesPage.h"
#include "ui/reader/MarkdownView.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Sidebar.h"
#include "ui/study/CardEditorDialog.h"
#include "ui/study/RevisionsPage.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDeadlineTimer>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

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

/// Texte visible de la carte, une fois rendu et contenant `needle`.
bool waitCard(ui::MarkdownView* view, const QString& needle)
{
    QDeadlineTimer deadline(kTimeout);
    while (!deadline.hasExpired()) {
        if (view && view->isReady()
            && js(view, QStringLiteral("document.body.innerText")).toString().contains(needle))
            return true;
        QTest::qWait(50);
    }
    return false;
}

}  // namespace

/// Espace Révisions (V2-1) : paquets, saisie des cartes, séance (recto, verso, réponses au clavier), statistiques,
/// pastille de la navigation et cartes créées depuis une annotation ou une note.
class TstStudyUi : public QObject {
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

private slots:
    void initTestCase()
    {
        QVERIFY(kWebEngineReady);
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("cengel.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Thermodynamique Çengel"), 3}));
    }

    void decksAndCardEditor()
    {
        Env env;
        StudyService study(*env.db);
        ui::RevisionsPage page(*env.ctx, study);
        page.resize(1200, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(page.center()->currentWidget()->objectName(), QString());  // état vide
        QCOMPARE(page.deckList()->count(), 0);

        // Première carte sans paquet : « Mes cartes » est créé à l'enregistrement.
        ui::CardEditorDialog* dlg = page.addCard();
        QVERIFY(dlg->deckCombo()->isEditable());
        QCOMPARE(dlg->deckCombo()->currentText(), QStringLiteral("Mes cartes"));
        QVERIFY(!dlg->save());  // recto vide
        QVERIFY(dlg->errorLabel()->isVisibleTo(dlg));
        dlg->frontEdit()->setPlainText(QStringLiteral("Premier principe ?"));
        dlg->backEdit()->setPlainText(QStringLiteral("$\\Delta U = W + Q$"));
        QVERIFY(dlg->save());
        dlg->accept();
        QCOMPARE(page.deckList()->count(), 1);
        QCOMPARE(page.deckList()->item(0)->text(), QStringLiteral("Mes cartes"));
        QCOMPARE(page.center()->currentWidget(), page.overviewPage());
        QCOMPARE(page.cardList()->topLevelItemCount(), 1);
        QCOMPARE(page.tileValue(0)->text(), QStringLiteral("1"));
        QVERIFY(page.startButton()->isEnabled());

        // Nouveau paquet, texte à trous : une carte par trou.
        const qint64 thermo = page.createDeck(QStringLiteral("Thermodynamique"));
        QVERIFY(thermo > 0);
        QCOMPARE(page.currentDeckId(), thermo);
        QCOMPARE(page.deckTitle()->text(), QStringLiteral("Thermodynamique"));
        QCOMPARE(page.cardList()->topLevelItemCount(), 0);
        QVERIFY(!page.startButton()->isEnabled());
        dlg = page.addCard();
        QCOMPARE(dlg->deckCombo()->currentData().toLongLong(), thermo);
        dlg->typeCombo()->setCurrentIndex(dlg->typeCombo()->findText(QStringLiteral("Texte à trous")));
        dlg->frontEdit()->setPlainText(QStringLiteral("Le rendement de {{c1::Carnot}} vaut {{c2::$1 - T_F/T_C$}}."));
        QVERIFY(dlg->save());
        QCOMPARE(dlg->saved().size(), 2);
        dlg->accept();
        QCOMPARE(page.cardList()->topLevelItemCount(), 2);
        // Les plus récentes d'abord.
        QCOMPARE(page.cardList()->topLevelItem(0)->text(0), QStringLiteral("[c2] Le rendement de Carnot vaut $1 - T_F/T_C$."));
        QVERIFY(page.cardList()->topLevelItem(1)->text(0).startsWith(QStringLiteral("[c1] ")));
        QCOMPARE(page.cardList()->topLevelItem(0)->text(1), QStringLiteral("Texte à trous"));
        QCOMPARE(page.cardList()->topLevelItem(0)->text(2), QStringLiteral("Nouvelle"));

        // Basique et inversée : deux cartes.
        dlg = page.addCard();
        dlg->typeCombo()->setCurrentIndex(dlg->typeCombo()->findText(QStringLiteral("Basique et inversée (2 cartes)")));
        dlg->frontEdit()->setPlainText(QStringLiteral("Entropie"));
        dlg->backEdit()->setPlainText(QStringLiteral("S"));
        QVERIFY(dlg->save());
        dlg->accept();
        QCOMPARE(page.cardList()->topLevelItemCount(), 4);

        // Modification et suspension.
        const qint64 first = page.cardList()->topLevelItem(0)->data(0, Qt::UserRole).toLongLong();
        dlg = page.editCard(first);
        QVERIFY(dlg);
        QCOMPARE(dlg->typeCombo()->count(), 1);  // texte à trous : type figé
        dlg->frontEdit()->setPlainText(QStringLiteral("Le cycle de {{c1::Carnot}} est réversible."));
        QVERIFY(dlg->save());
        dlg->accept();
        QVERIFY(page.setCardSuspended(first, true));
        QTRY_COMPARE(page.cardList()->topLevelItem(0)->text(2), QStringLiteral("Suspendue"));
        QCOMPARE(page.tileValue(0)->text(), QStringLiteral("3"));

        // Renommer, supprimer.
        QVERIFY(page.renameDeck(thermo, QStringLiteral("Thermo")));
        QTRY_COMPARE(page.deckTitle()->text(), QStringLiteral("Thermo"));
        QVERIFY(page.removeDeck(thermo));
        QCOMPARE(page.deckList()->count(), 1);
        QCOMPARE(page.deckTitle()->text(), QStringLiteral("Mes cartes"));
    }

    void reviewSession()
    {
        Env env;
        StudyService study(*env.db);
        QDateTime now = QDateTime(QDate(2026, 11, 20), QTime(10, 0)).toUTC();
        study.setClock([&now] { return now; });
        const qint64 deck = study.createDeck(QStringLiteral("Thermodynamique")).value().id;
        QVERIFY(study.addCards(deck, CardType::Formula, QStringLiteral("Rendement de Carnot ?"),
                               QStringLiteral("\\eta = 1 - \\frac{T_F}{T_C}")));
        QVERIFY(study.addCards(deck, CardType::Basic, QStringLiteral("Unité de l'entropie ?"),
                               QStringLiteral("J/K")));
        ui::RevisionsPage page(*env.ctx, study);
        page.resize(1300, 860);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QCOMPARE(page.currentDeckId(), deck);
        QCOMPARE(page.startButton()->text(), QStringLiteral("Commencer la révision (2)"));

        QVERIFY(page.startSession());
        QCOMPARE(page.center()->currentWidget(), page.sessionPage());
        QVERIFY(waitCard(page.cardView(), QStringLiteral("Rendement de Carnot")));
        QVERIFY(!js(page.cardView(), QStringLiteral("document.body.innerText")).toString().contains(QStringLiteral("RÉPONSE")));
        QCOMPARE(page.progressLabel()->text(), QStringLiteral("1 / 2"));
        QVERIFY(page.showAnswerButton()->isVisible());
        QVERIFY(!page.rate(Rating::Good));  // pas avant la réponse

        // Espace : la réponse (formule KaTeX) et les intervalles des quatre réponses.
        QTest::keyClick(page.sessionPage(), Qt::Key_Space);
        QVERIFY(page.answerShown());
        QTRY_VERIFY_WITH_TIMEOUT(js(page.cardView(), QStringLiteral("!!document.querySelector('.fc-a .katex')")).toBool(),
                                 kTimeout);
        QCOMPARE(page.rateInterval(0)->text(), QStringLiteral("1 min · 1"));
        QCOMPARE(page.rateInterval(1)->text(), QStringLiteral("6 min · 2"));
        QCOMPARE(page.rateInterval(2)->text(), QStringLiteral("10 min · 3"));
        QCOMPARE(page.rateInterval(3)->text(), QStringLiteral("4 j · 4"));
        QVERIFY(page.rateButtons().at(3)->isVisible());

        // Touche 4 (Facile) : carte suivante.
        const qint64 firstCard = page.currentCardId();
        QTest::keyClick(page.sessionPage(), Qt::Key_4);
        QCOMPARE(page.sessionReviewed(), 1);
        QVERIFY(page.currentCardId() != firstCard);
        QVERIFY(waitCard(page.cardView(), QStringLiteral("Unité de l'entropie")));
        QCOMPARE(page.progressLabel()->text(), QStringLiteral("2 / 2"));

        // « À revoir » : la carte revient (en apprentissage, due dans la minute, présentée en avance).
        QVERIFY(page.revealAnswer());
        QVERIFY(page.rate(Rating::Again));
        QVERIFY(page.inSession());
        QVERIFY(page.revealAnswer());
        QVERIFY(page.rate(Rating::Easy));

        // Fin de séance : bilan, statistiques du jour.
        QVERIFY(!page.inSession());
        QCOMPARE(page.center()->currentWidget(), page.finishedPage());
        QCOMPARE(page.finishedLabel()->text(), QStringLiteral("3 réponses · 67 % de bonnes réponses"));
        QCOMPARE(page.statValue(0)->text(), QStringLiteral("3"));
        QCOMPARE(page.statValue(1)->text(), QStringLiteral("0"));
        QCOMPARE(page.statValue(2)->text(), QStringLiteral("2 / 2"));
        QCOMPARE(page.statValue(3)->text(), QStringLiteral("67 %"));
        QCOMPARE(page.chart()->values().size(), 7);
        QCOMPARE(page.chart()->values().last(), 3);
        QCOMPARE(page.deckList()->item(0)->data(Qt::UserRole + 1).toInt(), 0);

        // Quatre jours plus tard : les deux cartes sont à réviser.
        now = now.addDays(4);
        page.refresh();
        QCOMPARE(page.tileValue(2)->text(), QStringLiteral("2"));
        QVERIFY(page.startSession());
        QTest::keyClick(page.sessionPage(), Qt::Key_Escape);
        QVERIFY(!page.inSession());
        QCOMPARE(page.center()->currentWidget(), page.overviewPage());
    }

    void cardsFromReadingAndNotes()
    {
        Env env;
        const qint64 doc = env.import(m_pdf);
        QVERIFY(doc > 0);
        AnnotationRecord a;
        a.documentId = doc;
        a.unitIndex = 1;
        a.selectedText = QStringLiteral("Le rendement de Carnot ne dépend que des températures des sources.");
        PdfLocator l;
        l.page = 1;
        l.rects = {QRectF(0.1, 0.2, 0.7, 0.02)};
        l.exact = a.selectedText;
        a.locator = l.toJson();
        a.sortKey = annotationSortKey(1, 0.2);
        auto annotation = env.annotations->create(a);
        QVERIFY2(annotation, annotation ? "" : qPrintable(annotation.error().message));
        auto note = env.notes->create(QStringLiteral("Second principe"), NoteType::Personal, {},
                                      QStringLiteral("L'entropie d'un système isolé ne décroît pas."));
        QVERIFY(note);

        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QToolButton* nav = window.sidebar()->button(QStringLiteral("revisions"));
        QVERIFY(nav);
        QVERIFY(!window.sidebar()->badge(QStringLiteral("revisions"))->isVisibleTo(window.sidebar()));

        // Depuis une annotation : origine affichée, carte rattachée.
        ui::CardEditorDialog* dlg = window.createCard(int(CardSource::Annotation), annotation.value().id, QString(),
                                                      annotation.value().selectedText);
        QVERIFY(dlg->findChild<QLabel*>(QStringLiteral("CardSource"))->text().contains(QStringLiteral("p. 2")));
        dlg->frontEdit()->setPlainText(QStringLiteral("De quoi dépend le rendement de Carnot ?"));
        QVERIFY(dlg->save());
        dlg->accept();
        QCOMPARE(dlg->saved().first().sourceType, CardSource::Annotation);
        QCOMPARE(window.sidebar()->badge(QStringLiteral("revisions"))->text(), QStringLiteral("1"));
        QVERIFY(window.sidebar()->badge(QStringLiteral("revisions"))->isVisibleTo(window.sidebar()));

        // Depuis une note : titre en recto, sélection en verso.
        window.navigateTo(QStringLiteral("notes"));
        auto* notes = qobject_cast<ui::NotesPage*>(window.page(QStringLiteral("notes")));
        notes->openNote(note.value().id);
        QSignalSpy requested(notes, &ui::NotesPage::cardRequested);
        QPlainTextEdit* editor = notes->editor();
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        notes->requestCard(note.value().id);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().at(1).toString(), QStringLiteral("Second principe"));
        QCOMPARE(requested.first().at(2).toString(), QStringLiteral("L'entropie d'un système isolé ne décroît pas."));
        ui::CardEditorDialog* noteDialog = nullptr;
        for (auto* d : window.findChildren<ui::CardEditorDialog*>())
            if (d->isVisible())
                noteDialog = d;
        QVERIFY(noteDialog);
        QCOMPARE(noteDialog->frontEdit()->toPlainText(), QStringLiteral("Second principe"));
        QVERIFY(noteDialog->findChild<QLabel*>(QStringLiteral("CardSource"))->text().contains(QStringLiteral("Second principe")));
        noteDialog->reject();

        // Séance : la source mène au passage annoté.
        nav->click();
        QCOMPARE(window.currentPage(), QStringLiteral("revisions"));
        auto* page = qobject_cast<ui::RevisionsPage*>(window.page(QStringLiteral("revisions")));
        QVERIFY(page->startSession());
        QVERIFY(page->revealAnswer());
        QVERIFY(waitCard(page->cardView(), QStringLiteral("p. 2")));
        maybeScreenshot(window, QStringLiteral("v2-1-revisions"));
        QSignalSpy origin(page, &ui::RevisionsPage::openItemRequested);
        js(page->cardView(), QStringLiteral("document.querySelector('.fc-source a').click(), true"));
        QTRY_COMPARE_WITH_TIMEOUT(origin.size(), 1, kTimeout);
        QCOMPARE(origin.first().at(0).toLongLong(), doc);
        QCOMPARE(origin.first().at(2).toLongLong(), annotation.value().id);
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
    }
};

QTEST_MAIN(TstStudyUi)
#include "tst_study_ui.moc"
