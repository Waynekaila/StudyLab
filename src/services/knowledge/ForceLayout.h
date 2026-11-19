#pragma once

#include "core/domain/Graph.h"

#include <QHash>
#include <QList>
#include <QPointF>

#include <atomic>

namespace studylab {

/// Réglages de la disposition force-dirigée (Fruchterman-Reingold, refroidissement linéaire).
struct LayoutOptions {
    double edgeLength = 90.0;      ///< longueur d'arête visée (px de scène)
    int iterations = 0;            ///< 0 : selon la taille du graphe
    bool groupBySubject = false;   ///< attire chaque nœud vers le pôle de sa première matière
    quint32 seed = 1;              ///< positions de départ reproductibles
    /// Positions connues (par clé de nœud) : point de départ, pour que la vue ne saute pas d'un calcul à l'autre.
    QHash<QString, QPointF> initial;
};

/// Calcule une position par nœud, centrée sur l'origine. Fonction pure : appelée hors du thread de l'interface.
/// `cancel` (facultatif) interrompt le calcul ; le résultat est alors celui de la dernière itération faite.
QList<QPointF> forceLayout(const KnowledgeGraph& graph, const LayoutOptions& options,
                           const std::atomic_bool* cancel = nullptr);

}  // namespace studylab
