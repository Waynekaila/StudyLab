#pragma once

#include "engines/pdf/PdfRenderQueue.h"

#include <QAbstractScrollArea>
#include <QList>
#include <QPdfSelection>
#include <QPointer>
#include <QRectF>
#include <QTransform>
#include <QUrl>
#include <QHash>

class QPdfDocument;
class QPdfSearchModel;

namespace studylab::ui {

class ThemeManager;

/// Position de lecture indépendante du zoom : page et décalage vertical, en points, depuis le haut
/// de la page telle qu'elle est affichée (après rotation).
struct PdfPosition {
    int page = 0;
    qreal offset = 0;
    bool operator==(const PdfPosition& o) const { return page == o.page && qAbs(offset - o.offset) < 0.5; }
};

/// Vue de lecture PDF : défilement continu, zoom, rotation, double page, sélection de texte, liens,
/// surlignage des résultats de recherche, historique de navigation. Rendu par tuiles en tâche de fond.
class PdfView : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum class ZoomMode { Custom, FitWidth, FitPage };
    static constexpr qreal kMinZoom = 0.1;
    static constexpr qreal kMaxZoom = 8.0;

    explicit PdfView(ThemeManager& theme, QWidget* parent = nullptr);
    ~PdfView() override;

    /// Le document et la file de rendu appartiennent à l'appelant et doivent survivre à la vue.
    void setDocument(QPdfDocument* document, PdfRenderQueue* queue);
    QPdfDocument* document() const { return m_doc; }
    int pageCount() const { return int(m_pages.size()); }

    // Navigation
    int currentPage() const { return m_currentPage; }
    PdfPosition position() const;
    void setPosition(const PdfPosition& pos);
    /// Va à une page, éventuellement à une ordonnée en points de la page non tournée (liens, sommaire, recherche).
    /// L'ancienne position entre dans l'historique.
    void goToPage(int page, qreal y = -1, bool recordHistory = true);
    void nextPage();
    void previousPage();
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    void goBack();
    void goForward();

    // Affichage
    qreal zoom() const { return m_zoom; }
    ZoomMode zoomMode() const { return m_zoomMode; }
    void setZoom(qreal zoom);
    void setZoomMode(ZoomMode mode);
    void zoomIn();
    void zoomOut();
    int rotation() const { return m_rotation; }
    void setRotation(int degrees);
    void rotateClockwise() { setRotation(m_rotation + 90); }
    bool doublePage() const { return m_doublePage; }
    void setDoublePage(bool on);

    // Recherche
    void setSearchModel(QPdfSearchModel* model);
    /// Met en évidence un résultat et le fait défiler à l'écran (-1 : aucun).
    void setCurrentSearchResult(int index);
    int currentSearchResult() const { return m_currentResult; }

    // Sélection
    QString selectedText() const;
    bool hasSelection() const { return !m_selection.isEmpty(); }
    void clearSelection();
    void copySelection();
    /// Sélectionne le texte entre deux points (coordonnées de page en points), éventuellement sur plusieurs pages.
    void selectText(int startPage, QPointF start, int endPage, QPointF end);

    // Correspondances de coordonnées (aussi utilisées par les tests et, plus tard, les annotations)
    QRectF pageRect(int page) const;  ///< rectangle de la page dans le document (pixels logiques)
    int pageAt(QPoint viewportPos) const;
    QPointF mapToPage(int page, QPoint viewportPos) const;
    QPoint mapFromPage(int page, QPointF pagePoint) const;

signals:
    void currentPageChanged(int page);
    void zoomChanged(qreal zoom);
    void positionChanged();
    void historyChanged();
    void selectionChanged();
    void layoutChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    QPdfSelection selectOnPage(int page, QPointF start, QPointF end) const;
    struct PageSlot {
        QSizeF points;      ///< taille de la page, en points, sans rotation
        QRectF rect;        ///< emplacement dans le document (pixels logiques, après rotation)
    };
    struct PageSelection {
        int page;
        QPdfSelection selection;
    };
    struct LinkHit {
        bool valid = false;
        int page = -1;
        qreal y = 0;
        QUrl url;
    };

    qreal scale() const;  ///< pixels logiques par point
    QSizeF rotatedSize(const PageSlot& slot) const;
    void relayout(bool keepPosition = true);
    void updateScrollBars();
    qreal fitZoom(ZoomMode mode) const;
    QTransform pageTransform(int page) const;  ///< points de la page → document (pixels logiques)
    QRectF visibleDocRect() const;
    void updateCurrentPage();
    void requestRender();
    void paintPage(QPainter& p, int page, const QRectF& clipDoc);
    void pushHistory();
    void scrollToDoc(QPointF docPoint);
    LinkHit linkAt(QPoint viewportPos) const;
    void setZoomAround(qreal zoom, QPoint viewportAnchor);

    ThemeManager& m_theme;
    QPdfDocument* m_doc = nullptr;
    PdfRenderQueue* m_queue = nullptr;
    QPointer<QPdfSearchModel> m_search;
    QList<PageSlot> m_pages;
    QSizeF m_docSize;
    qreal m_zoom = 1.0;
    ZoomMode m_zoomMode = ZoomMode::FitWidth;
    int m_rotation = 0;
    bool m_doublePage = false;
    int m_currentPage = 0;
    int m_currentResult = -1;
    QList<PdfPosition> m_back;
    QList<PdfPosition> m_forward;

    // Sélection en cours
    bool m_selecting = false;
    int m_selStartPage = -1;
    QPointF m_selStart;
    QPoint m_pressPos;
    QList<PageSelection> m_selection;
    LinkHit m_pressLink;
    mutable QHash<int, QList<QPair<QRectF, LinkHit>>> m_links;  ///< liens par page (points), lus à la demande
    bool m_inRelayout = false;
};

}  // namespace studylab::ui
