#include "engines/pdf/PdfRenderQueue.h"

#include <QPdfDocument>
#include <QPainter>
#include <QPdfDocumentRenderOptions>

#include <algorithm>

namespace studylab {

class PdfRenderQueue::Worker : public QThread {
public:
    Worker(PdfRenderQueue* owner, QPdfDocument* doc) : m_owner(owner), m_doc(doc) {}

    void replace(const QList<PdfTileKey>& keys, int lane)
    {
        QMutexLocker lock(&m_mutex);
        m_queues[lane].assign(keys.cbegin(), keys.cend());
        m_cond.wakeOne();
    }
    void stop()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_stop = true;
            m_queues[0].clear();
            m_queues[1].clear();
            m_cond.wakeOne();
        }
        wait();
    }
    int pending() const
    {
        QMutexLocker lock(&m_mutex);
        return int(m_queues[0].size() + m_queues[1].size()) + (m_busy ? 1 : 0);
    }

protected:
    void run() override
    {
        while (true) {
            PdfTileKey key;
            {
                QMutexLocker lock(&m_mutex);
                m_busy = false;
                while (!m_stop && m_queues[0].empty() && m_queues[1].empty())
                    m_cond.wait(&m_mutex);
                if (m_stop)
                    return;
                auto& q = m_queues[0].empty() ? m_queues[1] : m_queues[0];
                key = q.front();
                q.pop_front();
                m_busy = true;
            }
            const QImage img = render(key);
            QMetaObject::invokeMethod(m_owner, [owner = m_owner, key, img] { owner->onRendered(key, img); },
                                      Qt::QueuedConnection);
        }
    }

private:
    QImage render(const PdfTileKey& key) const
    {
        const QSize full = m_owner->scaledPageSize(key.page, key.scaledWidth);
        if (full.isEmpty())
            return {};
        QPdfDocumentRenderOptions opts;
        opts.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QImage img;
        if (key.isPreview()) {
            img = m_doc->render(key.page, full, opts);
        } else {
            const QRect clip = m_owner->tileRect(key);
            opts.setScaledSize(full);
            opts.setScaledClipRect(clip);
            img = m_doc->render(key.page, clip.size(), opts);
        }
        if (img.isNull())
            return img;
        // Fond papier : les zones transparentes du PDF s'affichent en blanc.
        if (img.hasAlphaChannel()) {
            QImage opaque(img.size(), QImage::Format_RGB32);
            opaque.fill(Qt::white);
            QPainter p(&opaque);
            p.drawImage(0, 0, img);
            p.end();
            return opaque;
        }
        return img;
    }

    PdfRenderQueue* m_owner;
    QPdfDocument* m_doc;
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    std::array<std::deque<PdfTileKey>, 2> m_queues;
    bool m_stop = false;
    bool m_busy = false;
};

PdfRenderQueue::PdfRenderQueue(QPdfDocument* document, QObject* parent)
    : QObject(parent), m_document(document), m_cache(160 * 1024)
{
    qRegisterMetaType<PdfTileKey>();
    for (int i = 0; i < document->pageCount(); ++i)
        m_pageSizes << document->pagePointSize(i);
    m_worker = new Worker(this, document);
    m_worker->start(QThread::LowPriority);
}

PdfRenderQueue::~PdfRenderQueue()
{
    m_worker->stop();
    delete m_worker;
}

QSize PdfRenderQueue::scaledPageSize(int page, int scaledWidth) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return {};
    const QSizeF pt = m_pageSizes.at(page);
    if (pt.width() <= 0)
        return {};
    return {scaledWidth, qMax(1, qRound(scaledWidth * pt.height() / pt.width()))};
}

QRect PdfRenderQueue::tileRect(const PdfTileKey& key) const
{
    const QSize full = scaledPageSize(key.page, key.scaledWidth);
    if (key.isPreview())
        return QRect(QPoint(0, 0), full);
    return QRect(key.column * kTileSize, key.row * kTileSize, kTileSize, kTileSize).intersected(QRect(QPoint(0, 0), full));
}

QImage PdfRenderQueue::cached(const PdfTileKey& key) const
{
    const QImage* img = m_cache.object(key);
    return img ? *img : QImage();
}

void PdfRenderQueue::setWanted(const QList<PdfTileKey>& keys, Lane lane)
{
    QList<PdfTileKey> missing;
    missing.reserve(keys.size());
    for (const auto& k : keys)
        if (!m_cache.contains(k) && !missing.contains(k))
            missing << k;
    m_worker->replace(missing, lane == Lane::View ? 0 : 1);
}

void PdfRenderQueue::setCacheLimitMb(int mb)
{
    m_cache.setMaxCost(qMax(16, mb) * 1024);
}

int PdfRenderQueue::pendingCount() const
{
    return m_worker->pending();
}

void PdfRenderQueue::onRendered(const PdfTileKey& key, const QImage& image)
{
    if (image.isNull())
        return;
    m_cache.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
    emit tileReady(key);
}

}  // namespace studylab
