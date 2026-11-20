#include "ui/reader/PdfView.h"

#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPdfDocument>
#include <QPdfSearchModel>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

#if __has_include(<QtPdf/qpdflinkmodel.h>)
#include <QPdfLinkModel>
#define STUDYLAB_HAS_PDF_LINKS 1
#endif

namespace studylab::ui {

namespace {
constexpr qreal kMargin = 24;    // marge autour des pages (px logiques)
constexpr qreal kRowGap = 16;    // espace entre deux rangées de pages
constexpr qreal kPageGap = 12;   // espace entre deux pages côte à côte
constexpr qreal kPointToPx = 96.0 / 72.0;
const QList<qreal> kZoomSteps = {0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0, 6.0, 8.0};
}  // namespace

PdfView::PdfView(ThemeManager& theme, QWidget* parent) : QAbstractScrollArea(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("PdfView"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::IBeamCursor);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);
    setAccessibleName(tr("Page du document"));
    connect(&theme, &ThemeManager::themeChanged, viewport(), qOverload<>(&QWidget::update));
    m_flashTimer.setInterval(300);
    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        if (++m_flashStep >= 4) {
            m_flashTimer.stop();
            m_flashId = 0;
        }
        viewport()->update();
    });
}

PdfView::~PdfView() = default;

void PdfView::setDocument(QPdfDocument* document, PdfRenderQueue* queue)
{
    if (m_queue)
        disconnect(m_queue, nullptr, this, nullptr);
    m_doc = document;
    m_queue = queue;
    m_pages.clear();
    m_links.clear();
    m_back.clear();
    m_forward.clear();
    m_selection.clear();
    m_marks.clear();
    m_marksByPage.clear();
    m_pageText.clear();
    m_ocr.clear();
    m_ocrHits.clear();
    m_currentPage = 0;
    if (m_doc) {
        for (int i = 0; i < m_doc->pageCount(); ++i)
            m_pages.append(PageSlot{m_doc->pagePointSize(i), QRectF()});
    }
    if (m_queue)
        connect(m_queue, &PdfRenderQueue::tileReady, this, [this](const PdfTileKey& key) {
            if (key.page >= 0 && key.page < m_pages.size() && m_pages[key.page].rect.intersects(visibleDocRect()))
                viewport()->update();
        });
    relayout(false);
    verticalScrollBar()->setValue(0);
    updateCurrentPage();
    emit historyChanged();
}

// ───────────────────────── Mise en page ─────────────────────────

qreal PdfView::scale() const
{
    return m_zoom * kPointToPx;
}

QSizeF PdfView::rotatedSize(const PageSlot& slot) const
{
    return (m_rotation % 180) ? slot.points.transposed() : slot.points;
}

qreal PdfView::fitZoom(ZoomMode mode) const
{
    if (m_pages.isEmpty())
        return 1.0;
    const qreal availW = qMax<qreal>(100, viewport()->width() - 2 * kMargin);
    const qreal availH = qMax<qreal>(100, viewport()->height() - 2 * kMargin);
    // Largeur de rangée la plus grande (une ou deux pages), en points
    qreal rowW = 0;
    qreal pageH = 0;
    for (int i = 0; i < m_pages.size(); ++i) {
        const QSizeF s = rotatedSize(m_pages[i]);
        qreal w = s.width();
        if (m_doublePage && i > 0 && i % 2 == 1 && i + 1 < m_pages.size())
            w += kPageGap / kPointToPx + rotatedSize(m_pages[i + 1]).width();
        rowW = qMax(rowW, w);
        pageH = qMax(pageH, s.height());
    }
    qreal z = availW / (rowW * kPointToPx);
    if (mode == ZoomMode::FitPage)
        z = qMin(z, availH / (pageH * kPointToPx));
    return std::clamp(z, kMinZoom, kMaxZoom);
}

void PdfView::relayout(bool keepPosition)
{
    if (m_inRelayout)
        return;
    m_inRelayout = true;
    const PdfPosition pos = position();
    if (m_zoomMode != ZoomMode::Custom)
        m_zoom = fitZoom(m_zoomMode);
    const qreal s = scale();

    // Rangées : une page, ou deux côte à côte (la première page seule, comme une couverture).
    QList<QList<int>> rows;
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_doublePage && i > 0 && i % 2 == 1 && i + 1 < m_pages.size()) {
            rows.append(QList<int>{i, i + 1});
            ++i;
        } else {
            rows.append(QList<int>{i});
        }
    }
    qreal maxRowW = 0;
    for (const auto& row : rows) {
        qreal w = 0;
        for (int i : row)
            w += rotatedSize(m_pages[i]).width() * s;
        w += kPageGap * (row.size() - 1);
        maxRowW = qMax(maxRowW, w);
    }
    const qreal docW = qMax(maxRowW + 2 * kMargin, qreal(viewport()->width()));
    qreal y = kMargin;
    for (const auto& row : rows) {
        qreal w = 0, h = 0;
        for (int i : row) {
            const QSizeF sz = rotatedSize(m_pages[i]) * s;
            w += sz.width();
            h = qMax(h, sz.height());
        }
        w += kPageGap * (row.size() - 1);
        qreal x = (docW - w) / 2;
        for (int i : row) {
            const QSizeF sz = rotatedSize(m_pages[i]) * s;
            m_pages[i].rect = QRectF(QPointF(std::round(x), std::round(y + (h - sz.height()) / 2)), sz);
            x += sz.width() + kPageGap;
        }
        y += h + kRowGap;
    }
    m_docSize = QSizeF(docW, y - kRowGap + kMargin);
    updateScrollBars();
    if (keepPosition && !m_pages.isEmpty())
        setPosition(pos);
    m_inRelayout = false;
    viewport()->update();
    requestRender();
    emit layoutChanged();
}

void PdfView::updateScrollBars()
{
    const QSize vp = viewport()->size();
    verticalScrollBar()->setPageStep(vp.height());
    horizontalScrollBar()->setPageStep(vp.width());
    verticalScrollBar()->setRange(0, qMax(0, int(std::ceil(m_docSize.height())) - vp.height()));
    horizontalScrollBar()->setRange(0, qMax(0, int(std::ceil(m_docSize.width())) - vp.width()));
}

QTransform PdfView::pageTransform(int page) const
{
    const PageSlot& slot = m_pages.at(page);
    const qreal w = slot.points.width();
    const qreal h = slot.points.height();
    QTransform t;
    t.translate(slot.rect.x(), slot.rect.y());
    t.scale(scale(), scale());
    switch (m_rotation) {
    case 90:
        t.translate(h, 0);
        t.rotate(90);
        break;
    case 180:
        t.translate(w, h);
        t.rotate(180);
        break;
    case 270:
        t.translate(0, w);
        t.rotate(270);
        break;
    default:
        break;
    }
    return t;
}

QRectF PdfView::visibleDocRect() const
{
    return QRectF(horizontalScrollBar()->value(), verticalScrollBar()->value(), viewport()->width(),
                  viewport()->height());
}

QRectF PdfView::pageRect(int page) const
{
    return page >= 0 && page < m_pages.size() ? m_pages[page].rect : QRectF();
}

int PdfView::pageAt(QPoint viewportPos) const
{
    const QPointF doc = viewportPos + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages[i].rect.contains(doc))
            return i;
    return -1;
}

QPointF PdfView::mapToPage(int page, QPoint viewportPos) const
{
    const QPointF doc = viewportPos + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    return pageTransform(page).inverted().map(doc);
}

QPoint PdfView::mapFromPage(int page, QPointF pagePoint) const
{
    const QPointF doc = pageTransform(page).map(pagePoint);
    return (doc - QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value())).toPoint();
}

// ───────────────────────── Position et navigation ─────────────────────────

PdfPosition PdfView::position() const
{
    if (m_pages.isEmpty())
        return {};
    const qreal top = verticalScrollBar()->value();
    int page = 0;
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].rect.bottom() + kRowGap > top) {
            page = i;
            break;
        }
        page = i;
    }
    const qreal offset = (top - m_pages[page].rect.top()) / scale();
    return {page, qMax<qreal>(-kMargin / scale(), offset)};
}

void PdfView::setPosition(const PdfPosition& pos)
{
    if (m_pages.isEmpty())
        return;
    const int page = std::clamp(pos.page, 0, int(m_pages.size()) - 1);
    verticalScrollBar()->setValue(qRound(m_pages[page].rect.top() + pos.offset * scale()));
    updateCurrentPage();
}

void PdfView::scrollToDoc(QPointF docPoint)
{
    verticalScrollBar()->setValue(qRound(docPoint.y()));
    const QRectF vis = visibleDocRect();
    if (docPoint.x() < vis.left() || docPoint.x() > vis.right())
        horizontalScrollBar()->setValue(qRound(docPoint.x() - vis.width() / 3));
}

void PdfView::pushHistory()
{
    const PdfPosition here = position();
    if (m_back.isEmpty() || !(m_back.last() == here))
        m_back.append(here);
    if (m_back.size() > 100)
        m_back.removeFirst();
    m_forward.clear();
    emit historyChanged();
}

void PdfView::goToPage(int page, qreal y, bool recordHistory)
{
    if (m_pages.isEmpty())
        return;
    page = std::clamp(page, 0, int(m_pages.size()) - 1);
    if (recordHistory)
        pushHistory();
    if (y < 0) {
        verticalScrollBar()->setValue(qRound(m_pages[page].rect.top() - kRowGap / 2));
    } else {
        const QPointF doc = pageTransform(page).map(QPointF(m_pages[page].points.width() / 2, y));
        scrollToDoc(QPointF(doc.x(), doc.y() - 24));
    }
    updateCurrentPage();
}

void PdfView::nextPage()
{
    const int step = m_doublePage && m_currentPage > 0 ? 2 : 1;
    goToPage(m_currentPage + step, -1, false);
}

void PdfView::previousPage()
{
    const int step = m_doublePage && m_currentPage > 2 ? 2 : 1;
    goToPage(m_currentPage - step, -1, false);
}

void PdfView::goBack()
{
    if (m_back.isEmpty())
        return;
    m_forward.append(position());
    setPosition(m_back.takeLast());
    emit historyChanged();
}

void PdfView::goForward()
{
    if (m_forward.isEmpty())
        return;
    m_back.append(position());
    setPosition(m_forward.takeLast());
    emit historyChanged();
}

void PdfView::updateCurrentPage()
{
    if (m_pages.isEmpty())
        return;
    // Page courante : celle qui occupe le tiers supérieur de la fenêtre.
    const QRectF vis = visibleDocRect();
    const qreal probe = vis.top() + vis.height() / 3;
    int best = 0;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_pages.size(); ++i) {
        const QRectF r = m_pages[i].rect;
        const qreal d = (probe >= r.top() && probe <= r.bottom()) ? 0 : qMin(qAbs(probe - r.top()), qAbs(probe - r.bottom()));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
        if (r.top() > probe + vis.height())
            break;
    }
    if (best != m_currentPage) {
        m_currentPage = best;
        emit currentPageChanged(best);
    }
}

// ───────────────────────── Zoom, rotation, double page ─────────────────────────

void PdfView::setZoom(qreal zoom)
{
    setZoomAround(zoom, QPoint(viewport()->width() / 2, viewport()->height() / 3));
}

void PdfView::setZoomAround(qreal zoom, QPoint anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (m_zoomMode == ZoomMode::Custom && qFuzzyCompare(zoom, m_zoom))
        return;
    // Le point du document sous l'ancre reste sous l'ancre après le zoom.
    const int page = pageAt(anchor) >= 0 ? pageAt(anchor) : m_currentPage;
    const QPointF pagePt = m_pages.isEmpty() ? QPointF() : mapToPage(page, anchor);
    m_zoomMode = ZoomMode::Custom;
    m_zoom = zoom;
    relayout(false);
    if (!m_pages.isEmpty()) {
        const QPointF doc = pageTransform(page).map(pagePt);
        horizontalScrollBar()->setValue(qRound(doc.x() - anchor.x()));
        verticalScrollBar()->setValue(qRound(doc.y() - anchor.y()));
    }
    updateCurrentPage();
    emit zoomChanged(m_zoom);
}

void PdfView::setZoomMode(ZoomMode mode)
{
    m_zoomMode = mode;
    if (mode != ZoomMode::Custom) {
        relayout(true);
        emit zoomChanged(m_zoom);
    }
}

void PdfView::zoomIn()
{
    for (qreal step : kZoomSteps)
        if (step > m_zoom + 0.001)
            return setZoom(step);
}

void PdfView::zoomOut()
{
    for (auto it = kZoomSteps.crbegin(); it != kZoomSteps.crend(); ++it)
        if (*it < m_zoom - 0.001)
            return setZoom(*it);
}

void PdfView::setRotation(int degrees)
{
    degrees = ((degrees % 360) + 360) % 360;
    degrees -= degrees % 90;
    if (degrees == m_rotation)
        return;
    const int page = m_currentPage;
    m_rotation = degrees;
    relayout(false);
    goToPage(page, -1, false);
}

void PdfView::setDoublePage(bool on)
{
    if (on == m_doublePage)
        return;
    const int page = m_currentPage;
    m_doublePage = on;
    relayout(false);
    goToPage(page, -1, false);
}

// ───────────────────────── Recherche ─────────────────────────

void PdfView::setSearchModel(QPdfSearchModel* model)
{
    if (m_search)
        disconnect(m_search, nullptr, this, nullptr);
    m_search = model;
    m_currentResult = -1;
    if (m_search) {
        connect(m_search, &QAbstractItemModel::modelReset, viewport(), qOverload<>(&QWidget::update));
        connect(m_search, &QAbstractItemModel::rowsInserted, viewport(), qOverload<>(&QWidget::update));
    }
    viewport()->update();
}

void PdfView::setOcrPages(const QHash<int, OcrPage>& pages)
{
    m_ocr = pages;
    m_pageText.clear();
    viewport()->update();
}

void PdfView::setOcrSearchString(const QString& text)
{
    m_ocrHits.clear();
    const QString needle = text.trimmed();
    if (!needle.isEmpty()) {
        QList<int> pages = m_ocr.keys();
        std::sort(pages.begin(), pages.end());
        for (int page : pages) {
            const QString hay = pageText(page);
            for (qsizetype at = hay.indexOf(needle, 0, Qt::CaseInsensitive); at >= 0;
                 at = hay.indexOf(needle, at + needle.size(), Qt::CaseInsensitive)) {
                const QList<QRectF> rects = ocrRects(page, int(at), int(at + needle.size()));
                if (!rects.isEmpty())
                    m_ocrHits << OcrHit{page, rects};
            }
        }
    }
    m_currentResult = -1;
    viewport()->update();
}

int PdfView::searchResultCount() const
{
    return (m_search ? m_search->rowCount({}) : 0) + int(m_ocrHits.size());
}

int PdfView::searchResultPage(int index) const
{
    const int native = m_search ? m_search->rowCount({}) : 0;
    if (index >= 0 && index < native)
        return m_search->resultAtIndex(index).page();
    if (index >= native && index - native < m_ocrHits.size())
        return m_ocrHits.at(index - native).page;
    return -1;
}

void PdfView::setCurrentSearchResult(int index)
{
    m_currentResult = index;
    const int native = m_search ? m_search->rowCount({}) : 0;
    int page = -1;
    QList<QRectF> rects;
    if (index >= 0 && index < native) {
        const QPdfLink link = m_search->resultAtIndex(index);
        page = link.page();
        rects = link.rectangles();
    } else if (index >= native && index - native < m_ocrHits.size()) {
        page = m_ocrHits.at(index - native).page;
        rects = m_ocrHits.at(index - native).rects;
    }
    if (!rects.isEmpty() && page >= 0 && page < m_pages.size()) {
        const QRectF docRect = pageTransform(page).mapRect(rects.first());
        const QRectF vis = visibleDocRect();
        if (!vis.adjusted(0, 40, 0, -40).contains(docRect)) {
            pushHistory();
            verticalScrollBar()->setValue(qRound(docRect.center().y() - vis.height() / 3));
            if (docRect.left() < vis.left() || docRect.right() > vis.right())
                horizontalScrollBar()->setValue(qRound(docRect.center().x() - vis.width() / 2));
        }
        updateCurrentPage();
    }
    viewport()->update();
}

// ───────────────────────── Sélection ─────────────────────────

PdfView::PageSelection PdfView::selectOnPage(int page, QPointF start, QPointF end) const
{
    if (m_ocr.contains(page))
        return selectOcr(page, start, end);
    QPdfSelection sel = m_doc->getSelection(page, start, end);
    if (sel.isValid() && !sel.text().isEmpty())
        return fromPdf(page, sel);
    // Qt PDF n'accepte que des extrémités posées sur un caractère : hors du texte (marge, interligne),
    // chaque extrémité est ramenée au caractère le plus proche.
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid() || all.text().isEmpty())
        return {};
    QRectF box;
    QList<QRectF> chars;
    for (const QPolygonF& b : all.bounds()) {
        const QRectF r = b.boundingRect();
        box |= r;
        if (r.width() > 0.5 && r.height() > 2)
            chars << r;
    }
    const QRectF wanted = QRectF(start, end).normalized();
    if (wanted.contains(box))
        return fromPdf(page, all);
    const auto nearest = [&chars](QPointF pt) {
        QPointF best = pt;
        qreal bestD = std::numeric_limits<qreal>::max();
        for (const QRectF& r : chars) {
            // Distance verticale prépondérante : on reste sur la ligne visée.
            const qreal dx = pt.x() < r.left() ? r.left() - pt.x() : pt.x() > r.right() ? pt.x() - r.right() : 0;
            const qreal dy = pt.y() < r.top() ? r.top() - pt.y() : pt.y() > r.bottom() ? pt.y() - r.bottom() : 0;
            const qreal d = dy * 4 + dx;
            if (d < bestD) {
                bestD = d;
                best = r.center();
            }
        }
        return best;
    };
    return fromPdf(page, m_doc->getSelection(page, nearest(start), nearest(end)));
}

PdfView::PageSelection PdfView::fromPdf(int page, const QPdfSelection& sel)
{
    PageSelection out;
    out.page = page;
    if (!sel.isValid())
        return out;
    out.text = sel.text();
    for (const QPolygonF& poly : sel.bounds()) {
        const QRectF r = poly.boundingRect();
        if (!r.isEmpty())
            out.rects << r;
    }
    out.start = sel.startIndex();
    out.end = sel.endIndex();
    return out;
}

PdfView::PageSelection PdfView::selectOcr(int page, QPointF start, QPointF end, bool whole) const
{
    PageSelection out;
    out.page = page;
    const OcrPage& ocr = m_ocr[page];
    if (ocr.words.isEmpty() || (!whole && QLineF(start, end).length() < 1.0))
        return out;
    const QSizeF size = m_pages[page].points;
    const auto toPoints = [&size](const QRectF& n) {
        return QRectF(n.x() * size.width(), n.y() * size.height(), n.width() * size.width(), n.height() * size.height());
    };
    // Mot sous le point, sinon le plus proche (distance verticale prépondérante : on reste sur la ligne visée).
    const auto nearest = [&](QPointF pt) {
        int best = 0;
        qreal bestD = std::numeric_limits<qreal>::max();
        for (int i = 0; i < ocr.words.size(); ++i) {
            const QRectF r = toPoints(ocr.words.at(i).box);
            const qreal dx = pt.x() < r.left() ? r.left() - pt.x() : pt.x() > r.right() ? pt.x() - r.right() : 0;
            const qreal dy = pt.y() < r.top() ? r.top() - pt.y() : pt.y() > r.bottom() ? pt.y() - r.bottom() : 0;
            const qreal d = dy * 4 + dx;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    };
    int first = whole ? 0 : nearest(start);
    int last = whole ? int(ocr.words.size()) - 1 : nearest(end);
    if (last < first)
        std::swap(first, last);
    const QList<int> offsets = ocr.offsets();
    out.start = offsets.at(first);
    out.end = offsets.at(last) + int(ocr.words.at(last).text.size());
    out.text = pageText(page).mid(out.start, out.end - out.start);
    out.rects = ocrRects(page, out.start, out.end);
    return out;
}

QList<QRectF> PdfView::ocrRects(int page, int from, int to) const
{
    QList<QRectF> lines;
    const auto it = m_ocr.constFind(page);
    if (it == m_ocr.constEnd() || page < 0 || page >= m_pages.size())
        return lines;
    const QSizeF size = m_pages[page].points;
    const QList<int> offsets = it->offsets();
    for (int i = 0; i < it->words.size(); ++i) {
        const OcrWord& w = it->words.at(i);
        const int a = offsets.at(i);
        const int b = a + int(w.text.size());
        if (b <= from || a >= to)
            continue;
        QRectF r(w.box.x() * size.width(), w.box.y() * size.height(), w.box.width() * size.width(),
                 w.box.height() * size.height());
        // Partie d'un mot (terme cherché suivi d'une ponctuation) : largeur au prorata des caractères.
        if (from > a || to < b) {
            const qreal perChar = r.width() / std::max(1, b - a);
            const int lo = std::max(from, a) - a;
            const int hi = std::min(to, b) - a;
            r = QRectF(r.x() + lo * perChar, r.y(), (hi - lo) * perChar, r.height());
        }
        // Mots d'une même ligne : une seule zone, espaces compris.
        if (!lines.isEmpty() && w.breakBefore == OcrWord::None && i > 0 && offsets.at(i - 1) >= from)
            lines.last() = lines.last().united(r);
        else
            lines << r;
    }
    return lines;
}



void PdfView::selectText(int startPage, QPointF start, int endPage, QPointF end)
{
    m_selection.clear();
    if (!m_doc || startPage < 0 || endPage < 0) {
        emit selectionChanged();
        viewport()->update();
        return;
    }
    if (endPage < startPage) {
        std::swap(startPage, endPage);
        std::swap(start, end);
    }
    for (int p = startPage; p <= endPage; ++p) {
        const QSizeF size = m_pages[p].points;
        const PageSelection sel = startPage == endPage ? selectOnPage(p, start, end)
                                  : p == startPage    ? selectOnPage(p, start, QPointF(size.width(), size.height()))
                                  : p == endPage      ? selectOnPage(p, QPointF(0, 0), end)
                                  : m_ocr.contains(p) ? selectOcr(p, {}, {}, true)
                                                      : fromPdf(p, m_doc->getAllText(p));
        if (!sel.isEmpty())
            m_selection.append(sel);
    }
    emit selectionChanged();
    viewport()->update();
}

QString PdfView::selectedText() const
{
    QStringList parts;
    for (const auto& s : m_selection)
        parts << s.text;
    return parts.join(QLatin1Char('\n'));
}

void PdfView::clearSelection()
{
    if (m_selection.isEmpty())
        return;
    m_selection.clear();
    emit selectionChanged();
    viewport()->update();
}

void PdfView::copySelection()
{
    if (hasSelection())
        QApplication::clipboard()->setText(selectedText());
}

// ───────────────────────── Liens ─────────────────────────

PdfView::LinkHit PdfView::linkAt(QPoint viewportPos) const
{
#ifdef STUDYLAB_HAS_PDF_LINKS
    const int page = pageAt(viewportPos);
    if (page < 0 || !m_doc)
        return {};
    if (!m_links.contains(page)) {
        QList<QPair<QRectF, LinkHit>> list;
        QPdfLinkModel model;
        model.setDocument(m_doc);
        model.setPage(page);
        for (int r = 0; r < model.rowCount({}); ++r) {
            const QModelIndex idx = model.index(r, 0);
            const QRectF rect = idx.data(int(QPdfLinkModel::Role::Rectangle)).toRectF();
            LinkHit hit;
            hit.valid = true;
            hit.url = idx.data(int(QPdfLinkModel::Role::Url)).toUrl();
            hit.page = idx.data(int(QPdfLinkModel::Role::Page)).toInt();
            hit.y = idx.data(int(QPdfLinkModel::Role::Location)).toPointF().y();
            if (!rect.isEmpty() && (hit.url.isValid() || hit.page >= 0))
                list.append({rect, hit});
        }
        m_links.insert(page, list);
    }
    const QPointF pt = mapToPage(page, viewportPos);
    for (const auto& [rect, hit] : m_links.value(page))
        if (rect.contains(pt))
            return hit;
#else
    Q_UNUSED(viewportPos)
#endif
    return {};
}

// ───────────────────────── Rendu ─────────────────────────

void PdfView::requestRender()
{
    if (!m_queue || m_pages.isEmpty())
        return;
    const qreal dpr = devicePixelRatioF();
    const QRectF vis = visibleDocRect();
    const QRectF prefetch = vis.adjusted(0, -vis.height() / 2, 0, vis.height() / 2);
    const QPointF center = vis.center();

    QList<PdfTileKey> previews, tiles, later;
    auto addTiles = [&](int page, const QRectF& region, QList<PdfTileKey>& out) {
        const PageSlot& slot = m_pages[page];
        const int scaledWidth = qRound(slot.points.width() * scale() * dpr);
        if (scaledWidth <= 0)
            return;
        const QRectF docPart = region.intersected(slot.rect);
        if (docPart.isEmpty())
            return;
        const QRectF pts = pageTransform(page).inverted().mapRect(docPart);
        const qreal f = scaledWidth / slot.points.width();
        const QRectF px(pts.topLeft() * f, pts.bottomRight() * f);
        const int t = PdfRenderQueue::kTileSize;
        QList<QPair<qreal, PdfTileKey>> found;
        for (int r = qMax(0, int(px.top() / t)); r <= int((px.bottom() - 1) / t); ++r)
            for (int c = qMax(0, int(px.left() / t)); c <= int((px.right() - 1) / t); ++c) {
                const PdfTileKey key{page, scaledWidth, c, r};
                const QRectF tileDoc = pageTransform(page).mapRect(QRectF(QRectF(m_queue->tileRect(key)).topLeft() / f,
                                                                          QRectF(m_queue->tileRect(key)).size() / f));
                found.append({QLineF(tileDoc.center(), center).length(), key});
            }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& f2 : found)
            out << f2.second;
    };
    for (int i = 0; i < m_pages.size(); ++i) {
        const QRectF r = m_pages[i].rect;
        if (r.top() > prefetch.bottom())
            break;
        if (r.bottom() < prefetch.top())
            continue;
        const PdfTileKey preview{i, PdfRenderQueue::kPreviewWidth, -1, -1};
        if (r.intersects(vis)) {
            previews << preview;
            addTiles(i, vis, tiles);
        } else {
            later << preview;
        }
        addTiles(i, prefetch, later);
    }
    m_queue->setWanted(previews + tiles + later);
}

void PdfView::paintPage(QPainter& p, int page, const QRectF& clipDoc)
{
    const PageSlot& slot = m_pages[page];
    const Palette& pal = m_theme.palette();
    const QTransform toView = QTransform::fromTranslate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const QRectF pageView = toView.mapRect(slot.rect);

    // Ombre douce et feuille blanche
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, pal.dark ? 70 : 22));
    p.drawRoundedRect(pageView.adjusted(-1, 0, 1, 3), 3, 3);
    p.fillRect(pageView, Qt::white);

    p.save();
    p.setTransform(pageTransform(page) * toView);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF pageRectPts(QPointF(0, 0), slot.points);
    if (m_queue) {
        const QImage preview = m_queue->cached({page, PdfRenderQueue::kPreviewWidth, -1, -1});
        const qreal dpr = devicePixelRatioF();
        const int scaledWidth = qRound(slot.points.width() * scale() * dpr);
        const qreal f = scaledWidth / slot.points.width();
        // Les tuiles nettes recouvrent l'aperçu ; l'aperçu reste visible tant qu'elles ne sont pas prêtes.
        const QRectF pts = pageTransform(page).inverted().mapRect(clipDoc.intersected(slot.rect));
        const QRectF px(pts.topLeft() * f, pts.bottomRight() * f);
        const int t = PdfRenderQueue::kTileSize;
        bool allTiles = true;
        QList<QPair<QRectF, QImage>> tiles;
        for (int r = qMax(0, int(px.top() / t)); r <= int((px.bottom() - 1) / t); ++r)
            for (int c = qMax(0, int(px.left() / t)); c <= int((px.right() - 1) / t); ++c) {
                const PdfTileKey key{page, scaledWidth, c, r};
                const QImage img = m_queue->cached(key);
                if (img.isNull()) {
                    allTiles = false;
                    continue;
                }
                const QRectF tr = m_queue->tileRect(key);
                tiles.append({QRectF(tr.topLeft() / f, tr.size() / f), img});
            }
        if (!allTiles && !preview.isNull())
            p.drawImage(pageRectPts, preview);
        for (const auto& [rect, img] : tiles)
            p.drawImage(rect, img);
    }

    paintMarks(p, page);

    // Résultats de recherche
    if (m_search && !m_search->searchString().isEmpty()) {
        const QPdfLink current = m_currentResult >= 0 ? m_search->resultAtIndex(m_currentResult) : QPdfLink();
        for (const QPdfLink& link : m_search->resultsOnPage(page)) {
            const bool isCurrent = current.isValid() && current.page() == page
                                   && current.rectangles() == link.rectangles();
            QColor c = isCurrent ? QColor(0xF0, 0x9A, 0x3E) : QColor(0xF5, 0xD7, 0x6E);
            c.setAlpha(isCurrent ? 150 : 110);
            for (const QRectF& r : link.rectangles())
                p.fillRect(r.adjusted(-1, -1, 1, 1), c);
        }
    }
    const int nativeHits = m_search ? m_search->rowCount({}) : 0;
    for (int i = 0; i < m_ocrHits.size(); ++i) {
        if (m_ocrHits.at(i).page != page)
            continue;
        const bool isCurrent = m_currentResult == nativeHits + i;
        QColor c = isCurrent ? QColor(0xF0, 0x9A, 0x3E) : QColor(0xF5, 0xD7, 0x6E);
        c.setAlpha(isCurrent ? 150 : 110);
        for (const QRectF& r : m_ocrHits.at(i).rects)
            p.fillRect(r.adjusted(-1, -1, 1, 1), c);
    }

    // Sélection
    QColor selColor = pal.primary;
    selColor.setAlpha(90);
    for (const auto& s : m_selection) {
        if (s.page != page)
            continue;
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (const QRectF& r : s.rects)
            path.addRect(r);
        p.fillPath(path, selColor);
    }
    // Zone en cours de tracé
    if (m_drawingArea && m_areaPage == page) {
        const QRectF area = QRectF(m_areaStart, m_areaEnd).normalized();
        QColor fill = pal.primary;
        fill.setAlpha(40);
        p.fillRect(area, fill);
        QPen pen(pal.primary, 1.5 / scale(), Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(area);
    }
    p.restore();
    paintMarkDecorations(p, page, toView);
}

void PdfView::paintMarks(QPainter& p, int page)
{
    const auto it = m_marksByPage.constFind(page);
    if (it == m_marksByPage.constEnd())
        return;
    const QSizeF size = m_pages[page].points;
    const bool flashOn = m_flashId != 0 && m_flashStep % 2 == 0;
    for (int idx : *it) {
        const PdfAnnotationMark& m = m_marks[idx];
        const bool flashing = flashOn && m.id == m_flashId;
        for (const QRectF& n : m.rects) {
            const QRectF r(n.x() * size.width(), n.y() * size.height(), n.width() * size.width(),
                           n.height() * size.height());
            switch (m.type) {
            case AnnotationType::Highlight: {
                p.save();
                p.setCompositionMode(QPainter::CompositionMode_Multiply);
                p.fillRect(r.adjusted(-0.5, -0.5, 0.5, 0.5), flashing ? m.stroke : m.fill);
                p.restore();
                break;
            }
            case AnnotationType::Underline:
            case AnnotationType::Strikeout: {
                const qreal w = std::max(1.0, r.height() * 0.09);
                const qreal y = m.type == AnnotationType::Underline ? r.bottom() - w / 2 : r.center().y();
                if (flashing) {
                    QColor f = m.fill;
                    p.fillRect(r, f);
                }
                p.fillRect(QRectF(r.left(), y - w / 2, r.width(), w), m.stroke);
                break;
            }
            case AnnotationType::Marker: {
                QColor f = m.fill;
                f.setAlpha(flashing ? 140 : 70);
                p.fillRect(r, f);
                p.setPen(QPen(m.stroke, 1.6 / scale()));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, 2, 2);
                break;
            }
            default:
                break;
            }
        }
    }
}

void PdfView::paintMarkDecorations(QPainter& p, int page, const QTransform& toView)
{
    Q_UNUSED(toView)
    const auto it = m_marksByPage.constFind(page);
    if (it == m_marksByPage.constEnd())
        return;
    const bool flashOn = m_flashId != 0 && m_flashStep % 2 == 0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (int idx : *it) {
        const PdfAnnotationMark& m = m_marks[idx];
        const bool flashing = flashOn && m.id == m_flashId;
        if (m.type == AnnotationType::Note) {
            // Icône de note à taille constante, quel que soit le zoom.
            const QRectF r = noteIconRect(m);
            p.setPen(QPen(m.stroke, 1.5));
            p.setBrush(flashing ? m.stroke : m.fill);
            QPainterPath bubble;
            bubble.addRoundedRect(r.adjusted(0, 0, 0, -4), 4, 4);
            QPolygonF tail;
            tail << QPointF(r.left() + 5, r.bottom() - 4.5) << QPointF(r.left() + 5, r.bottom())
                 << QPointF(r.left() + 10, r.bottom() - 4.5);
            bubble.addPolygon(tail);
            p.drawPath(bubble.simplified());
            p.setPen(QPen(flashing ? m.fill : m.stroke, 1.4, Qt::SolidLine, Qt::RoundCap));
            const qreal x0 = r.left() + 5, x1 = r.right() - 5;
            p.drawLine(QPointF(x0, r.top() + 6), QPointF(x1, r.top() + 6));
            p.drawLine(QPointF(x0, r.top() + 10), QPointF(x1 - 3, r.top() + 10));
        } else if (m.type == AnnotationType::Bookmark) {
            const QRectF r = bookmarkRibbonRect(page);
            QPolygonF ribbon;
            ribbon << r.topLeft() << r.topRight() << r.bottomRight() << QPointF(r.center().x(), r.bottom() - 6)
                   << r.bottomLeft();
            p.setPen(Qt::NoPen);
            p.setBrush(flashing ? m.fill : m.stroke);
            p.drawPolygon(ribbon);
        }
    }
    p.restore();
}

QRectF PdfView::noteIconRect(const PdfAnnotationMark& m) const
{
    if (m.rects.isEmpty() || m.page < 0 || m.page >= m_pages.size())
        return {};
    const QSizeF size = m_pages[m.page].points;
    const QPointF anchor(m.rects.first().center().x() * size.width(), m.rects.first().center().y() * size.height());
    const QPointF doc = pageTransform(m.page).map(anchor);
    const QPointF v = doc - QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    return QRectF(v.x() - 11, v.y() - 11, 22, 22);
}

QRectF PdfView::bookmarkRibbonRect(int page) const
{
    const QRectF r = m_pages[page].rect.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    return QRectF(r.right() - 34, r.top() - 2, 14, 24);
}

void PdfView::paintEvent(QPaintEvent* event)
{
    QPainter p(viewport());
    const Palette& pal = m_theme.palette();
    p.fillRect(event->rect(), pal.dark ? pal.bg.darker(115) : pal.sunken);
    if (m_pages.isEmpty())
        return;
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF clipDoc = QRectF(event->rect()).translated(horizontalScrollBar()->value(), verticalScrollBar()->value());
    for (int i = 0; i < m_pages.size(); ++i) {
        const QRectF r = m_pages[i].rect;
        if (r.top() > clipDoc.bottom())
            break;
        if (r.bottom() + 4 < clipDoc.top() || !r.adjusted(-2, -2, 2, 4).intersects(clipDoc))
            continue;
        paintPage(p, i, clipDoc);
    }
}

// ───────────────────────── Annotations ─────────────────────────

void PdfView::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    m_tool = tool;
    m_drawingArea = false;
    viewport()->setCursor(tool == Tool::Marker || tool == Tool::Note ? Qt::CrossCursor : Qt::IBeamCursor);
    viewport()->update();
    emit toolChanged(tool);
}

void PdfView::setAnnotations(const QList<PdfAnnotationMark>& marks)
{
    m_marks = marks;
    m_marksByPage.clear();
    for (int i = 0; i < m_marks.size(); ++i)
        m_marksByPage[m_marks[i].page].append(i);
    viewport()->update();
}

void PdfView::flashAnnotation(qint64 id)
{
    m_flashId = id;
    m_flashStep = 0;
    m_flashTimer.start();
    viewport()->update();
}

QRectF PdfView::normalizedToDoc(int page, const QRectF& norm) const
{
    if (page < 0 || page >= m_pages.size())
        return {};
    const QSizeF size = m_pages[page].points;
    const QRectF pts(norm.x() * size.width(), norm.y() * size.height(), norm.width() * size.width(),
                     norm.height() * size.height());
    return pageTransform(page).mapRect(pts);
}

void PdfView::revealNormalizedRect(int page, const QRectF& rect, bool recordHistory)
{
    if (page < 0 || page >= m_pages.size())
        return;
    if (rect.isNull()) {
        goToPage(page, -1, recordHistory);
        return;
    }
    if (recordHistory)
        pushHistory();
    // Le passage est placé au tiers supérieur de la fenêtre, s'il n'est pas déjà visible.
    const QRectF doc = normalizedToDoc(page, rect);
    const QRectF vis = visibleDocRect();
    if (!vis.adjusted(0, 40, 0, -40).contains(doc)) {
        verticalScrollBar()->setValue(qRound(doc.top() - vis.height() / 3));
        if (doc.left() < vis.left() || doc.right() > vis.right())
            horizontalScrollBar()->setValue(qRound(doc.center().x() - vis.width() / 2));
    }
    updateCurrentPage();
}

qint64 PdfView::annotationAt(QPoint viewportPos) const
{
    const int page = pageAt(viewportPos);
    // Icônes (notes, signets) d'abord : elles sont dessinées au-dessus.
    for (int i = int(m_marks.size()) - 1; i >= 0; --i) {
        const PdfAnnotationMark& m = m_marks[i];
        if (m.type == AnnotationType::Note && noteIconRect(m).adjusted(-2, -2, 2, 2).contains(viewportPos))
            return m.id;
        if (m.type == AnnotationType::Bookmark && m.page >= 0 && m.page < m_pages.size()
            && bookmarkRibbonRect(m.page).contains(viewportPos))
            return m.id;
    }
    if (page < 0)
        return 0;
    const QPointF pt = mapToPage(page, viewportPos);
    const QSizeF size = m_pages[page].points;
    const QPointF n(pt.x() / size.width(), pt.y() / size.height());
    const qreal tol = 2.0 / size.height();
    const auto it = m_marksByPage.constFind(page);
    if (it == m_marksByPage.constEnd())
        return 0;
    for (auto idx = it->crbegin(); idx != it->crend(); ++idx) {
        const PdfAnnotationMark& m = m_marks[*idx];
        if (m.type == AnnotationType::Note || m.type == AnnotationType::Bookmark)
            continue;
        for (const QRectF& r : m.rects)
            if (r.adjusted(-tol, -tol, tol, tol).contains(n))
                return m.id;
    }
    return 0;
}

QString PdfView::pageText(int page) const
{
    auto it = m_pageText.find(page);
    if (it == m_pageText.end())
        it = m_pageText.insert(page, m_ocr.contains(page) ? m_ocr[page].text()
                                     : m_doc                ? m_doc->getAllText(page).text()
                                                            : QString());
    return *it;
}

QList<PdfSelectionAnchor> PdfView::selectionAnchors() const
{
    QList<PdfSelectionAnchor> out;
    for (const auto& s : m_selection) {
        const QSizeF size = m_pages[s.page].points;
        PdfSelectionAnchor a;
        a.page = s.page;
        a.text = s.text;
        // Qt PDF renvoie souvent un rectangle par caractère : on les fusionne en une zone par ligne,
        // pour un surlignage continu et une ancre compacte.
        QList<QRectF> lines;
        for (const QRectF& r : s.rects) {
            if (r.isEmpty())
                continue;
            bool merged = false;
            for (QRectF& l : lines) {
                const qreal overlap = std::min(l.bottom(), r.bottom()) - std::max(l.top(), r.top());
                if (overlap > 0.5 * std::min(l.height(), r.height())) {
                    l = l.united(r);
                    merged = true;
                    break;
                }
            }
            if (!merged)
                lines << r;
        }
        std::sort(lines.begin(), lines.end(), [](const QRectF& x, const QRectF& y) { return x.top() < y.top(); });
        for (const QRectF& r : lines)
            a.rects << QRectF(r.x() / size.width(), r.y() / size.height(), r.width() / size.width(),
                              r.height() / size.height());
        a.start = s.start;
        a.end = s.end;
        const QString text = pageText(s.page);
        if (a.start >= 0 && a.start <= text.size()) {
            const int from = std::max(0, a.start - 32);
            a.prefix = text.mid(from, a.start - from).simplified();
            const int end = std::max(a.start, a.end);
            a.suffix = text.mid(end, 32).simplified();
        }
        if (!a.rects.isEmpty())
            out << a;
    }
    return out;
}

QRect PdfView::selectionViewportRect() const
{
    QRectF box;
    for (const auto& s : m_selection) {
        QRectF bounds;
        for (const QRectF& r : s.rects)
            bounds = bounds.isNull() ? r : bounds.united(r);
        const QRectF doc = pageTransform(s.page).mapRect(bounds);
        box = box.isNull() ? doc : box.united(doc);
    }
    if (box.isNull())
        return {};
    return box.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value()).toAlignedRect();
}

QRect PdfView::annotationViewportRect(qint64 id) const
{
    for (const auto& m : m_marks) {
        if (m.id != id)
            continue;
        if (m.type == AnnotationType::Note)
            return noteIconRect(m).toAlignedRect();
        if (m.type == AnnotationType::Bookmark)
            return bookmarkRibbonRect(m.page).toAlignedRect();
        QRectF box;
        for (const QRectF& r : m.rects) {
            const QRectF d = normalizedToDoc(m.page, r);
            box = box.isNull() ? d : box.united(d);
        }
        return box.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value()).toAlignedRect();
    }
    return {};
}

// ───────────────────────── Événements ─────────────────────────

void PdfView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout(true);
}

void PdfView::scrollContentsBy(int, int)
{
    viewport()->update();
    updateCurrentPage();
    requestRender();
    if (!m_inRelayout)
        emit positionChanged();
}

void PdfView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal steps = event->angleDelta().y() / 120.0;
        if (!qFuzzyIsNull(steps))
            setZoomAround(m_zoom * std::pow(1.1, steps), event->position().toPoint());
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PdfView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    m_pressPos = event->position().toPoint();
    m_pressLink = linkAt(m_pressPos);
    const int page = pageAt(m_pressPos);
    if (m_tool == Tool::Marker || m_tool == Tool::Note) {
        m_selecting = false;
        m_drawingArea = m_tool == Tool::Marker && page >= 0;
        m_areaPage = page;
        if (page >= 0)
            m_areaStart = m_areaEnd = mapToPage(page, m_pressPos);
        return;
    }
    m_selecting = page >= 0;
    m_selStartPage = page;
    if (page >= 0)
        m_selStart = mapToPage(page, m_pressPos);
}

void PdfView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_drawingArea && (event->buttons() & Qt::LeftButton)) {
        QPointF pt = mapToPage(m_areaPage, pos);
        pt.setX(std::clamp(pt.x(), 0.0, m_pages[m_areaPage].points.width()));
        pt.setY(std::clamp(pt.y(), 0.0, m_pages[m_areaPage].points.height()));
        m_areaEnd = pt;
        viewport()->update();
        return;
    }
    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        if ((pos - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        int page = pageAt(pos);
        if (page < 0) {
            // Entre deux pages : on rattache le point à la page la plus proche verticalement.
            const qreal y = pos.y() + verticalScrollBar()->value();
            page = m_selStartPage;
            for (int i = 0; i < m_pages.size(); ++i)
                if (m_pages[i].rect.top() <= y)
                    page = i;
        }
        QPointF pt = mapToPage(page, pos);
        pt.setX(std::clamp(pt.x(), 0.0, m_pages[page].points.width()));
        pt.setY(std::clamp(pt.y(), 0.0, m_pages[page].points.height()));
        selectText(m_selStartPage, m_selStart, page, pt);
        // Défilement automatique près des bords
        if (pos.y() < 20)
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 20);
        else if (pos.y() > viewport()->height() - 20)
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 20);
        return;
    }
    if (m_tool == Tool::Marker || m_tool == Tool::Note) {
        viewport()->setCursor(pageAt(pos) >= 0 ? Qt::CrossCursor : Qt::ArrowCursor);
        return;
    }
    if (annotationAt(pos) != 0) {
        viewport()->setCursor(Qt::PointingHandCursor);
        viewport()->setToolTip(QString());
        return;
    }
    const LinkHit hit = linkAt(pos);
    if (hit.valid) {
        viewport()->setCursor(Qt::PointingHandCursor);
        viewport()->setToolTip(hit.url.isValid() ? hit.url.toString() : tr("Aller à la page %1").arg(hit.page + 1));
    } else {
        viewport()->setCursor(pageAt(pos) >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
        viewport()->setToolTip(QString());
    }
}

void PdfView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const bool dragged = (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance();
    if (m_tool == Tool::Marker) {
        const bool wasDrawing = m_drawingArea;
        m_drawingArea = false;
        viewport()->update();
        const QRectF area = QRectF(m_areaStart, m_areaEnd).normalized();
        if (wasDrawing && m_areaPage >= 0 && area.width() * scale() >= 6 && area.height() * scale() >= 6) {
            const QSizeF size = m_pages[m_areaPage].points;
            emit areaDrawn(m_areaPage, QRectF(area.x() / size.width(), area.y() / size.height(),
                                              area.width() / size.width(), area.height() / size.height()));
        }
        return;
    }
    if (m_tool == Tool::Note) {
        const int page = pageAt(pos);
        if (!dragged && page >= 0) {
            const QPointF pt = mapToPage(page, pos);
            const QSizeF size = m_pages[page].points;
            emit pointClicked(page, QPointF(pt.x() / size.width(), pt.y() / size.height()));
        }
        return;
    }
    m_selecting = false;
    if (dragged && hasSelection()) {
        emit selectionFinished();
        m_pressLink = {};
        return;
    }
    if (!dragged) {
        clearSelection();
        if (const qint64 id = annotationAt(pos)) {
            m_pressLink = {};
            emit annotationClicked(id);
            return;
        }
        if (m_pressLink.valid) {
            if (m_pressLink.url.isValid())
                QDesktopServices::openUrl(m_pressLink.url);
            else
                goToPage(m_pressLink.page, m_pressLink.y, true);
        }
    }
    m_pressLink = {};
}

void PdfView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection();
        return;
    }
    if (event->matches(QKeySequence::SelectAll) && m_doc) {
        selectText(m_currentPage, QPointF(0, 0), m_currentPage, QPointF(m_pages[m_currentPage].points.width(),
                                                                           m_pages[m_currentPage].points.height()));
        return;
    }
    if (event->matches(QKeySequence::ZoomIn)
        || ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Equal)) {
        zoomIn();
        return;
    }
    if (event->matches(QKeySequence::ZoomOut)) {
        zoomOut();
        return;
    }
    switch (event->key()) {
    case Qt::Key_0:
        if (event->modifiers() & Qt::ControlModifier) {
            setZoomMode(ZoomMode::FitWidth);
            return;
        }
        break;
    case Qt::Key_Home:
        goToPage(0, -1, true);
        return;
    case Qt::Key_End:
        goToPage(pageCount() - 1, -1, true);
        return;
    case Qt::Key_Space:
        verticalScrollBar()->triggerAction(event->modifiers() & Qt::ShiftModifier ? QAbstractSlider::SliderPageStepSub
                                                                                   : QAbstractSlider::SliderPageStepAdd);
        return;
    case Qt::Key_Left:
        if (horizontalScrollBar()->maximum() == 0) {
            previousPage();
            return;
        }
        break;
    case Qt::Key_Right:
        if (horizontalScrollBar()->maximum() == 0) {
            nextPage();
            return;
        }
        break;
    case Qt::Key_Escape:
        clearSelection();
        if (m_tool != Tool::Select)
            setTool(Tool::Select);
        break;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PdfView::contextMenuEvent(QContextMenuEvent* event)
{
    if (const qint64 id = annotationAt(event->pos())) {
        emit annotationContextMenuRequested(id, event->globalPos());
        return;
    }
    QMenu menu(this);
    QAction* copy = menu.addAction(tr("Copier"), this, &PdfView::copySelection);
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection());
    menu.addAction(tr("Tout sélectionner sur la page"), this, [this] {
        selectText(m_currentPage, QPointF(0, 0), m_currentPage,
                   QPointF(m_pages[m_currentPage].points.width(), m_pages[m_currentPage].points.height()));
    });
    menu.exec(event->globalPos());
}

bool PdfView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Leave)
        viewport()->setToolTip(QString());
    return QAbstractScrollArea::viewportEvent(event);
}

}  // namespace studylab::ui
