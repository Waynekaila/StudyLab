#include "core/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

#include <utility>

namespace studylab {

AppPaths::AppPaths(QString dataDir) : m_dataDir(QDir::cleanPath(std::move(dataDir))) {}

AppPaths AppPaths::standard()
{
    const QString overrideDir = qEnvironmentVariable("STUDYLAB_DATA_DIR");
    if (!overrideDir.isEmpty())
        return AppPaths(overrideDir);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return AppPaths(base + QStringLiteral("/StudyLab"));
}

QString AppPaths::databaseFile() const { return m_dataDir + QStringLiteral("/studylab.db"); }
QString AppPaths::libraryDir() const { return m_dataDir + QStringLiteral("/library"); }
QString AppPaths::coversDir() const { return m_dataDir + QStringLiteral("/covers"); }
QString AppPaths::attachmentsDir() const { return m_dataDir + QStringLiteral("/attachments"); }
QString AppPaths::backupsDir() const { return m_dataDir + QStringLiteral("/backups"); }
QString AppPaths::cacheDir() const { return m_dataDir + QStringLiteral("/cache"); }
QString AppPaths::runningMarkerFile() const { return m_dataDir + QStringLiteral("/.running"); }

bool AppPaths::ensureCreated() const
{
    QDir dir;
    for (const QString& path : {m_dataDir, libraryDir(), coversDir(), attachmentsDir(), backupsDir()}) {
        if (!dir.mkpath(path))
            return false;
    }
    return true;
}

}  // namespace studylab
