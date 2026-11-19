#include "ui/library/LibraryModel.h"

#include "core/Log.h"
#include "services/library/LibraryService.h"

#include <QFileInfo>

namespace studylab::ui {

LibraryModel::LibraryModel(LibraryService& library, QObject* parent)
    : QAbstractListModel(parent), m_library(library), m_covers(600)
{
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const DocumentRecord& r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::AccessibleTextRole:
        return r.authors.isEmpty() ? r.title : r.title + QStringLiteral(" — ") + r.authors.join(QStringLiteral(", "));
    case Qt::ToolTipRole:
        return r.title;
    case IdRole:
        return r.id;
    case RecordRole:
        return QVariant::fromValue(&r);
    case CoverRole:
        return cover(r);
    default:
        return {};
    }
}

void LibraryModel::setQuery(const db::DocumentQuery& query)
{
    m_query = query;
    reload();
}

void LibraryModel::reload()
{
    auto rows = m_library.documents(m_query);
    if (!rows) {
        qCWarning(lcUi) << "Lecture de la bibliothèque impossible :" << rows.error().message;
        return;
    }
    beginResetModel();
    m_rows = std::move(rows.value());
    endResetModel();
}

const DocumentRecord* LibraryModel::recordAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? &m_rows.at(row) : nullptr;
}

int LibraryModel::rowOf(qint64 id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id)
            return i;
    return -1;
}

QImage LibraryModel::cover(const DocumentRecord& record) const
{
    if (record.coverPath.isEmpty())
        return {};
    if (const QImage* cached = m_covers.object(record.coverPath))
        return *cached;
    auto* img = new QImage(m_library.coverFile(record));
    const QImage copy = *img;
    m_covers.insert(record.coverPath, img);
    return copy;
}

}  // namespace studylab::ui
