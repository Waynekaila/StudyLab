#include "Bootstrap.h"

#include "core/AppLink.h"
#include "core/AppPaths.h"
#include "core/Log.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/search/SearchService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/InstanceChannel.h"
#include "ui/reader/EpubWeb.h"
#include "ui/shell/MainWindow.h"
#include "ui/pages/SettingsPage.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QTimer>

#include <cstdio>

using namespace studylab;

int main(int argc, char* argv[])
{
    QApplication::setApplicationName(QStringLiteral("StudyLab"));
    QApplication::setApplicationVersion(QStringLiteral(STUDYLAB_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("studylab"));
    ui::registerReaderSchemes();  // avant QApplication (Qt WebEngine)
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/studylab.svg")));

    const QStringList args = QCoreApplication::arguments().mid(1);
    if (args.contains(QStringLiteral("--version"))) {
        // Vérification des paquets (CI) : l'exécutable et toutes ses bibliothèques se chargent.
        std::fputs(qPrintable(QStringLiteral("StudyLab %1\n").arg(QCoreApplication::applicationVersion())), stdout);
        std::fflush(stdout);
        return 0;
    }
    QStringList links;
    for (const QString& a : args)
        if (AppLink::looksLikeLink(a))
            links << a;

    const AppPaths paths = AppPaths::standard();
    // Une seule instance par dossier de données : un lien cliqué dans un export s'ouvre dans la fenêtre existante.
    if (ui::InstanceChannel::sendToRunning(paths.dataDir(), links.isEmpty() ? QStringList{QStringLiteral("activate")} : links))
        return 0;
    ui::InstanceChannel channel(paths.dataDir());
    channel.listen();
    auto startup = openAndMigrate(paths);
    if (!startup) {
        QMessageBox::critical(nullptr, QStringLiteral("StudyLab"),
                              QObject::tr("StudyLab ne peut pas ouvrir sa base de données.\n\n%1")
                                  .arg(startup.error().message));
        return 1;
    }

    if (!startup.value().integrityProblems.isEmpty()) {
        // Fermeture anormale et base endommagée : proposer la dernière sauvegarde saine.
        QMessageBox box(QMessageBox::Warning, QStringLiteral("StudyLab"),
                        QObject::tr("StudyLab ne s'est pas fermé correctement la dernière fois et la vérification "
                                    "de la base a signalé des problèmes :\n\n%1")
                            .arg(startup.value().integrityProblems.mid(0, 5).join(QLatin1Char('\n'))));
        box.setInformativeText(QObject::tr("Vous pouvez restaurer la sauvegarde la plus récente. La base actuelle "
                                           "est conservée dans le dossier des sauvegardes."));
        auto* restore = box.addButton(QObject::tr("Restaurer la dernière sauvegarde"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Continuer sans restaurer"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == restore) {
            startup.value().db.reset();
            auto restored = restoreLatestBackup(paths);
            if (!restored)
                QMessageBox::warning(nullptr, QStringLiteral("StudyLab"),
                                     QObject::tr("La restauration a échoué : %1").arg(restored.error().message));
            startup = openAndMigrate(paths);
            if (!startup) {
                QMessageBox::critical(nullptr, QStringLiteral("StudyLab"),
                                      QObject::tr("StudyLab ne peut pas ouvrir sa base de données.\n\n%1")
                                          .arg(startup.error().message));
                return 1;
            }
        }
    }
    StartupReport& report = startup.value();
    db::Database& database = *report.db;

    int code = 0;
    {
        SettingsService settings(database);
        ui::ThemeManager theme(settings);
        theme.apply();

        if (!report.restoreError.isEmpty()) {
            QMessageBox::warning(nullptr, QStringLiteral("StudyLab"),
                                 QObject::tr("La restauration programmée n'a pas pu être appliquée ; la base actuelle "
                                             "est conservée.\n\n%1").arg(report.restoreError));
        } else if (!report.restoredFrom.isEmpty()) {
            QMessageBox::information(nullptr, QStringLiteral("StudyLab"),
                                     QObject::tr("La sauvegarde a été restaurée :\n%1")
                                         .arg(QDir::toNativeSeparators(report.restoredFrom)));
        }

        markRunning(paths);
        LibraryService library(database, paths);
        AnnotationService annotations(database);
        CommentService comments(database);
        SearchService search(database);
        BackupService backups(paths, database);
        NoteService notes(database, paths.attachmentsDir());
        OrganizeService organize(database);
        AppContext ctx{paths, database, settings, theme, library, annotations, comments, search, backups, notes, organize};
        ui::MainWindow window(ctx);
        window.show();
        QObject::connect(&channel, &ui::InstanceChannel::messageReceived, &window, &ui::MainWindow::handleInstanceMessage);
        for (const QString& link : std::as_const(links))
            QTimer::singleShot(0, &window, [&window, link] { window.handleInstanceMessage(link); });
        code = app.exec();
    }
    // Base fermée (services détruits) : la restauration programmée peut s'appliquer au redémarrage.
    report.db.reset();
    markStopped(paths);
    channel.close();  // la nouvelle instance doit pouvoir prendre le canal
    if (app.property(ui::kRestartProperty).toBool())
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    return code;
}
