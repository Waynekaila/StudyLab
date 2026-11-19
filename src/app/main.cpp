#include "Bootstrap.h"

#include "core/AppPaths.h"
#include "core/Log.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/shell/AppContext.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>

using namespace studylab;

int main(int argc, char* argv[])
{
    QApplication::setApplicationName(QStringLiteral("StudyLab"));
    QApplication::setApplicationVersion(QStringLiteral(STUDYLAB_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("studylab"));
    QApplication app(argc, argv);

    const AppPaths paths = AppPaths::standard();
    auto startup = openAndMigrate(paths);
    if (!startup) {
        QMessageBox::critical(nullptr, QStringLiteral("StudyLab"),
                              QObject::tr("StudyLab ne peut pas ouvrir sa base de données.\n\n%1")
                                  .arg(startup.error().message));
        return 1;
    }
    StartupReport& report = startup.value();
    db::Database& database = *report.db;

    SettingsService settings(database);
    ui::ThemeManager theme(settings);
    theme.apply();

    if (!report.integrityProblems.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("StudyLab"),
                             QObject::tr("StudyLab ne s'est pas fermé correctement la dernière fois et la vérification "
                                         "de la base a signalé des problèmes :\n\n%1\n\nLes sauvegardes se trouvent dans :\n%2")
                                 .arg(report.integrityProblems.mid(0, 5).join(QLatin1Char('\n')),
                                      QDir::toNativeSeparators(paths.backupsDir())));
    }

    markRunning(paths);
    LibraryService library(database, paths);
    AnnotationService annotations(database);
    AppContext ctx{paths, database, settings, theme, library, annotations};
    ui::MainWindow window(ctx);
    window.show();
    const int code = app.exec();
    markStopped(paths);
    return code;
}
