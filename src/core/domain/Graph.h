#pragma once

#include "core/domain/Note.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace studylab {

/// Graphe de connaissances (05-maquettes §7) : documents, notes, concepts (notes de type « Concept ») et, sur
/// demande, les cibles de liens [[…]] non résolus. Les arêtes viennent de la table links : liens [[…]] des notes
/// et provenance des notes tirées de la lecture (annotation, commentaire ou document → document).
enum class GraphNodeKind { Document, Note, Concept, Unresolved };

QString graphNodeKindLabel(GraphNodeKind kind);  ///< « Document », « Note », « Concept », « Lien non résolu »

struct GraphNode {
    GraphNodeKind kind = GraphNodeKind::Note;
    qint64 id = 0;                 ///< document ou note ; 0 pour un lien non résolu
    QString key;                   ///< identifiant stable : « d:12 », « n:5 », « u:second principe »
    QString title;
    QString detail;                ///< type de note, ou auteurs du document
    QString excerpt;               ///< début du texte (note) ou description (document)
    NoteType noteType = NoteType::Personal;
    QList<qint64> subjects;        ///< matières non archivées, dans l'ordre de la liste des matières
    QString color;                 ///< jeton de couleur de la première matière ; vide = sans matière

    // Chiffres de la fiche du nœud
    int outLinks = 0;              ///< liens [[…]] sortants (note)
    int backlinks = 0;             ///< liens [[…]] entrants, depuis des notes
    int sources = 0;               ///< note : sources de lecture ; document : notes qui en sont tirées
    int annotations = 0;           ///< document : annotations (hors signets)
};

struct GraphEdge {
    int from = 0;  ///< indice du nœud source
    int to = 0;    ///< indice du nœud cible
    bool derived = false;  ///< provenance (note tirée d'un document) plutôt que lien [[…]]
};

struct KnowledgeGraph {
    QList<GraphNode> nodes;
    QList<GraphEdge> edges;

    int indexOf(const QString& key) const;
    /// Voisins de chaque nœud (arêtes sans orientation, sans doublon).
    QList<QList<int>> adjacency() const;
};

/// Ce que l'utilisateur choisit d'afficher.
struct GraphFilter {
    bool documents = true;
    bool notes = true;
    bool concepts = true;
    bool unresolved = false;
    /// Matières retenues ; vide = toutes. Un nœud passe s'il appartient à l'une d'elles.
    QSet<qint64> subjects;
    bool filterSubjects = false;  ///< false : pas de filtre par matière
    bool unfiled = true;          ///< avec filtre par matière : garder les nœuds sans matière
};

/// Sous-graphe filtré. Un lien non résolu n'est gardé que s'il reste relié à un nœud affiché.
KnowledgeGraph filterGraph(const KnowledgeGraph& graph, const GraphFilter& filter);

/// Nœuds à au plus `depth` arêtes de `start` (start compris). depth < 0 : toute la composante.
QSet<int> graphNeighborhood(const KnowledgeGraph& graph, int start, int depth);

/// Sous-graphe réduit aux nœuds donnés (ordre conservé).
KnowledgeGraph subgraph(const KnowledgeGraph& graph, const QSet<int>& keep);

/// Extrait lisible d'une note : sans front matter, titres, balises Markdown ni liens, coupé à `length` caractères.
QString graphExcerpt(const QString& markdown, int length = 180);

}  // namespace studylab
