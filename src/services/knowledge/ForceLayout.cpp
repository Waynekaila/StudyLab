#include "services/knowledge/ForceLayout.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace studylab {

QList<QPointF> forceLayout(const KnowledgeGraph& graph, const LayoutOptions& o, const std::atomic_bool* cancel)
{
    const int n = int(graph.nodes.size());
    QList<QPointF> pos(n);
    if (n == 0)
        return pos;
    const double k = std::max(10.0, o.edgeLength);
    const double radius = k * std::sqrt(double(n)) * 0.6;
    QRandomGenerator rng(o.seed);
    const auto jitter = [&rng](double r) {
        const double a = rng.generateDouble() * 2 * std::numbers::pi;
        const double d = std::sqrt(rng.generateDouble()) * r;
        return QPointF(std::cos(a) * d, std::sin(a) * d);
    };
    const QList<QList<int>> adj = graph.adjacency();

    // Positions de départ : connues, sinon près d'un voisin connu, sinon au hasard dans un disque.
    QList<bool> placed(n, false);
    for (int i = 0; i < n; ++i) {
        const auto it = o.initial.constFind(graph.nodes.at(i).key);
        if (it != o.initial.constEnd() && std::isfinite(it->x()) && std::isfinite(it->y())) {
            pos[i] = *it;
            placed[i] = true;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (placed.at(i))
            continue;
        const auto near = std::find_if(adj.at(i).begin(), adj.at(i).end(), [&placed](int j) { return placed.at(j); });
        pos[i] = near != adj.at(i).end() ? pos.at(*near) + jitter(k * 0.5) : jitter(radius);
    }

    // Pôles des matières (regroupement) : sur un cercle, dans l'ordre des identifiants.
    QList<QPointF> anchor(n, QPointF(0, 0));
    if (o.groupBySubject) {
        QList<qint64> groups;
        for (const GraphNode& node : graph.nodes) {
            const qint64 g = node.subjects.isEmpty() ? 0 : node.subjects.first();
            if (!groups.contains(g))
                groups << g;
        }
        std::sort(groups.begin(), groups.end());
        if (groups.size() > 1) {
            const double ring = radius * 0.9;
            for (int i = 0; i < n; ++i) {
                const GraphNode& node = graph.nodes.at(i);
                const qint64 g = node.subjects.isEmpty() ? 0 : node.subjects.first();
                const double a = 2 * std::numbers::pi * double(groups.indexOf(g)) / double(groups.size());
                anchor[i] = QPointF(std::cos(a) * ring, std::sin(a) * ring);
            }
        }
    }
    const double gravity = o.groupBySubject ? 0.12 : 0.04;

    const int iterations = o.iterations > 0 ? o.iterations : std::clamp(60000 / n, 80, 400);
    const double t0 = k * 1.5;
    QList<QPointF> disp(n);
    for (int it = 0; it < iterations; ++it) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            break;
        std::fill(disp.begin(), disp.end(), QPointF(0, 0));
        // Répulsion entre toutes les paires
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                QPointF d = pos.at(i) - pos.at(j);
                double dist2 = d.x() * d.x() + d.y() * d.y();
                if (dist2 < 0.01) {
                    d = QPointF(0.1 * (1 + (i % 7)), 0.1 * (1 + (j % 5)));
                    dist2 = d.x() * d.x() + d.y() * d.y();
                }
                const QPointF f = d * (k * k / dist2);  // (d/|d|) · k²/|d|
                disp[i] += f;
                disp[j] -= f;
            }
        }
        // Attraction le long des arêtes
        for (const GraphEdge& e : graph.edges) {
            if (e.from == e.to)
                continue;
            const QPointF d = pos.at(e.from) - pos.at(e.to);
            const double dist = std::max(0.1, std::hypot(d.x(), d.y()));
            const QPointF f = d * (dist / k);  // (d/|d|) · |d|²/k
            disp[e.from] -= f;
            disp[e.to] += f;
        }
        // Gravité vers le centre, ou vers le pôle de la matière
        const double t = t0 * (1.0 - double(it) / double(iterations)) + 0.5;
        for (int i = 0; i < n; ++i) {
            disp[i] += (anchor.at(i) - pos.at(i)) * gravity * k / 10.0;
            const double len = std::hypot(disp.at(i).x(), disp.at(i).y());
            if (len > 0 && std::isfinite(len))
                pos[i] += disp.at(i) * (std::min(len, t) / len);
        }
    }

    // Centrage
    QPointF c(0, 0);
    for (const QPointF& p : pos)
        c += p;
    c /= n;
    for (QPointF& p : pos)
        p -= c;
    return pos;
}

}  // namespace studylab
