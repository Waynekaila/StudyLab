#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/components/SegmentedControl.h"
#include "ui/pages/SettingsPage.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/reader/CommentsPane.h"
#include "ui/reader/MarkdownView.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SearchPalette.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDeadlineTimer>
#include <QFocusEvent>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPdfDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;
using namespace studylab::test;

namespace {

constexpr int kTimeout = 30000;

const QString kBody = QStringLiteral(
    "La réponse fréquentielle d'un système stable décrit comment il amplifie et déphase une entrée sinusoïdale. "
    "Le gain et la phase se tracent en fonction de la fréquence : c'est le diagramme de Bode, étudié au chapitre 9. "
    "Une fois les transitoires amortis, seule subsiste la réponse établie.\n\n"
    "Pour un système linéaire, la réponse à une somme de sinusoïdes est la somme des réponses : la décomposition "
    "en série de Fourier ramène tout signal périodique à ce cas.");

/// Évalue une expression dans une vue Markdown et attend le résultat.
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

QList<qint64> cardIds(ui::MarkdownView* view)
{
    QList<qint64> ids;
    for (const QVariant& v : js(view, QStringLiteral("StudyLab.cardIds()")).toList())
        ids << v.toLongLong();
    return ids;
}

}  // namespace

class TstCommentsUi : public QObject {
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
            ctx = std::make_unique<AppContext>(
                AppContext{paths, *db, *settings, *theme, *library, *annotations, *comments, *search, *backups});
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

    struct Reader {
        std::unique_ptr<ui::PdfReaderTab> tab;
        Reader(Env& env, qint64 id)
        {
            tab = std::make_unique<ui::PdfReaderTab>(*env.ctx, id);
            tab->resize(1400, 900);
            tab->show();
        }
        ui::CommentsPane* pane() const { return tab->commentsPane(); }
        ui::MarkdownView* list() const { return pane()->listView(); }
    };

    static void waitList(ui::CommentsPane* pane)
    {
        QTRY_VERIFY_WITH_TIMEOUT(pane->listView() && pane->listView()->isReady(), kTimeout);
    }

    static void write(ui::CommentsPane* pane, const QString& text)
    {
        // Saisie directe : QTest::keyClicks ne sait pas taper les lettres accentuées.
        pane->editor()->setFocus();
        pane->editor()->setPlainText(text);
        pane->editor()->moveCursor(QTextCursor::End);
    }

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(900);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    static QRectF bodyLine(QPdfDocument* doc, int page, int line)
    {
        QList<QRectF> lines;
        for (const QPolygonF& poly : doc->getAllText(page).bounds()) {
            const QRectF r = poly.boundingRect();
            if (r.height() < 3)
                continue;
            bool merged = false;
            for (QRectF& l : lines)
                if (qAbs(l.center().y() - r.center().y()) < 4) {
                    l = l.united(r);
                    merged = true;
                    break;
                }
            if (!merged)
                lines << r;
        }
        std::sort(lines.begin(), lines.end(), [](const QRectF& a, const QRectF& b) { return a.top() < b.top(); });
        return lines.value(line);
    }

    /// Sélection d'un début de ligne, terminée comme à la souris (barre flottante affichée).
    static void selectLine(ui::PdfView* view, int page, int line)
    {
        view->goToPage(page);
        QTest::qWait(50);
        const QRectF l = bodyLine(view->document(), page, line);
        const qreal y = l.center().y();
        view->selectText(page, QPointF(l.left() + 1, y), page, QPointF(l.left() + l.width() * 0.6, y));
        emit view->selectionFinished();
    }

    QTemporaryDir m_files;
    QString m_pdf;

private slots:
    void initTestCase()
    {
        QVERIFY(kWebEngineReady);
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("bode.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Réponse fréquentielle"), 6, kBody}));
    }

    void freeCommentRendersMarkdownAndMath()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        QVERIFY(id > 0);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        ui::CommentsPane* pane = r.pane();
        QVERIFY(pane->isVisible());
        QVERIFY(r.tab->sidePanel()->isVisible());
        waitList(pane);
        QCOMPARE(cardIds(r.list()).size(), 0);
        QVERIFY(js(r.list(), QStringLiteral("StudyLab.text()")).toString().contains(QStringLiteral("Aucun commentaire")));

        // Saisie Markdown + formule + lien [[…]], type Explication
        QVERIFY(!pane->submitButton()->isEnabled());
        QCOMPARE(pane->target().anchor, CommentAnchor::Free);
        QVERIFY(pane->targetLabel()->text().contains(QStringLiteral("document entier")));
        const QString body = QStringLiteral("**Gain** en dB : $20\\log_{10} M$ - lien avec [[Diagramme de Bode]]");
        write(pane, body);
        pane->kindControl()->setCurrentIndex(1);  // Question, Explication, Réflexion, Référence
        emit pane->kindControl()->currentIndexChanged(1);
        QCOMPARE(pane->kind(), CommentKind::Explanation);
        QVERIFY(pane->submitButton()->isEnabled());

        // Aperçu : KaTeX et lien rendus
        pane->modeControl()->setCurrentIndex(1);
        emit pane->modeControl()->currentIndexChanged(1);
        QTRY_VERIFY_WITH_TIMEOUT(pane->previewView() && pane->previewView()->isReady(), kTimeout);
        QTRY_VERIFY_WITH_TIMEOUT(js(pane->previewView(), QStringLiteral("StudyLab.html()")).toString().contains(QStringLiteral("katex")), kTimeout);
        const QString preview = js(pane->previewView(), QStringLiteral("StudyLab.html()")).toString();
        QVERIFY(preview.contains(QStringLiteral("<strong>Gain</strong>")));
        QVERIFY(preview.contains(QStringLiteral("class=\"wikilink\"")));
        QVERIFY(!preview.contains(QStringLiteral("$20")));
        pane->modeControl()->setCurrentIndex(0);
        emit pane->modeControl()->currentIndexChanged(0);

        // Ctrl+Entrée ajoute
        QTest::keyClick(pane->editor(), Qt::Key_Return, Qt::ControlModifier);
        auto list = env.comments->list(id).value();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().body, body);
        QCOMPARE(list.first().kind, CommentKind::Explanation);
        QCOMPARE(list.first().anchor, CommentAnchor::Free);
        QCOMPARE(r.tab->comments().size(), 1);
        QVERIFY(pane->editor()->toPlainText().isEmpty());
        QCOMPARE(pane->kind(), CommentKind::Reflection);
        QCOMPARE(pane->countLabel()->text(), QStringLiteral("1"));
        QTRY_COMPARE_WITH_TIMEOUT(cardIds(r.list()), QList<qint64>{list.first().id}, kTimeout);
        const QString card = js(r.list(), QStringLiteral("StudyLab.html()")).toString();
        QVERIFY(card.contains(QStringLiteral("katex")));
        QVERIFY(card.contains(QStringLiteral("Explication")));
        // Le HTML brut est neutralisé
        QVERIFY(js(r.list(), QStringLiteral("StudyLab.render('<img src=x onerror=alert(1)>')")).toString().contains(QStringLiteral("&lt;img")));
        // Brouillon effacé après l'ajout
        QVERIFY(env.settings->value(ui::CommentsPane::draftKey(id)).toObject().isEmpty());
    }

    void passageAnnotationAndPageComments()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        ui::CommentsPane* pane = r.pane();
        waitList(pane);

        // Passage : sélection → barre flottante → Commenter
        selectLine(r.tab->view(), 1, 1);
        QTRY_VERIFY(r.tab->popup()->isVisible());
        QVERIFY(r.tab->popup()->commentButton()->isVisible());
        QTest::mouseClick(r.tab->popup()->commentButton(), Qt::LeftButton);
        QCOMPARE(pane->target().anchor, CommentAnchor::Passage);
        QCOMPARE(pane->target().unitIndex, 1);
        QVERIFY(!pane->target().quote.isEmpty());
        QVERIFY(pane->targetLabel()->text().startsWith(QStringLiteral("Passage · p. 2")));
        QVERIFY(pane->editor()->hasFocus());
        QVERIFY(pane->detachButton()->isVisible());
        write(pane, QStringLiteral("Pourquoi la réponse établie ne dépend-elle pas des conditions initiales ?"));
        pane->kindControl()->setCurrentIndex(0);
        emit pane->kindControl()->currentIndexChanged(0);
        QTest::mouseClick(pane->submitButton(), Qt::LeftButton);
        auto list = env.comments->list(id).value();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().anchor, CommentAnchor::Passage);
        QCOMPARE(list.first().kind, CommentKind::Question);
        QCOMPARE(list.first().unitIndex, 1);
        QVERIFY(!list.first().quote.isEmpty());
        QVERIFY(PdfLocator::fromJson(list.first().locator).has_value());

        // Annotation : commentaire = note de l'annotation, visible dans les deux volets
        selectLine(r.tab->view(), 2, 1);
        auto created = r.tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Blue);
        QVERIFY(created.ok() && created.value().size() == 1);
        const qint64 annId = created.value().first().id;
        r.tab->commentAnnotation(annId);
        QCOMPARE(pane->target().anchor, CommentAnchor::Annotation);
        QCOMPARE(pane->target().annotationId, annId);
        QVERIFY(pane->targetLabel()->text().startsWith(QStringLiteral("Annotation bleue · p. 3")));
        write(pane, QStringLiteral("Même idée qu'en vibrations."));
        QTest::mouseClick(pane->submitButton(), Qt::LeftButton);
        QCOMPARE(env.annotations->find(annId).value().note, QStringLiteral("Même idée qu'en vibrations."));
        QCOMPARE(r.tab->annotations().first().note, QStringLiteral("Même idée qu'en vibrations."));
        QCOMPARE(r.tab->comments().size(), 2);
        // Commenter à nouveau l'annotation ouvre le commentaire existant
        r.tab->commentAnnotation(annId);
        QVERIFY(pane->editingId() != 0);
        QCOMPARE(pane->editor()->toPlainText(), QStringLiteral("Même idée qu'en vibrations."));
        QCOMPARE(pane->submitButton()->text(), QStringLiteral("Enregistrer"));
        pane->cancelEditing();

        // Annotation supprimée : son commentaire disparaît ; annulation : il revient
        r.tab->deleteAnnotation(annId);
        QCOMPARE(r.tab->comments().size(), 1);
        QVERIFY(r.tab->undoDelete());
        QCOMPARE(r.tab->comments().size(), 2);

        // Page courante
        r.tab->view()->goToPage(4);
        QTRY_COMPARE(r.tab->view()->currentPage(), 4);
        QVERIFY(pane->pageButton()->isVisible());
        QTest::mouseClick(pane->pageButton(), Qt::LeftButton);
        QCOMPARE(pane->target().anchor, CommentAnchor::Page);
        QCOMPARE(pane->target().unitIndex, 4);
        write(pane, QStringLiteral("Résumé de la page 5"));
        QTest::mouseClick(pane->submitButton(), Qt::LeftButton);
        list = env.comments->list(id).value();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.last().anchor, CommentAnchor::Page);
        QCOMPARE(commentLocation(list.last()), QStringLiteral("p. 5"));

        // Ordre de lecture dans le volet, clic → navigation
        QTRY_COMPARE_WITH_TIMEOUT(cardIds(r.list()).size(), 3, kTimeout);
        QCOMPARE(cardIds(r.list()), (QList<qint64>{list[0].id, list[1].id, list[2].id}));
        r.tab->view()->goToPage(0);
        QTRY_COMPARE(r.tab->view()->currentPage(), 0);
        QSignalSpy activated(pane, &ui::CommentsPane::commentActivated);
        QVERIFY(js(r.list(), QStringLiteral("StudyLab.click(%1)").arg(list[2].id)).toBool());
        QTRY_COMPARE(activated.count(), 1);
        QTRY_COMPARE(r.tab->view()->currentPage(), 4);
        QCOMPARE(pane->active(), list[2].id);
        r.tab->revealComment(list[1].id);  // commentaire d'annotation → annotation
        QTRY_COMPARE(r.tab->view()->currentPage(), 2);
        QCOMPARE(r.tab->annotationsPane()->active(), annId);

        maybeScreenshot(*r.tab, QStringLiteral("iteration6-commentaires"));
    }

    void filterEditDeleteUndo()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        ui::CommentsPane* pane = r.pane();
        waitList(pane);
        CommentRecord q;
        q.documentId = id;
        q.kind = CommentKind::Question;
        q.body = QStringLiteral("Question libre");
        const qint64 qid = env.comments->create(q).value().id;
        CommentRecord e;
        e.documentId = id;
        e.kind = CommentKind::Explanation;
        e.anchor = CommentAnchor::Page;
        e.unitIndex = 0;
        e.locator = PdfLocator{}.toJson();
        e.body = QStringLiteral("Explication page 1");
        const qint64 eid = env.comments->create(e).value().id;
        QCOMPARE(r.tab->comments().size(), 2);

        QTest::mouseClick(pane->filterButton(ui::CommentsPane::Filter::Questions), Qt::LeftButton);
        QCOMPARE(pane->visibleIds(), QList<qint64>{qid});
        QTest::mouseClick(pane->filterButton(ui::CommentsPane::Filter::Explanations), Qt::LeftButton);
        QCOMPARE(pane->visibleIds(), QList<qint64>{eid});
        QTest::mouseClick(pane->filterButton(ui::CommentsPane::Filter::CurrentUnit), Qt::LeftButton);
        QCOMPARE(pane->visibleIds(), QList<qint64>{eid});
        r.tab->view()->goToPage(3);
        QTRY_VERIFY(pane->visibleIds().isEmpty());
        QTest::mouseClick(pane->filterButton(ui::CommentsPane::Filter::All), Qt::LeftButton);
        QCOMPARE(pane->visibleIds().size(), 2);
        QTest::mouseClick(pane->searchToggle(), Qt::LeftButton);
        QVERIFY(pane->searchEdit()->isVisible());
        QTest::keyClicks(pane->searchEdit(), QStringLiteral("explic"));
        QTRY_COMPARE(pane->visibleIds(), QList<qint64>{eid});
        QTest::mouseClick(pane->searchToggle(), Qt::LeftButton);
        QTRY_COMPARE(pane->visibleIds().size(), 2);

        // Modification (double-clic dans la liste → éditeur)
        QSignalSpy edit(pane, &ui::CommentsPane::editRequested);
        js(r.list(), QStringLiteral("document.querySelector('.card[data-id=\"%1\"]').dispatchEvent(new MouseEvent('dblclick', {bubbles: true}))").arg(qid));
        QTRY_COMPARE(edit.count(), 1);
        QCOMPARE(pane->editingId(), qid);
        QCOMPARE(pane->kind(), CommentKind::Question);
        QVERIFY(pane->targetLabel()->text().startsWith(QStringLiteral("Modification")));
        QVERIFY(!pane->detachButton()->isVisible());
        write(pane, QStringLiteral("Question reformulée"));
        QTest::mouseClick(pane->submitButton(), Qt::LeftButton);
        QCOMPARE(env.comments->find(qid).value().body, QStringLiteral("Question reformulée"));
        QCOMPARE(pane->editingId(), 0);
        QCOMPARE(env.comments->count(id).value(), 2);

        // Échap annule une modification
        r.tab->editComment(eid);
        write(pane, QStringLiteral("abandon"));
        QTest::keyClick(pane->editor(), Qt::Key_Escape);
        QCOMPARE(pane->editingId(), 0);
        QCOMPARE(env.comments->find(eid).value().body, QStringLiteral("Explication page 1"));

        // Suppression puis annulation (barre du volet, puis Ctrl+Z)
        r.tab->deleteComment(qid);
        QCOMPARE(r.tab->comments().size(), 1);
        QVERIFY(pane->undoButton()->isVisible());
        QTest::mouseClick(pane->undoButton(), Qt::LeftButton);
        QCOMPARE(r.tab->comments().size(), 2);
        r.tab->deleteComment(eid);
        QCOMPARE(r.tab->comments().size(), 1);
        QVERIFY(r.tab->undoDelete());
        QCOMPARE(r.tab->comments().size(), 2);

        // Question résolue
        QVERIFY(env.comments->setResolved(qid, true).ok());
        QTRY_VERIFY_WITH_TIMEOUT(js(r.list(), QStringLiteral("StudyLab.text()")).toString().contains(QStringLiteral("Résolue")), kTimeout);
    }

    void draftSurvivesAbruptClose()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        const QString text = QStringLiteral("Brouillon : la phase $\\varphi = \\arg G(i\\omega)$");
        {
            Reader r(env, id);
            QVERIFY(r.tab->ensureLoaded().ok());
            ui::CommentsPane* pane = r.pane();
            selectLine(r.tab->view(), 1, 1);
            QTRY_VERIFY(r.tab->popup()->isVisible());
            QTest::mouseClick(r.tab->popup()->commentButton(), Qt::LeftButton);
            write(pane, text);
            // Sauvegarde 2 s après la dernière frappe : vérifiée sur une autre connexion, comme le verrait
            // l'application relancée après un arrêt brutal (rien n'est écrit à la destruction).
            QTest::qWait(2300);
            auto other = db::Database::open(env.paths.databaseFile(), db::Database::Mode::ReadOnly);
            QVERIFY(other.ok());
            auto value = other.value()->queryString(
                QStringLiteral("SELECT value FROM settings WHERE key = '%1'").arg(ui::CommentsPane::draftKey(id)));
            QVERIFY(value.ok());
            QVERIFY(value.value().contains(QStringLiteral("varphi")));
            QVERIFY(value.value().contains(QStringLiteral("\"passage\"")));
        }
        // Nouvel onglet : brouillon et cible restaurés
        Reader again(env, id);
        QCOMPARE(again.pane()->editor()->toPlainText(), text);
        QCOMPARE(again.pane()->target().anchor, CommentAnchor::Passage);
        QVERIFY(again.pane()->targetLabel()->text().startsWith(QStringLiteral("Passage · p. 2")));

        // Perte du focus : enregistrement immédiat
        again.pane()->editor()->moveCursor(QTextCursor::End);
        QTest::keyClicks(again.pane()->editor(), QStringLiteral(" !"));
        QFocusEvent focusOut(QEvent::FocusOut, Qt::TabFocusReason);
        QApplication::sendEvent(again.pane()->editor(), &focusOut);
        QTRY_VERIFY(env.settings->value(ui::CommentsPane::draftKey(id)).toObject().value(QStringLiteral("body")).toString().endsWith(QStringLiteral(" !")));

        // Annuler efface le brouillon
        QTest::mouseClick(again.pane()->cancelButton(), Qt::LeftButton);
        QVERIFY(again.pane()->editor()->toPlainText().isEmpty());
        QCOMPARE(again.pane()->target().anchor, CommentAnchor::Free);
        QVERIFY(env.settings->value(ui::CommentsPane::draftKey(id)).toObject().isEmpty());
    }

    void exportAndSearchPalette()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        ui::MainWindow window(*env.ctx);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        reader->openDocument(id);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->currentTab());
        QVERIFY(tab && tab->ensureLoaded().ok());
        window.navigateTo(QStringLiteral("reader"));

        PdfLocator loc;
        loc.page = 3;
        CommentRecord c;
        c.documentId = id;
        c.anchor = CommentAnchor::Page;
        c.unitIndex = 3;
        c.locator = loc.toJson();
        c.kind = CommentKind::Reference;
        c.body = QStringLiteral("Voir Åström & Murray, *Feedback Systems*, chapitre 9 (Bode).");
        const qint64 cid = env.comments->create(c).value().id;

        // Export : Commentaires.md à côté des annotations
        QTemporaryDir out;
        auto folder = tab->exportMarkdown(out.path());
        QVERIFY(folder.ok());
        QFile md(folder.value() + QStringLiteral("/Commentaires.md"));
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(md.readAll());
        QVERIFY2(content.contains(QStringLiteral("> [!cite] Référence · [p. 4](studylab://open?doc=")) && content.contains(QStringLiteral("&comment=")),
                 qPrintable(content));
        QVERIFY(content.contains(QStringLiteral("Feedback Systems")));

        // Rechercher partout : Ctrl+K depuis n'importe quel écran
        window.navigateTo(QStringLiteral("library"));
        QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
        ui::SearchPalette* palette = window.searchPalette();
        QTRY_VERIFY(palette->isVisible());
        QVERIFY(palette->input()->hasFocus());
        QTest::keyClicks(palette->input(), QStringLiteral("astrom bode"));
        QTRY_COMPARE(palette->results().comments.size(), 1);
        QCOMPARE(palette->results().comments.first().itemId, cid);
        QCOMPARE(palette->results().comments.first().location, QStringLiteral("p. 4"));
        QVERIFY(palette->status()->text().startsWith(QStringLiteral("1 résultat")));
        maybeScreenshot(window, QStringLiteral("iteration6-recherche"));
        tab->view()->goToPage(0);
        QTRY_COMPARE(tab->view()->currentPage(), 0);
        QTest::keyClick(palette->input(), Qt::Key_Return);
        QVERIFY(!palette->isVisible());
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        QTRY_COMPARE(tab->view()->currentPage(), 3);
        QCOMPARE(tab->commentsPane()->active(), cid);

        // Titre du document ; Échap ferme ; aucun résultat
        QTest::mouseClick(window.sidebar()->searchButton(), Qt::LeftButton);
        QTRY_VERIFY(palette->isVisible());
        palette->search(QStringLiteral("frequentielle"));
        QCOMPARE(palette->results().documents.size(), 1);
        palette->search(QStringLiteral("zzzintrouvable"));
        QVERIFY(palette->results().isEmpty());
        QVERIFY(palette->status()->text().startsWith(QStringLiteral("Aucun résultat")));
        QTest::keyClick(palette->input(), Qt::Key_Escape);
        QVERIFY(!palette->isVisible());
    }

    void settingsBackupAndRestore()
    {
        Env env;
        ui::SettingsPage page(*env.ctx);
        page.resize(1100, 800);
        page.show();
        // La sauvegarde quotidienne du démarrage est listée
        QCOMPARE(page.backupList()->count(), 1);
        QVERIFY(page.backupList()->item(0)->text().contains(QStringLiteral("Automatique")));
        QVERIFY(!page.restoreButton()->isEnabled());
        QTest::mouseClick(page.backupButton(), Qt::LeftButton);
        QCOMPARE(page.backupList()->count(), 2);
        QVERIFY(page.backupStatus()->text().startsWith(QStringLiteral("Sauvegarde créée")));
        QVERIFY(page.backupList()->item(0)->text().contains(QStringLiteral("Manuelle")));

        page.backupList()->setCurrentRow(1);
        QVERIFY(page.restoreButton()->isEnabled());
        int asked = 0;
        page.setRestoreConfirmer([&](const BackupInfo& info) {
            ++asked;
            return info.kind == BackupKind::Automatic ? 1 : 0;
        });
        QTest::mouseClick(page.restoreButton(), Qt::LeftButton);
        QCOMPARE(asked, 1);
        QVERIFY(env.backups->pendingRestore().has_value());
        page.categories()->setCurrentRow(2);  // Données
        QVERIFY(page.pendingRestoreLabel()->isVisible());
        QVERIFY(page.pendingRestoreLabel()->text().startsWith(QStringLiteral("Restauration programmée")));
        QVERIFY(!page.restoreButton()->isEnabled());
        maybeScreenshot(page, QStringLiteral("iteration6-sauvegardes"));
        QTest::mouseClick(page.cancelRestoreButton(), Qt::LeftButton);
        QVERIFY(!env.backups->pendingRestore().has_value());
        QVERIFY(!page.pendingRestoreLabel()->isVisible());
    }

    void darkScreenshot()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Captures seulement avec STUDYLAB_SCREENSHOT_DIR");
        Env env;
        env.settings->setThemeMode(ThemeMode::Dark);
        env.theme->apply();
        const qint64 id = env.import(m_pdf);
        CommentRecord c;
        c.documentId = id;
        c.kind = CommentKind::Question;
        c.body = QStringLiteral("Pourquoi la réponse établie ne dépend-elle pas des conditions initiales ? "
                                "→ parce que le système est **stable** : les termes transitoires $e^{\\lambda t}$ s'annulent.");
        QVERIFY(env.comments->create(c).ok());
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        waitList(r.pane());
        QTRY_COMPARE_WITH_TIMEOUT(cardIds(r.list()).size(), 1, kTimeout);
        write(r.pane(), QStringLiteral("**Gain** en dB : $20\\log_{10} M$\n- lien avec [[Diagramme de Bode]]"));
        maybeScreenshot(*r.tab, QStringLiteral("iteration6-commentaires-sombre"));
    }
};

QTEST_MAIN(TstCommentsUi)
#include "tst_comments_ui.moc"
