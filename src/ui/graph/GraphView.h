#pragma once

#include "core/domain/Graph.h"

#include <QGraphicsView>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>

class QVariantAnimation;

namespace studylab::ui {

class ThemeManager;
class GraphNodeItem;
class GraphEdgeItem;

/// Vue du graphe de connaissances (QGraphicsView) : formes par type (rectangle = document, rond = note, losange =
/// concept, cercle pointillé = lien non résolu), couleur de la matière, étiquettes. La sélection agrandit le nœud,
/// met son voisinage en valeur et atténue le reste. Molette = zoom, glisser le fond = déplacer la vue, glisser un
/// nœud = le déplacer.
class GraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(ThemeManager& theme, QWidget* parent = nullptr);
    ~GraphView() override;

    /// Remplace le graphe affiché ; une position par nœud.
    void setGraph(const KnowledgeGraph& graph, const QList<QPointF>& positions);
    /// Nouvelles positions (disposition recalculée), avec une transition douce si `animate`.
    void setPositions(const QList<QPointF>& positions, bool animate);
    /// Positions actuelles, par clé de nœud.
    QHash<QString, QPointF> positionsByKey() const;
    QPointF nodePosition(int index) const;

    const KnowledgeGraph& graph() const { return m_graph; }
    int selected() const { return m_selected; }
    void setSelected(int index);
    /// Nœuds mis en valeur (voisinage de la sélection) ; vide : aucun nœud atténué.
    void setHighlight(const QSet<int>& nodes);
    bool isHighlighted(int index) const { return m_highlight.isEmpty() || m_highlight.contains(index); }
    bool isEdgeHighlighted(const GraphEdge& edge) const;
    void setLabelsVisible(bool visible);
    bool labelsVisible() const { return m_labels; }

    void centerOnNode(int index);
    void zoomBy(double factor);
    void fitAll();
    double zoom() const;
    /// Couleurs du thème et états (sélection, voisinage) : à rappeler après un changement de thème.
    void refreshStyle();

    // Rendu (utilisé par les éléments de la scène)
    QColor nodeColor(int index) const;
    ThemeManager& theme() const { return m_theme; }
    int hovered() const { return m_hovered; }
    void setHovered(int index);
    void emitClicked(int index) { emit nodeClicked(index); }
    void emitActivated(int index) { emit nodeActivated(index); }
    void nodeMoved(GraphNodeItem* item);

signals:
    void nodeClicked(int index);     ///< -1 : clic sur le fond
    void nodeActivated(int index);   ///< double-clic

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyPositions(const QList<QPointF>& positions);
    void updateSceneRect();

    ThemeManager& m_theme;
    KnowledgeGraph m_graph;
    QList<GraphNodeItem*> m_nodes;
    QList<GraphEdgeItem*> m_edges;
    QList<QList<GraphEdgeItem*>> m_edgesOf;
    QSet<int> m_highlight;
    int m_selected = -1;
    int m_hovered = -1;
    bool m_labels = true;
    QPoint m_pressPos;
    bool m_pressOnBackground = false;
    QVariantAnimation* m_anim = nullptr;
    QList<QPointF> m_animFrom;
    QList<QPointF> m_animTo;
};

}  // namespace studylab::ui
