#pragma once

#include "core/domain/Document.h"
#include "data/repositories/DocumentRepository.h"

#include <QAbstractListModel>
#include <QCache>
#include <QImage>

namespace studylab {
class LibraryService;
}

namespace studylab::ui {

/// Documents de la bibliothèque affichés en grille ou en liste, selon les filtres courants.
class LibraryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { RecordRole = Qt::UserRole + 1, IdRole, CoverRole };

    explicit LibraryModel(LibraryService& library, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setQuery(const db::DocumentQuery& query);
    const db::DocumentQuery& query() const { return m_query; }
    /// Relit les documents en conservant la requête.
    void reload();

    const DocumentRecord* recordAt(int row) const;
    int rowOf(qint64 id) const;
    /// Couverture (image redimensionnée), nulle si absente. Mise en cache.
    QImage cover(const DocumentRecord& record) const;

private:
    LibraryService& m_library;
    db::DocumentQuery m_query;
    QList<DocumentRecord> m_rows;
    mutable QCache<QString, QImage> m_covers;
};

}  // namespace studylab::ui
