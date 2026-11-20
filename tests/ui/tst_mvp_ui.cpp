#include "support/WebEngineSetup.h"
#include "Bootstrap.h"

#include "core/AppLink.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/library/LibraryService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "support/Fixtures.h"
#include "ui/library/LibraryModel.h"
#include "ui/library/LibraryPage.h"
#include "ui/pages/HomePage.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/reader/PdfView.h"
#include "ui/reader/ReaderPage.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/InstanceChannel.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::test;

/// Itération 7 : accueil, export de la bibliothèque depuis l'interface, liens studylab://, instance unique.
class TstMvpUi : public QObject {
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
            ctx = std::make_unique<AppContext>(
                AppContext{paths, *db, *settings, *theme, *library, *annotations, *comments, *search, *backups, *notes, *organize});
        }
        QList<qint64> import(const QStringList& files)
        {
            QSignalSpy finished(library.get(), &LibraryService::importFinished);
            library->importPaths(files);
            if (!finished.wait(60000))
                return {};
            return finished.first().at(0).value<ImportReport>().importedIds;
        }
        AnnotationRecord annotate(qint64 doc, int page, const QString& text, AnnotationColor color = AnnotationColor::Yellow)
        {
            PdfLocator l;
            l.page = page;
            l.rects = {QRectF(0.1, 0.12, 0.6, 0.02)};
            l.exact = text;
            AnnotationRecord r;
            r.documentId = doc;
            r.color = color;
            r.locator = l.toJson();
            r.selectedText = text;
            return annotations->create(r).value();
        }
        CommentRecord comment(qint64 doc, int page, const QString& body, CommentKind kind)
        {
            PdfLocator l;
            l.page = page;
            CommentRecord c;
            c.documentId = doc;
            c.anchor = CommentAnchor::Page;
            c.unitIndex = page;
            c.locator = l.toJson();
            c.kind = kind;
            c.body = body;
            return comments->create(c).value();
        }
    };

    QTemporaryDir m_src;
    QString m_bode;
    QString m_fourier;
    QString m_thermo;

    static void maybeScreenshot(QWidget& w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("STUDYLAB_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(600);
        QDir().mkpath(dir);
        w.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    /// Bibliothèque de démonstration : trois PDF, un ouvert récemment, annotations et commentaires.
    struct Demo {
        qint64 bode = 0, fourier = 0, thermo = 0;
        AnnotationRecord gain;
        CommentRecord question;
    };
    Demo fill(Env& env)
    {
        Demo d;
        const auto ids = env.import({m_bode, m_fourier, m_thermo});
        if (ids.size() != 3)
            qFatal("import incomplet");
        for (qint64 id : ids) {
            const auto doc = env.library->document(id).value();
            if (doc.title.contains(QLatin1String("quentielle")))
                d.bode = id;
            else if (doc.title.contains(QLatin1String("Fourier")))
                d.fourier = id;
            else
                d.thermo = id;
        }
        (void)env.library->setStatus(d.fourier, ReadingStatus::Reading);
        (void)env.library->markOpened(d.fourier);
        QTest::qWait(1100);  // horodatage à la seconde
        (void)env.library->setStatus(d.bode, ReadingStatus::Reading);
        (void)env.library->markOpened(d.bode);
        db::ReadingPosition pos;
        pos.locator = QJsonObject{{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("page"), 3}};
        pos.progress = 4.0 / 6.0;
        (void)env.library->saveReadingPosition(d.bode, pos);
        env.annotate(d.fourier, 0, QStringLiteral("Toute fonction périodique se décompose en série de Fourier"),
                     AnnotationColor::Blue);
        d.gain = env.annotate(d.bode, 2, QStringLiteral("Le gain et la phase se tracent en fonction de la fréquence"));
        env.annotate(d.bode, 3, QStringLiteral("seule subsiste la réponse établie"), AnnotationColor::Green);
        env.comment(d.bode, 2, QStringLiteral("Relier au **diagramme de Bode** du TD 3"), CommentKind::Explanation);
        d.question = env.comment(d.bode, 4, QStringLiteral("Pourquoi la phase vaut-elle $-90°$ à la coupure ?"),
                                 CommentKind::Question);
        return d;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImportReport>();
        QVERIFY(kWebEngineReady);
        m_bode = m_src.filePath(QStringLiteral("bode.pdf"));
        m_fourier = m_src.filePath(QStringLiteral("fourier.pdf"));
        m_thermo = m_src.filePath(QStringLiteral("thermo.pdf"));
        QVERIFY(writePdf(m_bode, {QStringLiteral("Réponse fréquentielle"), 6, QStringLiteral("Le gain et la phase.")}));
        QVERIFY(writePdf(m_fourier, {QStringLiteral("Séries de Fourier"), 4, QStringLiteral("Toute fonction.")}));
        QVERIFY(writePdf(m_thermo, {QStringLiteral("Thermodynamique"), 3, QStringLiteral("Entropie.")}));
    }

    void homeEmptyThenResume()
    {
        Env env;
        ui::MainWindow window(*env.ctx);
        window.resize(1300, 860);
        window.show();
        window.navigateTo(QStringLiteral("home"));
        auto* home = qobject_cast<ui::HomePage*>(window.page(QStringLiteral("home")));
        QVERIFY(home);
        QVERIFY(!home->continueButton()->isVisible());
        QVERIFY(home->annotationRows().isEmpty());
        QVERIFY2(home->subtitle()->text().contains(QStringLiteral("0 document")), qPrintable(home->subtitle()->text()));
        QVERIFY(home->backupLabel()->text().contains(QStringLiteral("Dernière sauvegarde")));  // sauvegarde du démarrage

        const Demo d = fill(env);
        home->refresh();
        QVERIFY2(home->subtitle()->text().contains(QStringLiteral("3 documents")), qPrintable(home->subtitle()->text()));
        QVERIFY(home->continueButton()->isVisible());
        QCOMPARE(home->resumeTitle()->text(), QStringLiteral("Réponse fréquentielle"));
        QVERIFY2(home->resumePosition()->text().startsWith(QStringLiteral("Page 4 sur 6 · 67 %")),
                 qPrintable(home->resumePosition()->text()));
        QCOMPARE(home->readingRows().size(), 1);  // Fourier, ouvert avant
        QCOMPARE(home->annotationRows().size(), 3);
        QVERIFY(home->annotationRows().first()->accessibleName().contains(QStringLiteral("seule subsiste")));
        QCOMPARE(home->commentRows().size(), 2);
        QVERIFY2(home->commentRows().first()->accessibleName().contains(QStringLiteral("Pourquoi la phase")),
                 qPrintable(home->commentRows().first()->accessibleName()));
        QVERIFY(!home->commentRows().last()->accessibleName().contains(QStringLiteral("**")));  // Markdown simplifié
        maybeScreenshot(window, QStringLiteral("iteration7-accueil"));

        // « Continuer la lecture » rouvre le document à sa page
        home->continueButton()->click();
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        QTRY_VERIFY_WITH_TIMEOUT(reader->tabFor(d.bode), 10000);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->tabFor(d.bode));
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->currentPage(), 3, 10000);

        // Une annotation récente ouvre son document au passage
        window.navigateTo(QStringLiteral("home"));
        home->annotationRows().last()->click();  // la plus ancienne : Fourier
        QTRY_VERIFY_WITH_TIMEOUT(reader->tabFor(d.fourier), 10000);
        QCOMPARE(reader->currentTab()->documentId(), d.fourier);
    }

    void homeDarkScreenshot()
    {
        if (qEnvironmentVariableIsEmpty("STUDYLAB_SCREENSHOT_DIR"))
            QSKIP("Captures seulement avec STUDYLAB_SCREENSHOT_DIR");
        Env env;
        env.settings->setThemeMode(ThemeMode::Dark);
        env.theme->apply();
        fill(env);
        ui::MainWindow window(*env.ctx);
        window.resize(1300, 860);
        window.show();
        window.navigateTo(QStringLiteral("home"));
        maybeScreenshot(window, QStringLiteral("iteration7-accueil-sombre"));
    }

    void exportFromLibraryAndOpenLink()
    {
        Env env;
        const Demo d = fill(env);
        ui::MainWindow window(*env.ctx);
        window.resize(1300, 860);
        window.show();
        window.navigateTo(QStringLiteral("library"));
        auto* lib = qobject_cast<ui::LibraryPage*>(window.page(QStringLiteral("library")));
        QVERIFY(lib->exportButton()->isVisible());

        // Sélection multiple (Ctrl) : Bode et Fourier
        QItemSelectionModel* sel = lib->view()->selectionModel();
        sel->clearSelection();
        for (int r = 0; r < lib->model()->rowCount(); ++r) {
            const QModelIndex idx = lib->model()->index(r, 0);
            const qint64 id = idx.data(ui::LibraryModel::IdRole).toLongLong();
            if (id == d.bode || id == d.fourier)
                sel->select(idx, QItemSelectionModel::Select);
        }
        QCOMPARE(lib->selectedIds().size(), 2);
        QCOMPARE(lib->visibleIds().size(), 3);

        QTemporaryDir out;
        lib->setExportDirectoryChooser([&out] { return out.path(); });
        QSignalSpy done(lib, &ui::LibraryPage::exportFinished);
        lib->exportDocuments(lib->selectedIds());
        QVERIFY(lib->isExporting());
        QVERIFY(done.wait(30000));
        const QString folder = done.first().at(0).toString();
        QVERIFY(!folder.isEmpty());
        QVERIFY(!lib->isExporting());
        QVERIFY(lib->importStatusLabel()->text().startsWith(QStringLiteral("2 documents exportés · 3 annotations · 2 commentaires")));
        QVERIFY(lib->openExportButton()->isVisible());
        maybeScreenshot(window, QStringLiteral("iteration7-export"));
        QStringList exported = QDir(folder + QStringLiteral("/Bibliothèque")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        exported.sort();
        QCOMPARE(exported, QStringList({QStringLiteral("Réponse fréquentielle"), QStringLiteral("Séries de Fourier")}));
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/annotations.csv")));

        // Lien du Markdown exporté → passage dans le lecteur
        QFile md(folder + QStringLiteral("/Bibliothèque/Réponse fréquentielle/Annotations.md"));
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(md.readAll());
        const qsizetype at = text.indexOf(QStringLiteral("studylab://open?doc="));
        QVERIFY(at > 0);
        const QString link = text.mid(at, text.indexOf(QLatin1Char(')'), at) - at);
        const auto parsed = AppLink::parse(link);
        QVERIFY2(parsed.has_value(), qPrintable(link));
        QCOMPARE(parsed->annotationUuid, d.gain.uuid);
        QCOMPARE(AppLink::parse(parsed->toString())->documentUuid, parsed->documentUuid);
        QVERIFY(!AppLink::parse(QStringLiteral("studylab://supprimer?doc=") + parsed->documentUuid));
        QVERIFY(!AppLink::parse(QStringLiteral("studylab://open?doc=pas-un-uuid")));
        QVERIFY(AppLink::parse(QStringLiteral("studylab://open/?doc=") + parsed->documentUuid));

        QVERIFY(window.openLink(link).ok());
        QCOMPARE(window.currentPage(), QStringLiteral("reader"));
        auto* reader = qobject_cast<ui::ReaderPage*>(window.page(QStringLiteral("reader")));
        QTRY_VERIFY_WITH_TIMEOUT(reader->tabFor(d.bode), 10000);
        auto* tab = qobject_cast<ui::PdfReaderTab*>(reader->tabFor(d.bode));
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->currentPage(), 2, 10000);

        // Lien vers un commentaire, puis vers un document retiré
        AppLink toComment{parsed->documentUuid, {}, d.question.uuid};
        QVERIFY(window.openLink(toComment.toString()).ok());
        QTRY_COMPARE_WITH_TIMEOUT(tab->view()->currentPage(), 4, 10000);
        const QString thermoUuid = env.library->document(d.thermo).value().uuid;
        QVERIFY(env.library->remove(d.thermo).ok());
        const auto missing = window.openLink(AppLink{thermoUuid, {}, {}}.toString());
        QVERIFY(!missing.ok());
        QCOMPARE(missing.error().code, Error::Code::NotFound);
        QVERIFY(!window.openLink(QStringLiteral("https://exemple.fr")).ok());
    }

    void secondLaunchForwardsLinkToRunningInstance()
    {
        QTemporaryDir data;
        QVERIFY(!ui::InstanceChannel::sendToRunning(data.path(), {QStringLiteral("activate")}, 300));
        ui::InstanceChannel channel(data.path());
        QVERIFY(channel.listen());
        QSignalSpy received(&channel, &ui::InstanceChannel::messageReceived);
        const QString link = QStringLiteral("studylab://open?doc=00000000-0000-4000-8000-000000000000");
        // Le second lancement est un autre processus : l'envoi part d'un autre thread pendant que la boucle
        // d'événements de l'instance tourne (sous Windows, l'écriture sur le tube attend la lecture du serveur).
        QFuture<bool> sent = QtConcurrent::run([path = data.path(), link] {
            return ui::InstanceChannel::sendToRunning(path, {link, QStringLiteral("activate")});
        });
        QTRY_VERIFY_WITH_TIMEOUT(sent.isFinished(), 10000);
        QVERIFY(sent.result());
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 5000);
        QCOMPARE(received.at(0).at(0).toString(), link);
        QCOMPARE(received.at(1).at(0).toString(), QStringLiteral("activate"));
        // Un autre dossier de données est une autre instance
        QTemporaryDir other;
        QVERIFY(ui::InstanceChannel::serverName(other.path()) != ui::InstanceChannel::serverName(data.path()));
        channel.close();
        QVERIFY(!ui::InstanceChannel::sendToRunning(data.path(), {QStringLiteral("activate")}, 300));
        QVERIFY(channel.listen());  // relance après un redémarrage
    }
};

QTEST_MAIN(TstMvpUi)
#include "tst_mvp_ui.moc"
