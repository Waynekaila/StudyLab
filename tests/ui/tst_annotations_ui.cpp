#include "Bootstrap.h"

#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/components/SegmentedControl.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPdfDocument>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace studylab;
using namespace studylab::test;
using Tool = ui::PdfView::Tool;

namespace {
const QString kBody = QStringLiteral(
    "Lorsque le système est isolé, aucun transfert de chaleur n'a lieu et l'intégrale est nulle. On obtient alors "
    "le résultat fondamental : l'entropie d'un système isolé ne peut que croître ou rester constante au cours "
    "d'une transformation. C'est le principe d'accroissement de l'entropie.\n\n"
    "Ce principe signifie qu'une transformation ne peut se produire que dans le sens qui respecte cette règle. "
    "La production d'entropie est une mesure des irréversibilités présentes au cours d'une transformation.\n\n"
    "Une transformation pour laquelle la production d'entropie serait négative est impossible. Une transformation "
    "réversible correspond à une production d'entropie nulle ; c'est un cas limite idéal.");
}

class TstAnnotationsUi : public QObject {
    Q_OBJECT

    struct Env {
        QTemporaryDir dir;
        AppPaths paths{dir.path()};
        std::unique_ptr<db::Database> db;
        std::unique_ptr<SettingsService> settings;
        std::unique_ptr<ui::ThemeManager> theme;
        std::unique_ptr<LibraryService> library;
        std::unique_ptr<AnnotationService> annotations;
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
            ctx = std::make_unique<AppContext>(AppContext{paths, *db, *settings, *theme, *library, *annotations});
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

    /// Onglet chargé et affiché, avec une saisie de note programmable.
    struct Reader {
        std::unique_ptr<ui::PdfReaderTab> tab;
        QString nextNote;
        int noteRequests = 0;
        Reader(Env& env, qint64 id)
        {
            tab = std::make_unique<ui::PdfReaderTab>(*env.ctx, id);
            tab->setNoteEditor([this](const QString&, const QString&) -> std::optional<QString> {
                ++noteRequests;
                return nextNote;
            });
            tab->resize(1400, 900);
            tab->show();
        }
        ui::PdfView* view() const { return tab->view(); }
    };

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(700);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    /// Lignes de texte d'une page (rectangles en points), de haut en bas : 0 = titre, 1 = première ligne du corps.
    static QList<QRectF> textLines(QPdfDocument* doc, int page)
    {
        QList<QRectF> lines;
        for (const QPolygonF& poly : doc->getAllText(page).bounds()) {
            const QRectF r = poly.boundingRect();
            if (r.height() < 3)
                continue;  // ponctuation basse
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
        return lines;
    }

    /// Sélectionne le début d'une ligne de corps de la page (de x0 à x1 en fraction de la ligne).
    static void selectLine(ui::PdfView* view, int page, int line = 1, qreal x0 = 0.0, qreal x1 = 0.6)
    {
        const QRectF l = textLines(view->document(), page).value(line);
        const qreal y = l.center().y();
        view->selectText(page, QPointF(l.left() + l.width() * x0 + 1, y), page, QPointF(l.left() + l.width() * x1, y));
    }
    static QPointF linePoint(ui::PdfView* view, int page, int line, qreal x)
    {
        const QRectF l = textLines(view->document(), page).value(line);
        return {l.left() + l.width() * x + 1, l.center().y()};
    }
    static void selectFirstSentence(ui::PdfView* view, int page) { selectLine(view, page, 1); }

    QTemporaryDir m_files;
    QString m_pdf;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        m_pdf = m_files.filePath(QStringLiteral("entropie.pdf"));
        QVERIFY(writePdf(m_pdf, {QStringLiteral("Le principe d'accroissement de l'entropie"), 8, kBody}));
    }

    void highlightAppearsInPaneImmediately()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        QVERIFY(r.tab->annotationsPane()->isVisible());
        QCOMPARE(r.tab->annotationsPane()->visibleAnnotationCount(), 0);

        selectFirstSentence(r.view(), 0);
        QVERIFY(r.view()->hasSelection());
        const QString text = r.view()->selectedText();
        QVERIFY2(text.contains(QStringLiteral("système")), qPrintable(text));

        // Critère du MVP : création → ligne visible dans le volet en moins de 50 ms
        QElapsedTimer t;
        t.start();
        auto created = r.tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Green);
        const qint64 ms = t.elapsed();
        QVERIFY(created.ok());
        QCOMPARE(created.value().size(), 1);
        QCOMPARE(r.tab->annotationsPane()->visibleAnnotationCount(), 1);
        qInfo() << "Création → volet :" << ms << "ms";
        QVERIFY2(ms < 50, "création → volet > 50 ms");

        const AnnotationRecord a = created.value().first();
        QCOMPARE(a.color, AnnotationColor::Green);
        QCOMPARE(a.unitIndex, 0);
        QCOMPARE(r.tab->annotationsPane()->active(), a.id);  // dernière créée mise en évidence
        QVERIFY(!r.view()->hasSelection());
        // Ancre : rectangles normalisés, texte cité et contexte
        const auto loc = PdfLocator::fromJson(a.locator).value();
        QVERIFY(!loc.rects.isEmpty());
        for (const QRectF& rect : loc.rects)
            QVERIFY(rect.left() >= 0 && rect.right() <= 1 && rect.top() >= 0 && rect.bottom() <= 1);
        QCOMPARE(loc.exact, normalizeQuote(text));
        QVERIFY(loc.start >= 0);
        QVERIFY(!loc.suffix.isEmpty());
        // La vue dessine l'annotation ; un clic dessus la reconnaît
        QCOMPARE(r.view()->annotations().size(), 1);
        const QRect vr = r.view()->annotationViewportRect(a.id);
        QVERIFY(!vr.isEmpty());
        QCOMPARE(r.view()->annotationAt(vr.center()), a.id);
        // Miniature marquée d'une pastille
        QVERIFY(r.tab->thumbnails()->model()->index(0, 0).data(Qt::UserRole + 3).isValid());
    }

    void mouseWorkflowWithPopupAndTools()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        QVERIFY(QTest::qWaitForWindowExposed(r.tab.get()));
        auto* view = r.view();
        auto* vp = view->viewport();

        // Sélection à la souris → barre flottante → pastille rose
        const QPoint a = view->mapFromPage(0, linePoint(view, 0, 1, 0.0));
        const QPoint b = view->mapFromPage(0, linePoint(view, 0, 1, 0.6));
        QTest::mousePress(vp, Qt::LeftButton, {}, a);
        QTest::mouseMove(vp, b);
        QTest::mouseRelease(vp, Qt::LeftButton, {}, b);
        QVERIFY(view->hasSelection());
        QTRY_VERIFY(r.tab->popup()->isVisible());
        QCOMPARE(r.tab->popup()->mode(), ui::AnnotationPopup::Mode::Create);
        QTest::mouseClick(r.tab->popup()->colorButton(AnnotationColor::Pink), Qt::LeftButton);
        QVERIFY(!r.tab->popup()->isVisible());
        QCOMPARE(r.tab->annotations().size(), 1);
        QCOMPARE(r.tab->annotations().first().color, AnnotationColor::Pink);
        QCOMPARE(r.tab->currentColor(), AnnotationColor::Pink);  // la couleur choisie devient la couleur courante

        // Outil Souligner : la sélection est annotée directement, sans barre flottante
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Underline), Qt::LeftButton);
        QCOMPARE(view->tool(), Tool::Underline);
        view->goToPage(1);
        const QPoint c2 = view->mapFromPage(1, linePoint(view, 1, 1, 0.0));
        const QPoint d2 = view->mapFromPage(1, linePoint(view, 1, 1, 0.6));
        QTest::mousePress(vp, Qt::LeftButton, {}, c2);
        QTest::mouseMove(vp, d2);
        QTest::mouseRelease(vp, Qt::LeftButton, {}, d2);
        QCOMPARE(r.tab->annotations().size(), 2);
        QCOMPARE(r.tab->annotations().last().type, AnnotationType::Underline);
        QCOMPARE(r.tab->annotations().last().color, AnnotationColor::Pink);
        QVERIFY(!r.tab->popup()->isVisible());
        // Un second clic sur l'outil revient à la sélection simple ; Échap aussi
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Underline), Qt::LeftButton);
        QCOMPARE(view->tool(), Tool::Select);
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Strikeout), Qt::LeftButton);
        QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(view->tool(), Tool::Select);
        QVERIFY(!r.tab->toolButtonFor(Tool::Strikeout)->isChecked());

        // Zone : rectangle tracé à la souris
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Marker), Qt::LeftButton);
        const QPoint z1 = view->mapFromPage(1, QPointF(40, 150));
        const QPoint z2 = view->mapFromPage(1, QPointF(200, 260));
        QTest::mousePress(vp, Qt::LeftButton, {}, z1);
        QTest::mouseMove(vp, z2);
        QTest::mouseRelease(vp, Qt::LeftButton, {}, z2);
        QCOMPARE(r.tab->annotations().size(), 3);
        const AnnotationRecord zone = r.tab->annotations().last();
        QCOMPARE(zone.type, AnnotationType::Marker);
        const QRectF zr = PdfLocator::fromJson(zone.locator)->rects.first();
        const QSizeF pts = view->document()->pagePointSize(1);
        QVERIFY(qAbs(zr.left() * pts.width() - 40) < 2 && qAbs(zr.bottom() * pts.height() - 260) < 2);

        // Note : clic avec l'outil Note, saisie, retour à la sélection
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Note), Qt::LeftButton);
        r.nextNote = QStringLiteral("Pourquoi l'intégrale est-elle nulle ?");
        QTest::mouseClick(vp, Qt::LeftButton, {}, view->mapFromPage(1, QPointF(320, 300)));
        QCOMPARE(r.noteRequests, 1);
        QCOMPARE(view->tool(), Tool::Select);
        const AnnotationRecord note = r.tab->annotations().last();
        QCOMPARE(note.type, AnnotationType::Note);
        QCOMPARE(note.note, r.nextNote);
        // Note annulée : rien n'est créé
        QTest::mouseClick(r.tab->toolButtonFor(Tool::Note), Qt::LeftButton);
        r.nextNote.clear();
        QTest::mouseClick(vp, Qt::LeftButton, {}, view->mapFromPage(1, QPointF(320, 340)));
        QCOMPARE(r.tab->annotations().size(), 4);

        // Signet : bascule sur la page courante
        QVERIFY(!r.tab->bookmarkButton()->isChecked());
        QTest::mouseClick(r.tab->bookmarkButton(), Qt::LeftButton);
        QVERIFY(r.tab->bookmarkButton()->isChecked());
        QCOMPARE(r.tab->annotations().size(), 5);
        {
            // Le ruban du signet est dessiné en haut à droite de la page
            qint64 bm = 0;
            for (const auto& ann : r.tab->annotations())
                if (ann.type == AnnotationType::Bookmark)
                    bm = ann.id;
            const QRect ribbon = view->annotationViewportRect(bm);
            QVERIFY(!ribbon.isEmpty());
            QCOMPARE(view->annotationAt(ribbon.center()), bm);
            const QImage shot = vp->grab().toImage();
            const QColor px = shot.pixelColor(ribbon.center() * shot.devicePixelRatio());
            QVERIFY2(px.blue() > px.red() + 30, qPrintable(px.name()));
        }
        QTest::keyClick(view, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(!r.tab->bookmarkButton()->isChecked());
        QCOMPARE(r.tab->annotations().size(), 4);
    }

    void editDeleteAndUndo()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        QVERIFY(QTest::qWaitForWindowExposed(r.tab.get()));
        auto* view = r.view();
        selectFirstSentence(view, 0);
        const AnnotationRecord a = r.tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Yellow).value().first();

        // Clic sur l'annotation → barre d'édition
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->annotationViewportRect(a.id).center());
        QTRY_VERIFY(r.tab->popup()->isVisible());
        QCOMPARE(r.tab->popup()->mode(), ui::AnnotationPopup::Mode::Edit);
        QVERIFY(r.tab->popup()->colorButton(AnnotationColor::Yellow)->isChecked());
        QTest::mouseClick(r.tab->popup()->typeButton(AnnotationType::Strikeout), Qt::LeftButton);
        QCOMPARE(env.annotations->find(a.id).value().type, AnnotationType::Strikeout);
        QTest::mouseClick(r.tab->popup()->colorButton(AnnotationColor::Blue), Qt::LeftButton);
        QCOMPARE(env.annotations->find(a.id).value().color, AnnotationColor::Blue);
        r.nextNote = QStringLiteral("Lister les 4 sources d'irréversibilité.");
        QTest::mouseClick(r.tab->popup()->noteButton(), Qt::LeftButton);
        QCOMPARE(env.annotations->find(a.id).value().note, r.nextNote);
        QCOMPARE(r.tab->annotations().first().note, r.nextNote);  // vue et volet à jour

        // Suppression puis annulation par la barre du volet
        r.tab->deleteAnnotation(a.id);
        QCOMPARE(r.tab->annotations().size(), 0);
        QCOMPARE(view->annotations().size(), 0);
        QVERIFY(r.tab->annotationsPane()->undoButton()->isVisible());
        QTest::mouseClick(r.tab->annotationsPane()->undoButton(), Qt::LeftButton);
        QCOMPARE(r.tab->annotations().size(), 1);
        QCOMPARE(r.tab->annotations().first().note, r.nextNote);  // la note revient avec l'annotation
        QVERIFY(!r.tab->annotationsPane()->undoButton()->isVisible());
        // … et par Ctrl+Z
        r.tab->deleteAnnotation(a.id);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(r.tab->annotations().size(), 1);
        QVERIFY(!r.tab->undoDelete());  // pile vide
    }

    void anchorsSurviveZoomRotationAndRestart()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        qint64 annId = 0;
        QRectF relative;  // position relative à la page affichée
        {
            Reader r(env, id);
            QVERIFY(r.tab->ensureLoaded().ok());
            selectFirstSentence(r.view(), 0);
            annId = r.tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Purple).value().first().id;
            const auto rel = [&](ui::PdfView* v) {
                const QRectF page = v->pageRect(0).translated(-v->pageRect(0).topLeft());
                QRectF ann = QRectF(v->annotationViewportRect(annId));
                const QPoint origin = v->mapFromPage(0, QPointF(0, 0));
                ann.translate(-origin);
                return QRectF(ann.x() / page.width(), ann.y() / page.height(), ann.width() / page.width(),
                              ann.height() / page.height());
            };
            relative = rel(r.view());
            r.view()->setZoom(2.0);
            r.view()->goToPage(0);
            const QRectF atTwo = rel(r.view());
            QVERIFY(qAbs(atTwo.x() - relative.x()) < 0.01 && qAbs(atTwo.y() - relative.y()) < 0.01);
            QVERIFY(qAbs(atTwo.width() - relative.width()) < 0.01);
            // Rotation de 180° : la zone passe à l'opposé de la page
            r.view()->setRotation(180);
            r.view()->goToPage(0);
            const QRect rotated = r.view()->annotationViewportRect(annId);
            const QPointF hit = r.view()->mapToPage(0, rotated.center());
            const QRectF stored = PdfLocator::fromJson(env.annotations->find(annId).value().locator)->rects.first();
            const QSizeF pts = r.view()->document()->pagePointSize(0);
            QVERIFY(qAbs(hit.y() / pts.height() - stored.center().y()) < 0.02);
            QCOMPARE(r.view()->annotationAt(rotated.center()), annId);
            // Fenêtre plus étroite (ajuster à la largeur) : toujours au même endroit relatif
            r.view()->setRotation(0);
            r.view()->setZoomMode(ui::PdfView::ZoomMode::FitWidth);
            r.tab->resize(900, 700);
            QCoreApplication::processEvents();
            r.view()->goToPage(0);
            const QRectF narrow = rel(r.view());
            QVERIFY(qAbs(narrow.x() - relative.x()) < 0.01 && qAbs(narrow.y() - relative.y()) < 0.01);
        }
        // Redémarrage : l'annotation est relue et dessinée au même endroit
        Reader again(env, id);
        QVERIFY(again.tab->ensureLoaded().ok());
        QCOMPARE(again.tab->annotations().size(), 1);
        QCOMPARE(again.tab->annotations().first().id, annId);
        QCOMPARE(again.view()->annotations().size(), 1);
        QCOMPARE(again.view()->annotations().first().stroke.isValid(), true);
    }

    void paneFiltersAndNavigation()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        QVERIFY(QTest::qWaitForWindowExposed(r.tab.get()));
        const AnnotationColor colors[] = {AnnotationColor::Yellow, AnnotationColor::Yellow, AnnotationColor::Green,
                                          AnnotationColor::Blue, AnnotationColor::Yellow};
        for (int page = 0; page < 5; ++page) {
            selectFirstSentence(r.view(), page);
            QVERIFY(r.tab->annotateSelection(page == 3 ? AnnotationType::Underline : AnnotationType::Highlight,
                                             colors[page]).ok());
        }
        r.nextNote = QStringLiteral("idée clé");
        r.tab->editNote(r.tab->annotations()[2].id);
        auto* pane = r.tab->annotationsPane();
        QCOMPARE(pane->visibleAnnotationCount(), 5);
        QCOMPARE(pane->countLabel()->text(), QStringLiteral("5"));
        QCOMPARE(pane->colorFilter(AnnotationColor::Yellow)->text(), QStringLiteral("3"));
        QCOMPARE(pane->colorFilter(AnnotationColor::Pink)->text(), QStringLiteral("0"));

        // Couleur
        QTest::mouseClick(pane->colorFilter(AnnotationColor::Yellow), Qt::LeftButton);
        QCOMPARE(pane->visibleAnnotationCount(), 3);
        QTest::mouseClick(pane->colorFilter(std::nullopt), Qt::LeftButton);
        QCOMPARE(pane->visibleAnnotationCount(), 5);
        // Type
        pane->typeFilter()->setCurrentIndex(pane->typeFilter()->findData(QStringLiteral("underline")));
        QCOMPARE(pane->visibleAnnotationCount(), 1);
        pane->typeFilter()->setCurrentIndex(0);
        // Recherche (accents ignorés), dans le texte et les notes
        pane->searchEdit()->setText(QStringLiteral("idee"));
        QTRY_COMPARE(pane->visibleAnnotationCount(), 1);
        pane->searchEdit()->setText(QStringLiteral("systeme isole"));
        QTRY_COMPARE(pane->visibleAnnotationCount(), 5);
        pane->searchEdit()->clear();
        QTRY_COMPARE(pane->visibleAnnotationCount(), 5);
        // Portée : page courante
        r.view()->goToPage(2);
        pane->scope()->setCurrentIndex(1);
        emit pane->scope()->currentIndexChanged(1);
        QCOMPARE(pane->visibleAnnotationCount(), 1);
        pane->scope()->setCurrentIndex(0);
        emit pane->scope()->currentIndexChanged(0);
        // Groupes : sans sommaire, un titre par page (5 titres + 5 annotations)
        QCOMPARE(pane->model()->rowCount(), 10);
        QCOMPARE(pane->model()->index(0, 0).data().toString(), QStringLiteral("Page 1"));

        // Clic dans le volet → le lecteur va au passage
        r.view()->goToPage(0);
        const qint64 target = r.tab->annotations()[4].id;
        const QModelIndex idx = pane->model()->index(pane->model()->rowOf(target), 0);
        pane->list()->scrollTo(idx);
        QTest::mouseClick(pane->list()->viewport(), Qt::LeftButton, {}, pane->list()->visualRect(idx).center());
        QCOMPARE(r.view()->currentPage(), 4);
        QCOMPARE(pane->active(), target);

        // Masquer le volet : réglage mémorisé
        QTest::mouseClick(r.tab->paneToggle(), Qt::LeftButton);
        QVERIFY(!pane->isVisible());
        QCOMPARE(env.settings->value(QStringLiteral("reader/annotationsPane")).toBool(), false);
    }

    void exportDocumentMarkdown()
    {
        Env env;
        const qint64 id = env.import(m_pdf);
        Reader r(env, id);
        QVERIFY(r.tab->ensureLoaded().ok());
        selectFirstSentence(r.view(), 0);
        QVERIFY(r.tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Yellow).ok());
        r.tab->toggleBookmark();
        QTemporaryDir out;
        auto folder = r.tab->exportMarkdown(out.path());
        QVERIFY2(folder.ok(), folder.ok() ? "" : qPrintable(folder.error().message));
        QFile md(QDir(folder.value()).filePath(QStringLiteral("Annotations.md")));
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(md.readAll());
        QVERIFY2(text.contains(QStringLiteral("> [!quote] p. 1 · Surlignage · jaune")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("## Signets")));
        QVERIFY(QFile::exists(QDir(folder.value()).filePath(QStringLiteral("metadata.json"))));
    }

    void screenshots()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Captures non demandées (STUDYLAB_SCREENSHOT_DIR)");
        Env env;
        const qint64 id = env.import(m_pdf);
        env.settings->setValue(QStringLiteral("ui/lastPage"), QStringLiteral("reader"));
        ui::MainWindow window(*env.ctx);
        window.resize(1440, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        reader->openDocument(id);
        QTRY_VERIFY(reader->currentTab()->isLoaded());
        auto* tab = reader->currentTab();
        tab->setNoteEditor([](const QString&, const QString&) { return std::optional<QString>(); });
        auto* view = tab->view();
        view->goToPage(0);
        // Quelques annotations réalistes
        const auto mark = [&](QPointF a, QPointF b, AnnotationType t, AnnotationColor c, const QString& note = {}) {
            view->selectText(0, a, 0, b);
            auto res = tab->annotateSelection(t, c);
            if (res && !note.isEmpty())
                (void)env.annotations->setNote(res.value().first().id, note);
        };
        const auto lines = textLines(view->document(), 0);
        const auto at = [&](int line, qreal x) {
            const QRectF l = lines.value(line);
            return QPointF(l.left() + l.width() * x + 1, l.center().y());
        };
        mark(at(2, 0.55), at(4, 0.62), AnnotationType::Highlight, AnnotationColor::Yellow);
        mark(at(6, 0.0), at(7, 0.7), AnnotationType::Underline, AnnotationColor::Green,
             QStringLiteral("Lister les 4 sources d'irréversibilité pour l'examen."));
        mark(at(8, 0.0), at(8, 0.99), AnnotationType::Highlight, AnnotationColor::Blue);
        (void)tab->addNote(0, QPointF(0.9, 0.2), QStringLiteral("Pourquoi l'intégrale est-elle nulle ?"));
        tab->toggleBookmark();
        // Barre flottante sur une nouvelle sélection
        view->selectText(0, at(9, 0.0), 0, at(10, 0.5));
        emit view->selectionFinished();
        maybeScreenshot(window, QStringLiteral("iteration4-annotations"));
        tab->popup()->hide();
        view->clearSelection();
        env.settings->setThemeMode(ThemeMode::Dark);
        tab->annotationsPane()->setActive(tab->annotations()[1].id);
        maybeScreenshot(window, QStringLiteral("iteration4-annotations-sombre"));
    }
};

QTEST_MAIN(TstAnnotationsUi)
#include "tst_annotations_ui.moc"
