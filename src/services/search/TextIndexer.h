#pragma once

#include "core/Result.h"
#include "data/repositories/PageTextRepository.h"

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>

namespace studylab {

namespace db {
class Database;
}

/// Indexation du texte intégral en tâche de fond (02-architecture §… : « une seule fois par empreinte de fichier »).
/// Un document à la fois : l'extraction (PDF page par page, EPUB section par section) tourne dans un thread de
/// travail ; l'écriture en base (page_text, index FTS par triggers) se fait sur le thread principal, seul écrivain.
class TextIndexer : public QObject {
    Q_OBJECT
public:
    TextIndexer(db::Database& db, QString libraryDir, QObject* parent = nullptr);
    ~TextIndexer() override;

    /// Lance l'indexation des documents en attente après `delayMs` (sans effet si elle tourne déjà).
    void schedule(int delayMs = 1500);
    /// Remet tous les documents en attente, puis lance l'indexation.
    Result<void> reindexAll();
    /// Interrompt le document en cours et suspend l'indexation (reprise avec schedule()).
    void stop();

    /// Vrai pendant l'extraction d'un document (après stop(), jusqu'à ce qu'elle s'interrompe).
    bool isRunning() const { return m_running; }
    /// Documents PDF/EPUB indexés (ou sans texte), total.
    QPair<int, int> progress();

    /// Texte de chaque page ou section d'un fichier (thread de travail).
    static Result<QStringList> extract(const QString& path, const QString& format, const std::atomic_bool* cancel);

signals:
    void progressChanged(int done, int total);
    /// Plus rien à indexer.
    void finished();

private:
    void next();
    void onExtracted();

    db::PageTextRepository m_repo;
    QString m_libraryDir;
    QTimer m_start;
    QFutureWatcher<Result<QStringList>>* m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    qint64 m_current = 0;
    bool m_running = false;
};

}  // namespace studylab
