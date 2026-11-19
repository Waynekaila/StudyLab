#pragma once

#include "core/AppPaths.h"
#include "core/Result.h"
#include "core/domain/Document.h"
#include "data/repositories/DocumentRepository.h"
#include "services/library/ImportWorker.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

namespace studylab {

namespace db {
class Database;
}

/// Problème rencontré sur un fichier lors d'un import.
struct ImportIssue {
    QString path;
    QString reason;
};

/// Bilan d'un import, affiché à la fin.
struct ImportReport {
    int requested = 0;               ///< fichiers trouvés (après parcours des dossiers)
    QList<qint64> importedIds;
    QList<ImportIssue> duplicates;
    QList<ImportIssue> failures;
    bool canceled = false;
};

/// Bibliothèque : lecture/écriture des documents et import en arrière-plan.
/// Vit dans le thread de l'interface ; les écritures en base y sont faites (transactions courtes).
class LibraryService : public QObject {
    Q_OBJECT
public:
    LibraryService(db::Database& db, AppPaths paths, QObject* parent = nullptr);
    ~LibraryService() override;

    Result<QList<DocumentRecord>> documents(const db::DocumentQuery& query = {});
    Result<DocumentRecord> document(qint64 id);
    int count();
    qint64 totalFileSize();
    QStringList tags();

    Result<void> save(DocumentRecord& record);
    Result<void> setStatus(qint64 id, ReadingStatus status);
    Result<void> setFavorite(qint64 id, bool favorite);
    /// Retire le document de la bibliothèque (ses annotations restent en base, le fichier géré est conservé).
    Result<void> remove(qint64 id);

    /// Chemin absolu du fichier du document, et de sa couverture (vide si aucune).
    QString documentFile(const DocumentRecord& record) const;
    QString coverFile(const DocumentRecord& record) const;

    /// Importe des fichiers et dossiers. Si un import est en cours, celui-ci est mis en file d'attente.
    void importPaths(const QStringList& paths, StorageMode mode = StorageMode::Managed);
    void cancelImport();
    bool isImporting() const;

signals:
    void documentsChanged();
    void documentChanged(qint64 id);
    void importStarted();
    void importProgress(int done, int total);
    void importFinished(const studylab::ImportReport& report);

private:
    void startNext();
    void onResults(int begin, int end);
    void onFinished();

    db::Database& m_db;
    db::DocumentRepository m_repo;
    AppPaths m_paths;
    QFutureWatcher<ImportCandidate> m_watcher;
    QList<QPair<QStringList, StorageMode>> m_queue;
    ImportReport m_report;
    QTimer m_changeThrottle;
};

}  // namespace studylab

Q_DECLARE_METATYPE(studylab::ImportReport)
