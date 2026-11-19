#pragma once

#include "engines/pdf/PdfRenderQueue.h"

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QSet>
#include <QTimer>

class QPdfDocument;

namespace studylab::ui {

/// Miniatures des pages, rendues à la demande (seules les lignes affichées sont demandées).
class ThumbnailModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { ImageRole = Qt::UserRole + 1, LabelRole, MarkRole };

    ThumbnailModel(QPdfDocument* document, PdfRenderQueue* queue, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    /// Pastille de couleur des pages annotées (couleur de la première annotation de la page).
    void setMarks(const QHash<int, QColor>& marks);

private:
    QPdfDocument* m_doc;
    PdfRenderQueue* m_queue;
    mutable QList<PdfTileKey> m_missing;   ///< demandées depuis le dernier envoi
    QList<PdfTileKey> m_pending;           ///< envoyées et pas encore rendues
    mutable QTimer m_flush;
    QHash<int, QColor> m_marks;
};

}  // namespace studylab::ui
