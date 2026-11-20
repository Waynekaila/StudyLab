#pragma once

#include "core/domain/Annotation.h"
#include "engines/ocr/OcrPage.h"
#include "engines/pdf/PdfRenderQueue.h"

#include <QAbstractScrollArea>
#include <QList>
#include <QPdfSelection>
#include <QPointer>
#include <QRectF>
#include <QTransform>
#include <QHash>
#include <QTimer>
#include <QUrl>

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

/// Annotation telle que la vue la dessine (zones normalisées 0..1 dans la page non tournée).
struct PdfAnnotationMark {
    qint64 id = 0;
    int page = 0;
    AnnotationType type = AnnotationType::Highlight;
    QColor fill;    ///< teinte de surlignage
    QColor stroke;  ///< trait (soulignement, barré, zone, icône)
    QList<QRectF> rects;
    bool hasNote = false;
};

/// Ancre d'une sélection de texte sur une page (base d'une nouvelle annotation).
struct PdfSelectionAnchor {
    int page = 0;
    QList<QRectF> rects;  ///< normalisés, page non tournée
    QString text;
    QString prefix;
    QString suffix;
    int start = -1;
    int end = -1;
};

/// Vue de lecture PDF : défilement continu, zoom, rotation, double page, sélection de texte, liens,
/// surlignage des résultats de recherche, historique de navigation. Rendu par tuiles en tâche de fond.
class PdfView : public QAbstractScrollArea {
    Q_OBJECT
public:
    /// Le rendu a été invalidé (réglage du moteur) : redemande les pages visibles.
    void reloadPages();

    enum class ZoomMode { Custom, FitWidth, FitPage };
    /// Outil actif : la sélection de texte, un outil de marquage du texte, la zone ou la note.
    enum class Tool { Select, Highlight, Underline, Strikeout, Marker, Note };
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

    // Texte reconnu (OCR) des pages numérisées : sélection, copie, annotations et recherche comme le texte natif.
    void setOcrPages(const QHash<int, OcrPage>& pages);
    bool hasOcrText(int page) const { return m_ocr.contains(page); }

    // Recherche : résultats du modèle (texte natif) puis ceux du texte OCR.
    void setSearchModel(QPdfSearchModel* model);
    /// Cherche aussi dans le texte OCR (sans tenir compte de la casse).
    void setOcrSearchString(const QString& text);
    int searchResultCount() const;
    int searchResultPage(int index) const;
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

    // Annotations
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setAnnotations(const QList<PdfAnnotationMark>& marks);
    const QList<PdfAnnotationMark>& annotations() const { return m_marks; }
    /// Fait clignoter une annotation (2 × 300 ms) ; la vue doit déjà l'afficher.
    void flashAnnotation(qint64 id);
    /// Fait défiler jusqu'à une zone normalisée d'une page.
    void revealNormalizedRect(int page, const QRectF& rect, bool recordHistory = true);
    qint64 annotationAt(QPoint viewportPos) const;
    /// Ancres de la sélection courante, une par page.
    QList<PdfSelectionAnchor> selectionAnchors() const;
    /// Rectangle (coordonnées de la fenêtre de vue) englobant la sélection, vide sans sélection.
    QRect selectionViewportRect() const;
    QRect annotationViewportRect(qint64 id) const;
    QRectF normalizedToDoc(int page, const QRectF& norm) const;

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
    /// Sélection terminée à la souris (bouton relâché avec du texte sélectionné).
    void selectionFinished();
    void toolChanged(studylab::ui::PdfView::Tool tool);
    void annotationClicked(qint64 id);
    void annotationContextMenuRequested(qint64 id, QPoint globalPos);
    /// Zone tracée avec l'outil Zone (rectangle normalisé).
    void areaDrawn(int page, const QRectF& rect);
    /// Clic avec l'outil Note (point normalisé).
    void pointClicked(int page, const QPointF& point);

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
    struct PageSelection {
        int page = -1;
        QString text;
        QList<QRectF> rects;  ///< points de la page non tournée
        int start = -1;       ///< positions dans pageText(page)
        int end = -1;
        bool isEmpty() const { return text.isEmpty() || rects.isEmpty(); }
    };
    struct OcrHit {
        int page = -1;
        QList<QRectF> rects;  ///< points
    };
    PageSelection selectOnPage(int page, QPointF start, QPointF end) const;
    PageSelection selectOcr(int page, QPointF start, QPointF end, bool whole = false) const;
    static PageSelection fromPdf(int page, const QPdfSelection& sel);
    QList<QRectF> ocrRects(int page, int from, int to) const;  ///< mots couvrant [from, to[ du texte, une zone par ligne
    struct PageSlot {
        QSizeF points;      ///< taille de la page, en points, sans rotation
        QRectF rect;        ///< emplacement dans le document (pixels logiques, après rotation)
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
    void paintMarks(QPainter& p, int page);
    void paintMarkDecorations(QPainter& p, int page, const QTransform& toView);
    QRectF noteIconRect(const PdfAnnotationMark& m) const;    ///< coordonnées de la vue
    QRectF bookmarkRibbonRect(int page) const;                ///< coordonnées de la vue
    QString pageText(int page) const;

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

    // Annotations
    Tool m_tool = Tool::Select;
    QList<PdfAnnotationMark> m_marks;
    QHash<int, QList<int>> m_marksByPage;  ///< indices dans m_marks
    qint64 m_flashId = 0;
    int m_flashStep = 0;
    QTimer m_flashTimer;
    bool m_drawingArea = false;
    int m_areaPage = -1;
    QPointF m_areaStart;   ///< points de la page
    QPointF m_areaEnd;
    mutable QHash<int, QString> m_pageText;
    QHash<int, OcrPage> m_ocr;
    QList<OcrHit> m_ocrHits;
};

}  // namespace studylab::ui
