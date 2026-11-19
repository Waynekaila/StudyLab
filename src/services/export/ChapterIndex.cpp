#include "services/export/ChapterIndex.h"

#include "engines/epub/EpubPackage.h"

#include <QFileInfo>
#include <QPdfBookmarkModel>
#include <QPdfDocument>

#include <algorithm>
#include <functional>

namespace studylab {

ChapterIndex::ChapterIndex(QList<std::pair<int, QString>> entries)
    : m_entries(std::move(entries))
{
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
}

ChapterIndex ChapterIndex::forPdf(const QString& path)
{
    if (!QFileInfo::exists(path))
        return {};
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return {};
    QPdfBookmarkModel model;
    model.setDocument(&pdf);
    QList<std::pair<int, QString>> entries;
    std::function<void(const QModelIndex&, int)> walk = [&](const QModelIndex& parent, int depth) {
        for (int i = 0; i < model.rowCount(parent); ++i) {
            const QModelIndex idx = model.index(i, 0, parent);
            const QString title = idx.data(int(QPdfBookmarkModel::Role::Title)).toString().simplified();
            const int page = idx.data(int(QPdfBookmarkModel::Role::Page)).toInt();
            if (!title.isEmpty() && page >= 0)
                entries.append({page, title});
            if (depth < 1)
                walk(idx, depth + 1);
        }
    };
    walk(QModelIndex(), 0);
    return ChapterIndex(entries);
}

ChapterIndex ChapterIndex::forEpub(const QString& path)
{
    auto pkg = EpubPackage::open(path);
    if (!pkg)
        return {};
    QList<std::pair<int, QString>> entries;
    QList<int> seen;
    for (const EpubNavPoint& p : pkg.value()->toc()) {
        // Comme le lecteur : premier titre de premier ou second niveau de chaque section
        if (p.depth > 1 || p.section < 0 || seen.contains(p.section))
            continue;
        seen << p.section;
        entries.append({p.section, p.label});
    }
    return ChapterIndex(entries);
}

ChapterIndex ChapterIndex::forDocument(const QString& path, DocumentFormat format)
{
    return format == DocumentFormat::Epub ? forEpub(path) : forPdf(path);
}

QString ChapterIndex::chapterOf(int unit) const
{
    QString title;
    for (const auto& [u, t] : m_entries) {
        if (u > unit)
            break;
        title = t;
    }
    return title;
}

}  // namespace studylab
