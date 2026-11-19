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
#include "support/Fixtures.h"
#include "ui/components/SegmentedControl.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/reader/EpubReaderTab.h"
#include "ui/reader/EpubView.h"
#include "ui/reader/EpubWeb.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/theme/ThemeManager.h"

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

using namespace studylab;
using namespace studylab::test;

namespace {

constexpr int kTimeout = 30000;

QString paragraph(const QString& sentence, int repeat)
{
    QStringList parts;
    for (int i = 0; i < repeat; ++i)
        parts << sentence;
    return QStringLiteral("<p>%1</p>").arg(parts.join(QLatin1Char(' ')));
}

QString chapter(const QString& title, const QString& sentence, int paragraphs)
{
    QString body = QStringLiteral("<h1>%1</h1>").arg(title);
    for (int i = 0; i < paragraphs; ++i)
        body += paragraph(sentence, 8);
    return body;
}

QVariant js(ui::EpubView* view, const QString& expression)
{
    QVariant result;
    bool done = false;
    view->evaluate(expression, [&](const QVariant& v) {
        result = v;
        done = true;
    });
    QElapsedTimer t;
    t.start();
    while (!done && t.elapsed() < kTimeout)
        QTest::qWait(10);
    return result;
}

/// Proportion de pixels de la vue proches d'une couleur donnée.
double share(const QImage& img, const QColor& c)
{
    if (img.isNull())
        return 0;
    int n = 0, total = 0;
    for (int y = 0; y < img.height(); y += 7)
        for (int x = 0; x < img.width(); x += 7) {
            const QColor p = img.pixelColor(x, y);
            ++total;
            if (qAbs(p.red() - c.red()) + qAbs(p.green() - c.green()) + qAbs(p.blue() - c.blue()) < 12)
                ++n;
        }
    return total ? double(n) / total : 0;
}

}  // namespace

class TstEpubReader : public QObject {
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
            ctx = std::make_unique<AppContext>(AppContext{paths, *db, *settings, *theme, *library, *annotations, *comments, *search, *backups, *notes, *organize});
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

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(800);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    /// Ouvre un document dans un ReaderPage affiché et attend que le livre soit rendu.
    static ui::EpubReaderTab* openTab(ui::ReaderPage& page, qint64 id)
    {
        page.openDocument(id);
        auto* tab = qobject_cast<ui::EpubReaderTab*>(page.currentTab());
        if (!tab)
            return nullptr;
        QElapsedTimer t;
        t.start();
        while (!(tab->isOpened() && !tab->view()->location().cfi.isEmpty()) && t.elapsed() < kTimeout)
            QTest::qWait(20);
        return tab->isOpened() ? tab : nullptr;
    }

    QTemporaryDir m_files;
    QString m_epub3;
    QString m_epub2;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("StudyLabTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        qRegisterMetaType<ImportReport>();
        QVERIFY(kWebEngineReady);

        EpubSpec spec;
        spec.title = QStringLiteral("Thermodynamique en poche");
        spec.authors = {QStringLiteral("Sadi Carnot")};
        spec.chapters = {
            chapter(QStringLiteral("Premier principe"),
                    QStringLiteral("L'énergie d'un système isolé se conserve au cours de ses transformations."), 14),
            chapter(QStringLiteral("Second principe"),
                    QStringLiteral("L'entropie d'un système isolé ne peut que croître au fil du temps."), 14)
                + QStringLiteral("<script>document.documentElement.setAttribute('data-script', 'exécuté')</script>"
                                 "<p><img src=\"x\" onerror=\"document.documentElement.setAttribute('data-inline', '1')\"/></p>"),
            chapter(QStringLiteral("Machines thermiques"),
                    QStringLiteral("Le rendement d'une machine de Carnot dépend des deux températures."), 14),
        };
        spec.tocTitles = {QStringLiteral("1 · Premier principe"), QStringLiteral("2 · Second principe"),
                          QStringLiteral("3 · Machines thermiques")};
        m_epub3 = m_files.filePath(QStringLiteral("poche3.epub"));
        QVERIFY(writeEpub(m_epub3, spec));
        spec.epub3 = false;
        spec.title = QStringLiteral("Thermodynamique EPUB 2");
        spec.isbn = QStringLiteral("9782100000000");
        m_epub2 = m_files.filePath(QStringLiteral("poche2.epub"));
        QVERIFY(writeEpub(m_epub2, spec));
    }

    void opensEpub3WithTocAndText()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        QVERIFY(id > 0);
        ui::ReaderPage page(*env.ctx);
        page.resize(1400, 900);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY2(tab, "le livre ne s'est pas ouvert");
        QCOMPARE(tab->view()->toc().size(), 3);
        QCOMPARE(tab->tocView()->model()->rowCount(), 3);
        QCOMPARE(tab->view()->sectionCount(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(tab->chapterLabel()->text(), QStringLiteral("1 · Premier principe"), kTimeout);
        QVERIFY(tab->progressLabel()->text().contains(QStringLiteral("%")));
        QVERIFY(tab->pageLabel()->text().startsWith(QStringLiteral("Page 1 sur ")));
        // Du texte est dessiné : la vue n'est pas uniformément blanche.
        QTRY_VERIFY_WITH_TIMEOUT(share(tab->view()->grab().toImage(), Qt::white) < 0.985, kTimeout);
        const QString text = js(tab->view(), QStringLiteral("StudyLab.selectedText() + document.title")).toString();
        QCOMPARE(text, QStringLiteral("StudyLab"));
    }

    void opensEpub2WithNcx()
    {
        Env env;
        const qint64 id = env.import(m_epub2);
        QVERIFY(id > 0);
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY2(tab, "le livre EPUB 2 ne s'est pas ouvert");
        QCOMPARE(tab->view()->toc().size(), 3);
        QCOMPARE(tab->view()->toc().at(1).label, QStringLiteral("2 · Second principe"));
        QCOMPARE(tab->view()->toc().at(1).section, 1);
    }

    void bookScriptsAreBlocked()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        tab->view()->goTo(tab->view()->toc().at(1).href);
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, 1, kTimeout);
        QTest::qWait(300);
        const QVariant attrs = js(tab->view(),
                                  QStringLiteral("(() => { const v = document.querySelector('foliate-view');"
                                                 " const d = v.renderer.getContents()[0].doc.documentElement;"
                                                 " return [d.getAttribute('data-script'), d.getAttribute('data-inline')] })()"));
        QCOMPARE(attrs.toList().size(), 2);
        QVERIFY(attrs.toList().at(0).isNull());
        QVERIFY(attrs.toList().at(1).isNull());
    }

    void navigationAndPositionRestoredExactly()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        QString savedCfi;
        {
            ui::ReaderPage page(*env.ctx);
            page.resize(1200, 800);
            page.show();
            auto* tab = openTab(page, id);
            QVERIFY(tab);
            // Sommaire → chapitre 2, puis deux pages plus loin
            const QModelIndex second = tab->tocView()->model()->index(1, 0);
            emit tab->tocView()->activated(second);
            QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, 1, kTimeout);
            QTRY_COMPARE_WITH_TIMEOUT(tab->chapterLabel()->text(), QStringLiteral("2 · Second principe"), kTimeout);
            QCOMPARE(tab->tocView()->currentIndex().row(), 1);
            const QString first = tab->view()->location().cfi;
            tab->view()->next();
            QTRY_VERIFY_WITH_TIMEOUT(tab->view()->location().cfi != first, kTimeout);
            QVERIFY(tab->pageLabel()->text().startsWith(QStringLiteral("Page 2 sur ")));
            QTest::qWait(200);
            savedCfi = tab->view()->location().cfi;
            QVERIFY(savedCfi.startsWith(QStringLiteral("epubcfi(")));
            page.closeTab(0);  // enregistre la position
        }
        const auto pos = env.library->readingPosition(id);
        QVERIFY(pos.has_value());
        QCOMPARE(pos->locator.value(QStringLiteral("cfi")).toString(), savedCfi);
        QVERIFY(pos->progress > 0.3 && pos->progress < 0.7);
        // Réouverture : même CFI, donc même caractère en tête de page
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().cfi, savedCfi, kTimeout);
        QCOMPARE(tab->view()->location().section, 1);
    }

    void displaySettingsApplyAndPersist()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        QTest::qWait(300);

        // Thème sépia : le fond de la page prend la couleur du thème
        tab->typographyButton()->click();
        QTRY_VERIFY(tab->typographyPanel()->isVisible());
        tab->typographyPanel()->themeButton(ui::ReadingTheme::Sepia)->click();
        QCOMPARE(tab->displaySettings().theme, ui::ReadingTheme::Sepia);
        const QColor sepia = ui::readingColors(ui::ReadingTheme::Sepia).bg;
        QTRY_VERIFY_WITH_TIMEOUT(share(tab->view()->grab().toImage(), sepia) > 0.6, kTimeout);

        // Défilement continu : plus de numéro de page
        tab->typographyPanel()->layoutMode()->setCurrentIndex(1);
        emit tab->typographyPanel()->layoutMode()->currentIndexChanged(1);
        QVERIFY(tab->displaySettings().scrolled);
        QTRY_VERIFY_WITH_TIMEOUT(tab->pageLabel()->text().isEmpty(), kTimeout);

        // Taille : le chapitre compte davantage de pages en mode paginé
        tab->typographyPanel()->layoutMode()->setCurrentIndex(0);
        emit tab->typographyPanel()->layoutMode()->currentIndexChanged(0);
        QTRY_VERIFY_WITH_TIMEOUT(!tab->pageLabel()->text().isEmpty(), kTimeout);
        const auto pagesOf = [&] {
            const QString t = tab->pageLabel()->text();
            return t.section(QStringLiteral(" sur "), 1).section(QLatin1Char(' '), 0, 0).toInt();
        };
        QTest::qWait(300);
        const int before = pagesOf();
        tab->typographyPanel()->sizeSlider()->setValue(18);
        QCOMPARE(tab->displaySettings().fontSize, 180);
        QTRY_VERIFY_WITH_TIMEOUT(pagesOf() > before, kTimeout);
        tab->typographyPanel()->hide();

        // Réglages enregistrés pour les prochains livres
        const auto stored = ui::EpubSettings::fromStored(env.settings->value(QStringLiteral("epub/display")).toObject());
        QCOMPARE(stored.theme, ui::ReadingTheme::Sepia);
        QCOMPARE(stored.fontSize, 180);
        QVERIFY(!stored.scrolled);
    }

    void searchFindsAndNavigates()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        tab->showSearch();
        QTest::keyClicks(tab->searchEdit(), QStringLiteral("rendement"));
        QTest::keyClick(tab->searchEdit(), Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(tab->view()->searchDone() && tab->view()->searchResultCount() > 0, kTimeout);
        QCOMPARE(tab->view()->searchResultCount(), 14 * 8);
        tab->findNext();
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, 2, kTimeout);
        QCOMPARE(tab->searchCountLabel()->text(), QStringLiteral("1 sur 112"));
        // Recherche insensible aux accents et à la casse
        tab->searchEdit()->setText(QStringLiteral("ENERGIE"));
        QTRY_VERIFY_WITH_TIMEOUT(tab->view()->searchDone() && tab->view()->searchResultCount() == 14 * 8, kTimeout);
    }

    void annotationsCreateDrawAndSurviveReopenAndFontChange()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        QString cfi;
        {
            ui::ReaderPage page(*env.ctx);
            page.resize(1400, 900);
            page.show();
            auto* tab = openTab(page, id);
            QVERIFY(tab);
            QTest::qWait(300);

            // Sélection → barre flottante → couleur verte
            QSignalSpy sel(tab->view(), &ui::EpubView::selectionMade);
            QVERIFY(js(tab->view(), QStringLiteral("StudyLab.selectText('système isolé se conserve')")).toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!sel.isEmpty(), kTimeout);
            sel.clear();
            QTRY_VERIFY(tab->popup()->isVisible());
            QElapsedTimer timer;
            timer.start();
            tab->popup()->colorButton(AnnotationColor::Green)->click();
            QCOMPARE(tab->annotations().size(), 1);
            QVERIFY(timer.elapsed() < 100);
            const AnnotationRecord a = tab->annotations().first();
            QCOMPARE(a.type, AnnotationType::Highlight);
            QCOMPARE(a.color, AnnotationColor::Green);
            QCOMPARE(a.selectedText, QStringLiteral("système isolé se conserve"));
            const auto loc = EpubLocator::fromJson(a.locator);
            QVERIFY(loc.has_value());
            QVERIFY(loc->cfi.startsWith(QStringLiteral("epubcfi(")));
            QCOMPARE(loc->section, 0);
            QCOMPARE(loc->exact, QStringLiteral("système isolé se conserve"));
            QVERIFY(!loc->prefix.isEmpty() && !loc->suffix.isEmpty());
            QCOMPARE(annotationLocation(a).right(1), QStringLiteral("%"));
            cfi = loc->cfi;
            QCOMPARE(tab->annotationsPane()->visibleAnnotationCount(), 1);
            QCOMPARE(tab->chapterOf(a.unitIndex), QStringLiteral("1 · Premier principe"));
            QTRY_VERIFY_WITH_TIMEOUT(js(tab->view(), QStringLiteral("StudyLab.state().drawn")).toStringList().contains(cfi), kTimeout);

            // Outil Souligner actif : la sélection est annotée directement
            tab->toolButtonFor(ui::EpubReaderTab::Tool::Underline)->click();
            QVERIFY(js(tab->view(), QStringLiteral("StudyLab.selectText('au cours de ses transformations')")).toBool());
            QTRY_COMPARE_WITH_TIMEOUT(tab->annotations().size(), 2, kTimeout);
            QCOMPARE(tab->annotations().last().type == AnnotationType::Underline
                         || tab->annotations().first().type == AnnotationType::Underline,
                     true);
            tab->setTool(ui::EpubReaderTab::Tool::Select);

            // Signet sur la vue courante, puis retrait
            tab->toggleBookmark();
            QTRY_VERIFY_WITH_TIMEOUT(tab->visibleBookmark() != 0, kTimeout);
            QTRY_VERIFY(tab->bookmarkButton()->isChecked());
            QCOMPARE(tab->annotations().size(), 3);
            tab->toggleBookmark();
            QTRY_COMPARE(tab->annotations().size(), 2);
            QVERIFY(!tab->bookmarkButton()->isChecked());

            // Suppression annulable
            const qint64 underline = tab->annotations().last().id;
            tab->deleteAnnotation(underline);
            QCOMPARE(tab->annotations().size(), 1);
            QVERIFY(tab->undoDelete());
            QCOMPARE(tab->annotations().size(), 2);
        }
        // Réouverture : les annotations sont redessinées au même endroit
        ui::ReaderPage page(*env.ctx);
        page.resize(1400, 900);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        QCOMPARE(tab->annotations().size(), 2);
        tab->view()->goTo(cfi);
        QTRY_VERIFY_WITH_TIMEOUT(js(tab->view(), QStringLiteral("StudyLab.state().drawn")).toStringList().contains(cfi), kTimeout);
        const auto textOf = [&] {
            return js(tab->view(),
                      QStringLiteral("(() => { const v = document.querySelector('foliate-view');"
                                     " const c = v.renderer.getContents()[0];"
                                     " const r = v.resolveNavigation(%1).anchor(c.doc); return r.toString() })()")
                          .arg(QStringLiteral("'") + cfi + QStringLiteral("'")))
                .toString();
        };
        QCOMPARE(textOf(), QStringLiteral("système isolé se conserve"));
        // Changement de police et de taille : l'ancre désigne toujours le même texte
        ui::EpubSettings s = tab->displaySettings();
        s.font = ui::EpubSettings::Font::Sans;
        s.fontSize = 150;
        tab->setDisplaySettings(s);
        QTest::qWait(500);
        tab->view()->goTo(cfi);
        QTRY_VERIFY_WITH_TIMEOUT(js(tab->view(), QStringLiteral("StudyLab.state().drawn")).toStringList().contains(cfi), kTimeout);
        QCOMPARE(textOf(), QStringLiteral("système isolé se conserve"));
    }

    void exportUsesPercentAndChapters()
    {
        Env env;
        const qint64 id = env.import(m_epub3);
        ui::ReaderPage page(*env.ctx);
        page.resize(1200, 800);
        page.show();
        auto* tab = openTab(page, id);
        QVERIFY(tab);
        tab->view()->goTo(tab->view()->toc().at(2).href);
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, 2, kTimeout);
        QTest::qWait(300);
        QSignalSpy sel(tab->view(), &ui::EpubView::selectionMade);
        QVERIFY(js(tab->view(), QStringLiteral("StudyLab.selectText('machine de Carnot')")).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!sel.isEmpty(), kTimeout);
            sel.clear();
        QVERIFY(tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Pink).ok());
        QTemporaryDir out;
        auto folder = tab->exportMarkdown(out.path());
        QVERIFY(folder.ok());
        QFile f(QDir(folder.value()).filePath(QStringLiteral("Annotations.md")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString md = QString::fromUtf8(f.readAll());
        QVERIFY2(md.contains(QStringLiteral("## 3 · Machines thermiques")), qPrintable(md));
        QVERIFY2(md.contains(QStringLiteral("> [!quote] ")) && md.contains(QStringLiteral(" %](studylab://open?doc=")) && md.contains(QStringLiteral(") · Surlignage · rose")), qPrintable(md));
        QVERIFY(md.contains(QStringLiteral("> machine de Carnot")));
    }

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("STUDYLAB_SCREENSHOT_DIR non défini");
        for (const bool dark : {false, true}) {
            Env env;
            env.settings->setThemeMode(dark ? ThemeMode::Dark : ThemeMode::Light);
            env.theme->apply();
            const qint64 id = env.import(m_epub3);
            ui::ReaderPage page(*env.ctx);
            page.resize(1440, 900);
            page.show();
            auto* tab = openTab(page, id);
            QVERIFY(tab);
            ui::EpubSettings s;
            s.theme = dark ? ui::ReadingTheme::Dark : ui::ReadingTheme::Sepia;
            s.font = ui::EpubSettings::Font::Serif;
            s.fontSize = 120;
            s.lineHeight = 1.75;
            tab->setDisplaySettings(s);
            tab->view()->goTo(tab->view()->toc().at(1).href);
            QTRY_COMPARE_WITH_TIMEOUT(tab->view()->location().section, 1, kTimeout);
            QTest::qWait(400);
            QSignalSpy sel(tab->view(), &ui::EpubView::selectionMade);
            QVERIFY(js(tab->view(), QStringLiteral("StudyLab.selectText('ne peut que croître')")).toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!sel.isEmpty(), kTimeout);
            sel.clear();
            QVERIFY(tab->annotateSelection(AnnotationType::Highlight, AnnotationColor::Yellow).ok());
            QVERIFY(js(tab->view(), QStringLiteral("StudyLab.selectText('au fil du temps')")).toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!sel.isEmpty(), kTimeout);
            sel.clear();
            QVERIFY(tab->annotateSelection(AnnotationType::Underline, AnnotationColor::Blue).ok());
            if (!dark) {
                tab->typographyButton()->click();
                QTRY_VERIFY(tab->typographyPanel()->isVisible());
            }
            QTest::qWait(600);
            QDir().mkpath(dir);
            QImage shot = page.grab().toImage();
            if (!dark) {
                // Le panneau est une fenêtre surgissante : on la compose sur la capture.
                QPainter p(&shot);
                const QPoint at = page.mapFromGlobal(tab->typographyPanel()->pos());
                p.drawPixmap(at, tab->typographyPanel()->grab());
                p.end();
                tab->typographyPanel()->hide();
            }
            shot.save(dir + (dark ? QStringLiteral("/iteration5-epub-sombre.png") : QStringLiteral("/iteration5-epub.png")));
        }
    }
};

QTEST_MAIN(TstEpubReader)
#include "tst_epub_reader.moc"
