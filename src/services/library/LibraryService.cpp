#include "services/library/LibraryService.h"

#include "core/Log.h"
#include "data/sqlite/Database.h"

#include <QDir>
#include <QFile>
#include <QPromise>
#include <QtConcurrent/QtConcurrentRun>

namespace studylab {

LibraryService::LibraryService(db::Database& db, AppPaths paths, QObject* parent)
    : QObject(parent), m_db(db), m_repo(db), m_paths(std::move(paths))
{
    // Pendant un gros import, la liste n'est rafraîchie qu'au plus deux fois par seconde.
    m_changeThrottle.setSingleShot(true);
    m_changeThrottle.setInterval(500);
    connect(&m_changeThrottle, &QTimer::timeout, this, &LibraryService::documentsChanged);
    connect(&m_watcher, &QFutureWatcher<ImportCandidate>::resultsReadyAt, this, &LibraryService::onResults);
    connect(&m_watcher, &QFutureWatcher<ImportCandidate>::finished, this, &LibraryService::onFinished);
    connect(&m_watcher, &QFutureWatcher<ImportCandidate>::progressValueChanged, this,
            [this](int v) { emit importProgress(v, m_watcher.progressMaximum()); });
}

LibraryService::~LibraryService()
{
    m_queue.clear();
    if (m_watcher.isRunning()) {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }
}

Result<QList<DocumentRecord>> LibraryService::documents(const db::DocumentQuery& query)
{
    return m_repo.list(query);
}

Result<DocumentRecord> LibraryService::document(qint64 id)
{
    return m_repo.findById(id);
}

int LibraryService::count()
{
    const auto n = m_repo.count();
    return n ? n.value() : 0;
}

qint64 LibraryService::totalFileSize()
{
    const auto n = m_repo.totalFileSize();
    return n ? n.value() : 0;
}

QStringList LibraryService::tags()
{
    auto t = m_repo.documentTags();
    return t ? t.value() : QStringList{};
}

Result<void> LibraryService::save(DocumentRecord& record)
{
    auto r = m_repo.update(record);
    if (r) {
        emit documentChanged(record.id);
        emit documentsChanged();
    }
    return r;
}

Result<void> LibraryService::setStatus(qint64 id, ReadingStatus status)
{
    auto r = m_repo.setStatus(id, status);
    if (r) {
        emit documentChanged(id);
        emit documentsChanged();
    }
    return r;
}

Result<void> LibraryService::setFavorite(qint64 id, bool favorite)
{
    auto r = m_repo.setFavorite(id, favorite);
    if (r) {
        emit documentChanged(id);
        emit documentsChanged();
    }
    return r;
}

Result<void> LibraryService::remove(qint64 id)
{
    auto r = m_repo.remove(id);
    if (r)
        emit documentsChanged();
    return r;
}

QString LibraryService::documentFile(const DocumentRecord& record) const
{
    if (record.storageMode == StorageMode::Linked)
        return record.filePath;
    return QDir(m_paths.libraryDir()).filePath(record.filePath);
}

QString LibraryService::coverFile(const DocumentRecord& record) const
{
    if (record.coverPath.isEmpty())
        return {};
    return QDir(m_paths.coversDir()).filePath(record.coverPath);
}

bool LibraryService::isImporting() const
{
    return m_watcher.isRunning() || !m_queue.isEmpty();
}

void LibraryService::importPaths(const QStringList& paths, StorageMode mode)
{
    if (paths.isEmpty())
        return;
    m_queue.append({paths, mode});
    if (!m_watcher.isRunning())
        startNext();
}

void LibraryService::cancelImport()
{
    m_queue.clear();
    if (m_watcher.isRunning()) {
        m_report.canceled = true;
        m_watcher.cancel();
    }
}

void LibraryService::startNext()
{
    if (m_queue.isEmpty())
        return;
    const auto [inputs, mode] = m_queue.takeFirst();
    m_report = {};
    auto hashes = m_repo.activeHashes();
    const QSet<QString> libraryHashes = hashes ? hashes.value() : QSet<QString>{};
    const AppPaths paths = m_paths;

    auto future = QtConcurrent::run(
        [inputs, mode, libraryHashes, paths](QPromise<ImportCandidate>& promise) {
            const QStringList files = ImportWorker::expandPaths(inputs);
            promise.setProgressRange(0, int(files.size()));
            promise.setProgressValue(0);
            QSet<QString> batch;
            int done = 0;
            for (const QString& f : files) {
                if (promise.isCanceled())
                    return;
                promise.addResult(ImportWorker::prepare(f, paths, mode, libraryHashes, batch));
                promise.setProgressValue(++done);
            }
        });
    m_watcher.setFuture(future);
    emit importStarted();
}

void LibraryService::onResults(int begin, int end)
{
    for (int i = begin; i < end; ++i) {
        ImportCandidate c = m_watcher.resultAt(i);
        ++m_report.requested;
        if (!c.error.isEmpty()) {
            m_report.failures.append({c.sourcePath, c.error});
            continue;
        }
        if (!c.duplicateHash.isEmpty()) {
            QString reason = QStringLiteral("Présent deux fois dans la sélection.");
            if (!c.duplicateInSelection) {
                const auto existing = m_repo.findByHash(c.duplicateHash);
                reason = existing && existing.value()
                             ? QStringLiteral("Déjà dans la bibliothèque : « %1 ».").arg(existing.value()->title)
                             : QStringLiteral("Déjà dans la bibliothèque.");
            }
            m_report.duplicates.append({c.sourcePath, reason});
            continue;
        }
        DocumentRecord& r = *c.record;
        if (auto ins = m_repo.insert(r); !ins) {
            qCWarning(lcDb) << "Insertion du document impossible :" << c.sourcePath << ins.error().message;
            if (!r.coverPath.isEmpty())
                QFile::remove(coverFile(r));
            m_report.failures.append({c.sourcePath, QStringLiteral("Enregistrement impossible : %1").arg(ins.error().message)});
            continue;
        }
        m_report.importedIds << r.id;
        if (!m_changeThrottle.isActive())
            m_changeThrottle.start();
    }
}

void LibraryService::onFinished()
{
    m_report.canceled = m_report.canceled || m_watcher.isCanceled();
    m_changeThrottle.stop();
    if (!m_report.importedIds.isEmpty())
        emit documentsChanged();
    const ImportReport report = m_report;
    emit importFinished(report);
    if (!m_queue.isEmpty())
        startNext();
}

}  // namespace studylab
