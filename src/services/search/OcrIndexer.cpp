#include "services/search/OcrIndexer.h"

#include "core/Log.h"
#include "engines/document/TextUnits.h"
#include "engines/ocr/TesseractOcr.h"

#include <QDir>
#include <QPainter>
#include <QPdfDocument>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace studylab {

namespace {
constexpr int kDpi = 300;
constexpr int kMaxSide = 4200;  // pixels : au-delà, une page (affiche, plan) est rendue moins finement
}  // namespace

OcrIndexer::OcrIndexer(db::Database& db, QString libraryDir, QObject* parent)
    : QObject(parent), m_repo(db), m_libraryDir(std::move(libraryDir)), m_watcher(new QFutureWatcher<Result<void>>(this))
{
    m_start.setSingleShot(true);
    connect(&m_start, &QTimer::timeout, this, &OcrIndexer::next);
    connect(m_watcher, &QFutureWatcher<Result<void>>::finished, this, &OcrIndexer::onDone);
}

OcrIndexer::~OcrIndexer()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher->waitForFinished();
}

bool OcrIndexer::isAvailable() { return TesseractOcr::isAvailable(); }

void OcrIndexer::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
        stop();
}

void OcrIndexer::schedule(int delayMs)
{
    if (!m_enabled || m_running || m_start.isActive() || !isAvailable())
        return;
    m_start.start(std::max(0, delayMs));
}

void OcrIndexer::stop()
{
    m_start.stop();
    if (m_cancel)
        m_cancel->store(true);
}

Result<void> OcrIndexer::rerunAll()
{
    stop();
    m_watcher->waitForFinished();
    if (auto r = m_repo.resetOcr(); !r)
        return r;
    emit progressChanged();
    schedule(0);
    return {};
}

QPair<int, int> OcrIndexer::progress()
{
    auto p = m_repo.ocrProgress();
    return p ? p.value() : qMakePair(0, 0);
}

Result<void> OcrIndexer::recognizePdf(const QString& path, const QList<int>& pages, const QStringList& languages,
                                      const std::atomic_bool* cancel,
                                      const std::function<void(int, const OcrPage&)>& onPage)
{
    TesseractOcr ocr;
    if (auto r = ocr.init(languages); !r)
        return r;
    QPdfDocument doc;
    const QPdfDocument::Error err = doc.load(path);
    if (err == QPdfDocument::Error::IncorrectPassword)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("PDF protégé par mot de passe"));
    if (err != QPdfDocument::Error::None)
        return Error::make(Error::Code::Io, QStringLiteral("PDF illisible : %1").arg(path));
    for (int page : pages) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return Error::make(Error::Code::Generic, QStringLiteral("OCR interrompu"));
        if (page < 0 || page >= doc.pageCount())
            continue;
        const QSizeF points = doc.pagePointSize(page);
        if (points.isEmpty())
            continue;
        qreal scale = kDpi / 72.0;
        scale = std::min(scale, kMaxSide / std::max(points.width(), points.height()));
        const QSize size = (points * scale).toSize();
        const QImage rendered = doc.render(page, size);
        // Fond blanc : les pages sans fond rendent un canal alpha transparent, lu comme du noir.
        QImage canvas(size, QImage::Format_RGB32);
        canvas.fill(Qt::white);
        {
            QPainter p(&canvas);
            p.drawImage(0, 0, rendered);
        }
        auto result = ocr.recognize(canvas, qRound(72.0 * scale));
        if (!result) {
            qCInfo(lcDb) << "OCR de la page" << page << ":" << result.error().message;
            continue;
        }
        onPage(page, result.value());
    }
    return {};
}

void OcrIndexer::next()
{
    if (m_running || !m_enabled)
        return;
    auto pending = m_repo.ocrPending();
    if (!pending || pending.value().isEmpty()) {
        if (!pending)
            qCWarning(lcDb) << "OCR :" << pending.error().message;
        m_current = 0;
        emit finished();
        return;
    }
    const db::PendingTextDocument doc = pending.value().first();
    auto withText = m_repo.unitsWithText(doc.id);
    const QString path = doc.linked ? doc.filePath : QDir(m_libraryDir).filePath(doc.filePath);
    int count = 0;
    {
        QPdfDocument pdf;
        if (pdf.load(path) == QPdfDocument::Error::None)
            count = pdf.pageCount();
    }
    QList<int> pages;
    for (int i = 0; i < count; ++i)
        if (!withText || !withText.value().contains(i))
            pages << i;
    m_running = true;
    m_current = doc.id;
    m_pageDone = 0;
    m_pageTotal = int(pages.size());
    emit progressChanged();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const QStringList languages = m_languages;
    const qint64 id = doc.id;
    if (count == 0) {
        // Fichier absent ou illisible : le worker renvoie l'erreur, le document est marqué « OCR impossible ».
        pages.clear();
    }
    m_watcher->setFuture(QtConcurrent::run([this, path, pages, languages, cancel, id, count]() -> Result<void> {
        if (count == 0)
            return Error::make(Error::Code::Io, QStringLiteral("PDF illisible : %1").arg(path));
        // Basse priorité : la lecture et l'interface passent avant l'OCR (R6, R16).
        QThread* thread = QThread::currentThread();
        const QThread::Priority before = thread->priority();
        thread->setPriority(QThread::LowestPriority);
        auto r = recognizePdf(path, pages, languages, cancel.get(), [this, id](int page, const OcrPage& ocr) {
            const QString text = normalizeExtractedText(ocr.text());
            const QString words = ocr.toJson();
            QMetaObject::invokeMethod(this, [this, id, page, text, words] { store(id, page, text, words); },
                                      Qt::QueuedConnection);
        });
        thread->setPriority(before == QThread::InheritPriority ? QThread::NormalPriority : before);
        return r;
    }));
}

void OcrIndexer::store(qint64 documentId, int page, const QString& text, const QString& words)
{
    if (auto r = m_repo.storeOcr(documentId, page, text, words); !r)
        qCWarning(lcDb) << "OCR :" << r.error().message;
    if (documentId == m_current)
        ++m_pageDone;
    emit pageRecognized(documentId, page);
    emit progressChanged();
}

void OcrIndexer::onDone()
{
    m_running = false;
    const bool cancelled = m_cancel && m_cancel->load();
    const Result<void> result = m_watcher->result();
    const qint64 id = m_current;
    if (cancelled && !result) {
        m_current = 0;
        emit progressChanged();
        return;  // arrêt demandé : les pages lues sont gardées, le reste attend
    }
    if (!result)
        qCInfo(lcDb) << "OCR du document" << id << ":" << result.error().message;
    if (auto r = m_repo.markOcrDone(id, !result); !r) {
        qCWarning(lcDb) << "OCR :" << r.error().message;
        m_current = 0;
        emit progressChanged();
        emit finished();  // base indisponible : on ne boucle pas
        return;
    }
    m_current = 0;
    emit progressChanged();
    if (cancelled)
        return;  // document terminé juste avant l'arrêt : la suite attend
    m_start.start(0);
}

}  // namespace studylab
