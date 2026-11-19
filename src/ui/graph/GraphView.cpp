#include "ui/graph/GraphView.h"

#include "ui/organize/OrganizeWidgets.h"
#include "ui/theme/ThemeManager.h"

#include <QEasingCurve>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace studylab::ui {

namespace {

constexpr double kNodeRadius = 7.0;
constexpr double kSelectedScale = 1.5;
constexpr double kLabelWidth = 190.0;
constexpr double kMinLabelScale = 0.55;  ///< en dessous de ce zoom, les étiquettes ne rapetissent plus
constexpr double kFaded = 0.32;
constexpr double kMinZoom = 0.08;
constexpr double kMaxZoom = 4.0;

}  // namespace

// ───────────────────────── Nœud ─────────────────────────

class GraphNodeItem : public QGraphicsItem {
public:
    GraphNodeItem(GraphView* view, int index) : m_view(view), m_index(index)
    {
        setFlags(ItemIsMovable | ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
        setZValue(1);
    }

    int index() const { return m_index; }

    QRectF boundingRect() const override
    {
        const double w = kLabelWidth / kMinLabelScale;
        return QRectF(-w / 2, -kNodeRadius * kSelectedScale - 8, w, kNodeRadius * 5 + 34 / kMinLabelScale);
    }

    QPainterPath shape() const override
    {
        QPainterPath p;
        const double r = kNodeRadius * (selected() ? kSelectedScale : 1.0) + 4;
        p.addEllipse(QPointF(0, 0), r, r);
        return p;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const Palette& pal = m_view->theme().palette();
        const GraphNode& node = m_view->graph().nodes.at(m_index);
        const bool sel = selected();
        const QColor color = m_view->nodeColor(m_index);
        const double r = kNodeRadius * (sel ? kSelectedScale : 1.0);
        painter->setRenderHint(QPainter::Antialiasing);

        if (sel) {
            QColor halo = color;
            halo.setAlphaF(0.28);
            painter->setPen(QPen(halo, 5));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(0, 0), r + 6, r + 6);
        }
        switch (node.kind) {
        case GraphNodeKind::Document: {
            painter->setPen(QPen(color, 2.0));
            painter->setBrush(pal.surface);
            const QRectF box(-r * 1.25, -r, r * 2.5, r * 2);
            painter->drawRoundedRect(box, 3, 3);
            break;
        }
        case GraphNodeKind::Note:
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawEllipse(QPointF(0, 0), r, r);
            break;
        case GraphNodeKind::Concept: {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            const double d = r * 1.3;
            painter->drawPolygon(QPolygonF({QPointF(0, -d), QPointF(d, 0), QPointF(0, d), QPointF(-d, 0)}));
            break;
        }
        case GraphNodeKind::Unresolved: {
            QPen pen(pal.text3, 1.5, Qt::DashLine);
            painter->setPen(pen);
            painter->setBrush(pal.bg);
            painter->drawEllipse(QPointF(0, 0), r, r);
            break;
        }
        }

        const bool hover = m_view->hovered() == m_index;
        if (!(m_view->labelsVisible() || sel || hover))
            return;
        // Étiquettes lisibles quand on dézoome : taille compensée jusqu'à un certain point.
        const double z = std::clamp(m_view->zoom(), kMinLabelScale, 1.0);
        QFont f = painter->font();
        f.setPixelSize(std::max(1, int(std::lround(m_view->theme().px(12) / z))));
        const bool strong = sel || hover;
        f.setWeight(strong ? QFont::DemiBold : QFont::Normal);
        painter->setFont(f);
        const QFontMetricsF fm(f);
        const double width = kLabelWidth / z;
        const QString text = fm.elidedText(node.title, Qt::ElideRight, width - 6);
        painter->setPen(strong ? pal.text : (node.kind == GraphNodeKind::Unresolved ? pal.text3 : pal.text2));
        const QRectF label(-width / 2, r + 5 / z, width, fm.height() + 2);
        painter->drawText(label, Qt::AlignHCenter | Qt::AlignTop, text);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == ItemPositionHasChanged)
            m_view->nodeMoved(this);
        return QGraphicsItem::itemChange(change, value);
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent*) override { m_view->setHovered(m_index); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override
    {
        if (m_view->hovered() == m_index)
            m_view->setHovered(-1);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        m_press = event->screenPos();
        QGraphicsItem::mousePressEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        QGraphicsItem::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && (event->screenPos() - m_press).manhattanLength() < 4)
            m_view->emitClicked(m_index);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            m_view->emitActivated(m_index);
    }

private:
    bool selected() const { return m_view->selected() == m_index; }

    GraphView* m_view;
    int m_index;
    QPoint m_press;
};

// ───────────────────────── Arête ─────────────────────────

class GraphEdgeItem : public QGraphicsItem {
public:
    GraphEdgeItem(GraphView* view, int index, GraphNodeItem* from, GraphNodeItem* to)
        : m_view(view), m_index(index), m_from(from), m_to(to)
    {
        setZValue(0);
        setAcceptedMouseButtons(Qt::NoButton);
        adjust();
    }

    int index() const { return m_index; }

    void adjust()
    {
        prepareGeometryChange();
        m_line = QLineF(m_from->pos(), m_to->pos());
    }

    QRectF boundingRect() const override
    {
        return QRectF(m_line.p1(), m_line.p2()).normalized().adjusted(-2, -2, 2, 2);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const Palette& pal = m_view->theme().palette();
        const GraphEdge& e = m_view->graph().edges.at(m_index);
        const int sel = m_view->selected();
        const bool touching = sel >= 0 && (e.from == sel || e.to == sel);
        QColor color = touching ? m_view->nodeColor(sel) : (pal.dark ? pal.text3 : pal.border);
        if (!touching && !pal.dark)
            color = color.darker(108);
        QPen pen(color, touching ? 1.8 : 1.1);
        if (e.derived)
            pen.setStyle(Qt::DashLine);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(pen);
        painter->drawLine(m_line);
    }

private:
    GraphView* m_view;
    int m_index;
    GraphNodeItem* m_from;
    GraphNodeItem* m_to;
    QLineF m_line;
};

// ───────────────────────── Vue ─────────────────────────

GraphView::GraphView(ThemeManager& theme, QWidget* parent) : QGraphicsView(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("GraphView"));
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Graphe de connaissances"));
    viewport()->setCursor(Qt::ArrowCursor);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(450);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const double t = v.toDouble();
        if (m_animFrom.size() != m_nodes.size() || m_animTo.size() != m_nodes.size())
            return;
        QList<QPointF> now(m_nodes.size());
        for (int i = 0; i < m_nodes.size(); ++i)
            now[i] = m_animFrom.at(i) + (m_animTo.at(i) - m_animFrom.at(i)) * t;
        applyPositions(now);
    });
    connect(m_anim, &QVariantAnimation::finished, this, &GraphView::updateSceneRect);
    refreshStyle();
}

GraphView::~GraphView()
{
    m_anim->stop();
    // Les arêtes lisent le graphe en se détruisant : on vide la scène avant le graphe.
    scene()->clear();
}

void GraphView::setGraph(const KnowledgeGraph& graph, const QList<QPointF>& positions)
{
    m_anim->stop();
    scene()->clear();
    m_nodes.clear();
    m_edges.clear();
    m_edgesOf.clear();
    m_graph = graph;
    m_selected = -1;
    m_hovered = -1;
    m_highlight.clear();

    for (int i = 0; i < m_graph.nodes.size(); ++i) {
        auto* item = new GraphNodeItem(this, i);
        item->setPos(i < positions.size() ? positions.at(i) : QPointF());
        item->setToolTip(m_graph.nodes.at(i).title);
        scene()->addItem(item);
        m_nodes << item;
    }
    m_edgesOf.resize(m_nodes.size());
    for (int i = 0; i < m_graph.edges.size(); ++i) {
        const GraphEdge& e = m_graph.edges.at(i);
        if (e.from < 0 || e.to < 0 || e.from >= m_nodes.size() || e.to >= m_nodes.size())
            continue;
        auto* item = new GraphEdgeItem(this, i, m_nodes.at(e.from), m_nodes.at(e.to));
        scene()->addItem(item);
        m_edges << item;
        m_edgesOf[e.from] << item;
        m_edgesOf[e.to] << item;
    }
    updateSceneRect();
    refreshStyle();
}

void GraphView::applyPositions(const QList<QPointF>& positions)
{
    for (int i = 0; i < m_nodes.size() && i < positions.size(); ++i)
        m_nodes.at(i)->setPos(positions.at(i));
}

void GraphView::setPositions(const QList<QPointF>& positions, bool animate)
{
    m_anim->stop();
    if (!animate || positions.size() != m_nodes.size()) {
        applyPositions(positions);
        updateSceneRect();
        return;
    }
    m_animFrom.clear();
    for (GraphNodeItem* item : std::as_const(m_nodes))
        m_animFrom << item->pos();
    m_animTo = positions;
    m_anim->start();
}

QHash<QString, QPointF> GraphView::positionsByKey() const
{
    QHash<QString, QPointF> out;
    for (int i = 0; i < m_nodes.size(); ++i)
        out.insert(m_graph.nodes.at(i).key, m_anim->state() == QAbstractAnimation::Running ? m_animTo.value(i)
                                                                                           : m_nodes.at(i)->pos());
    return out;
}

QPointF GraphView::nodePosition(int index) const
{
    return index >= 0 && index < m_nodes.size() ? m_nodes.at(index)->pos() : QPointF();
}

void GraphView::setSelected(int index)
{
    if (index >= m_nodes.size())
        index = -1;
    m_selected = index;
    refreshStyle();
}

void GraphView::setHighlight(const QSet<int>& nodes)
{
    m_highlight = nodes;
    refreshStyle();
}

bool GraphView::isEdgeHighlighted(const GraphEdge& edge) const
{
    return m_highlight.isEmpty() || (m_highlight.contains(edge.from) && m_highlight.contains(edge.to));
}

void GraphView::setLabelsVisible(bool visible)
{
    m_labels = visible;
    refreshStyle();
}

void GraphView::setHovered(int index)
{
    const int old = m_hovered;
    m_hovered = index;
    for (int i : {old, index}) {
        if (i >= 0 && i < m_nodes.size()) {
            m_nodes.at(i)->setZValue(i == index ? 4 : (i == m_selected ? 3 : (isHighlighted(i) ? 2 : 1)));
            m_nodes.at(i)->update();
        }
    }
}

QColor GraphView::nodeColor(int index) const
{
    const Palette& p = m_theme.palette();
    if (index < 0 || index >= m_graph.nodes.size())
        return p.text3;
    const GraphNode& n = m_graph.nodes.at(index);
    if (n.kind == GraphNodeKind::Unresolved || n.color.isEmpty())
        return p.text3;
    return organizeColor(p, n.color);
}

void GraphView::refreshStyle()
{
    setBackgroundBrush(m_theme.palette().bg);
    for (int i = 0; i < m_nodes.size(); ++i) {
        GraphNodeItem* item = m_nodes.at(i);
        const bool hi = isHighlighted(i);
        item->setOpacity(hi ? 1.0 : kFaded);
        item->setZValue(i == m_selected ? 3 : (hi ? 2 : 1));
        item->update();
    }
    for (GraphEdgeItem* edge : std::as_const(m_edges)) {
        edge->setOpacity(isEdgeHighlighted(m_graph.edges.at(edge->index())) ? 1.0 : kFaded);
        edge->update();
    }
    viewport()->update();
}

void GraphView::nodeMoved(GraphNodeItem* item)
{
    const int i = item->index();
    if (i < 0 || i >= m_edgesOf.size())
        return;
    for (GraphEdgeItem* edge : std::as_const(m_edgesOf.at(i)))
        edge->adjust();
}

void GraphView::updateSceneRect()
{
    QRectF r;
    for (GraphNodeItem* item : std::as_const(m_nodes))
        r = r.united(item->sceneBoundingRect());
    if (r.isNull())
        r = QRectF(-200, -200, 400, 400);
    // Large marge : on peut déplacer la vue au-delà des nœuds.
    const double m = std::max({600.0, r.width(), r.height()});
    setSceneRect(r.adjusted(-m, -m, m, m));
}

void GraphView::centerOnNode(int index)
{
    if (index >= 0 && index < m_nodes.size())
        centerOn(m_nodes.at(index));
}

double GraphView::zoom() const { return transform().m11(); }

void GraphView::zoomBy(double factor)
{
    const double current = zoom();
    const double target = std::clamp(current * factor, kMinZoom, kMaxZoom);
    if (current > 0 && std::abs(target - current) > 1e-6)
        scale(target / current, target / current);
}

void GraphView::fitAll()
{
    QRectF r;
    for (GraphNodeItem* item : std::as_const(m_nodes))
        r = r.united(item->sceneBoundingRect());
    if (r.isNull())
        return;
    r.adjust(-30, -30, 30, 30);
    const ViewportAnchor anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    fitInView(r, Qt::KeepAspectRatio);
    if (zoom() > 1.3) {
        resetTransform();
        scale(1.3, 1.3);
    }
    centerOn(r.center());
    setTransformationAnchor(anchor);
}

void GraphView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0)
        zoomBy(std::pow(1.0015, delta));
    event->accept();
}

void GraphView::mousePressEvent(QMouseEvent* event)
{
    m_pressPos = event->position().toPoint();
    m_pressOnBackground = itemAt(m_pressPos) == nullptr;
    QGraphicsView::mousePressEvent(event);
}

void GraphView::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
    if (m_pressOnBackground && event->button() == Qt::LeftButton
        && (event->position().toPoint() - m_pressPos).manhattanLength() < 4)
        emit nodeClicked(-1);
    m_pressOnBackground = false;
}

void GraphView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1.25);
        return;
    case Qt::Key_Minus:
        zoomBy(0.8);
        return;
    case Qt::Key_Escape:
        emit nodeClicked(-1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_selected >= 0) {
            emit nodeActivated(m_selected);
            return;
        }
        break;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

}  // namespace studylab::ui
