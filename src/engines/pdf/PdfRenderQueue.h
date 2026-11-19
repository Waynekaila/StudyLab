#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QThread>
#include <QWaitCondition>

#include <array>
#include <deque>

class QPdfDocument;

namespace studylab {

/// Identifie une image à rendre : une tuile d'une page à une échelle donnée, ou un aperçu de page entière.
struct PdfTileKey {
    int page = 0;
    int scaledWidth = 0;   ///< largeur de la page entière rendue (pixels physiques, sans rotation)
    int column = -1;       ///< -1 : aperçu de la page entière
    int row = -1;

    bool isPreview() const { return column < 0; }
    friend bool operator==(const PdfTileKey& a, const PdfTileKey& b)
    {
        return a.page == b.page && a.scaledWidth == b.scaledWidth && a.column == b.column && a.row == b.row;
    }
};

inline size_t qHash(const PdfTileKey& k, size_t seed = 0) noexcept
{
    return ::qHash(QByteArray::fromRawData(reinterpret_cast<const char*>(&k), sizeof(k)), seed);
}

/// Rendu des pages PDF en tâche de fond (un fil de travail dédié), par tuiles.
///
/// - Le fil de l'interface déclare ce qu'il veut voir (`setWanted`), par ordre de priorité ; tout ce qui
///   n'est plus demandé est abandonné avant d'être rendu (défilement rapide, changement de zoom).
/// - Les images rendues sont gardées dans un cache LRU borné en mémoire.
class PdfRenderQueue : public QObject {
    Q_OBJECT
public:
    static constexpr int kTileSize = 512;       ///< côté d'une tuile, en pixels physiques
    static constexpr int kPreviewWidth = 280;   ///< largeur des aperçus (miniatures, attente des tuiles)

    explicit PdfRenderQueue(QPdfDocument* document, QObject* parent = nullptr);
    ~PdfRenderQueue() override;

    /// Taille de la page entière, en pixels physiques, pour une largeur rendue donnée.
    QSize scaledPageSize(int page, int scaledWidth) const;
    /// Rectangle (pixels physiques) couvert par une tuile.
    QRect tileRect(const PdfTileKey& key) const;

    /// Image en cache (nulle si pas encore rendue). Ne déclenche aucun rendu.
    QImage cached(const PdfTileKey& key) const;
    /// File prioritaire (vue de lecture) ou secondaire (miniatures), servie seulement quand la première est vide.
    enum class Lane { View, Background };
    /// Remplace la file indiquée par ces demandes, dans l'ordre de priorité. Les images en cache sont ignorées.
    void setWanted(const QList<PdfTileKey>& keys, Lane lane = Lane::View);

    void setCacheLimitMb(int mb);
    int pendingCount() const;

signals:
    void tileReady(const studylab::PdfTileKey& key);

private:
    class Worker;
    void onRendered(const PdfTileKey& key, const QImage& image);

    QPdfDocument* m_document;
    QList<QSizeF> m_pageSizes;  ///< points, lus une fois (accès sans verrou depuis l'interface)
    mutable QCache<PdfTileKey, QImage> m_cache;
    Worker* m_worker;
};

}  // namespace studylab

Q_DECLARE_METATYPE(studylab::PdfTileKey)
