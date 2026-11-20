#pragma once

#include "core/Result.h"
#include "data/repositories/PageTextRepository.h"
#include "engines/ocr/OcrPage.h"

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace studylab {

namespace db {
class Database;
}

/// File d'OCR en tâche de fond (V1-7b, R16) : les pages sans texte des PDF indexés (scans) sont rendues à 300 dpi et
/// lues par Tesseract, une page à la fois, dans un thread de travail à basse priorité. Chaque page reconnue est
/// enregistrée aussitôt (texte dans page_text, source « ocr », boîtes des mots dans words_json, index FTS par
/// triggers) : une interruption reprend à la page suivante. Sans Tesseract, ou désactivée, la file ne fait rien.
class OcrIndexer : public QObject {
    Q_OBJECT
public:
    OcrIndexer(db::Database& db, QString libraryDir, QObject* parent = nullptr);
    ~OcrIndexer() override;

    /// Tesseract et au moins une langue installés.
    static bool isAvailable();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    /// Langues de reconnaissance (codes Tesseract) ; vide : langues par défaut. S'applique au document suivant.
    void setLanguages(const QStringList& languages) { m_languages = languages; }
    QStringList languages() const { return m_languages; }

    /// Lance l'OCR des documents en attente après `delayMs` (sans effet si désactivé ou déjà en cours).
    void schedule(int delayMs = 2000);
    /// Interrompt la page en cours ; les pages déjà lues sont gardées (reprise avec schedule()).
    void stop();
    /// Oublie le texte reconnu et relit tous les PDF concernés (autres langues, nouvelle version de Tesseract).
    Result<void> rerunAll();

    bool isRunning() const { return m_running; }
    /// Document en cours (0 sinon), pages lues et à lire pour lui.
    qint64 currentDocument() const { return m_current; }
    int currentDone() const { return m_pageDone; }
    int currentTotal() const { return m_pageTotal; }
    /// PDF passés à l'OCR, PDF en attente.
    QPair<int, int> progress();

    /// Reconnaît des pages d'un PDF (thread de travail). `onPage(page, ocrPage)` est appelé pour chaque page lue.
    static Result<void> recognizePdf(const QString& path, const QList<int>& pages, const QStringList& languages,
                                     const std::atomic_bool* cancel,
                                     const std::function<void(int, const OcrPage&)>& onPage);

signals:
    void progressChanged();
    /// Une page vient d'être reconnue et enregistrée.
    void pageRecognized(qint64 documentId, int page);
    /// Plus rien à lire.
    void finished();

private:
    void next();
    void onDone();
    void store(qint64 documentId, int page, const QString& text, const QString& words);

    db::PageTextRepository m_repo;
    QString m_libraryDir;
    QTimer m_start;
    QFutureWatcher<Result<void>>* m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QStringList m_languages;
    qint64 m_current = 0;
    int m_pageDone = 0;
    int m_pageTotal = 0;
    bool m_enabled = true;
    bool m_running = false;
};

}  // namespace studylab
