#include "ui/reader/ThumbnailModel.h"

#include <QImage>
#include <QPdfDocument>

namespace studylab::ui {

ThumbnailModel::ThumbnailModel(QPdfDocument* document, PdfRenderQueue* queue, QObject* parent)
    : QAbstractListModel(parent), m_doc(document), m_queue(queue)
{
    m_flush.setSingleShot(true);
    m_flush.setInterval(0);
    connect(&m_flush, &QTimer::timeout, this, [this] {
        // Les dernières demandes passent en tête ; on garde les précédentes encore utiles (60 au plus).
        QList<PdfTileKey> wanted = m_missing;
        for (const auto& k : std::as_const(m_pending))
            if (!wanted.contains(k) && m_queue->cached(k).isNull())
                wanted << k;
        m_pending = wanted.mid(0, 60);
        m_missing.clear();
        m_queue->setWanted(m_pending, PdfRenderQueue::Lane::Background);
    });
    connect(queue, &PdfRenderQueue::tileReady, this, [this](const PdfTileKey& key) {
        m_pending.removeAll(key);
        if (key.isPreview() && key.page < rowCount())
            emit dataChanged(index(key.page), index(key.page), {ImageRole});
    });
}

int ThumbnailModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_doc ? 0 : m_doc->pageCount();
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const
{
    if (role == MarkRole) {
        const auto it = m_marks.constFind(index.row());
        return it == m_marks.constEnd() ? QVariant() : QVariant(*it);
    }
    if (!index.isValid())
        return {};
    const int page = index.row();
    switch (role) {
    case ImageRole: {
        const PdfTileKey key{page, PdfRenderQueue::kPreviewWidth, -1, -1};
        const QImage img = m_queue->cached(key);
        if (img.isNull() && !m_missing.contains(key)) {
            m_missing << key;
            m_flush.start();
        }
        return img;
    }
    case LabelRole:
    case Qt::DisplayRole: {
        const QString label = m_queue->pageLabel(page);
        return label.isEmpty() ? QString::number(page + 1) : label;
    }
    case Qt::AccessibleTextRole:
        return tr("Page %1").arg(page + 1);
    default:
        return {};
    }
}

void ThumbnailModel::setMarks(const QHash<int, QColor>& marks)
{
    QSet<int> changed;
    for (auto it = m_marks.cbegin(); it != m_marks.cend(); ++it)
        if (marks.value(it.key()) != it.value())
            changed.insert(it.key());
    for (auto it = marks.cbegin(); it != marks.cend(); ++it)
        if (m_marks.value(it.key()) != it.value())
            changed.insert(it.key());
    m_marks = marks;
    for (int row : changed)
        if (row >= 0 && row < rowCount())
            emit dataChanged(index(row), index(row), {MarkRole});
}

}  // namespace studylab::ui
