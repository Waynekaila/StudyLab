#include "core/domain/Graph.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace studylab {

QString graphNodeKindLabel(GraphNodeKind kind)
{
    switch (kind) {
    case GraphNodeKind::Document:
        return QCoreApplication::translate("Graph", "Document");
    case GraphNodeKind::Note:
        return QCoreApplication::translate("Graph", "Note");
    case GraphNodeKind::Concept:
        return QCoreApplication::translate("Graph", "Concept");
    case GraphNodeKind::Unresolved:
        return QCoreApplication::translate("Graph", "Lien non résolu");
    }
    return {};
}

int KnowledgeGraph::indexOf(const QString& key) const
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).key == key)
            return i;
    }
    return -1;
}

QList<QList<int>> KnowledgeGraph::adjacency() const
{
    QList<QSet<int>> sets(nodes.size());
    for (const GraphEdge& e : edges) {
        if (e.from == e.to || e.from < 0 || e.to < 0 || e.from >= nodes.size() || e.to >= nodes.size())
            continue;
        sets[e.from].insert(e.to);
        sets[e.to].insert(e.from);
    }
    QList<QList<int>> out;
    out.reserve(sets.size());
    for (const QSet<int>& s : sets) {
        QList<int> l(s.begin(), s.end());
        std::sort(l.begin(), l.end());
        out << l;
    }
    return out;
}

namespace {

bool kindShown(const GraphFilter& f, GraphNodeKind kind)
{
    switch (kind) {
    case GraphNodeKind::Document:
        return f.documents;
    case GraphNodeKind::Note:
        return f.notes;
    case GraphNodeKind::Concept:
        return f.concepts;
    case GraphNodeKind::Unresolved:
        return f.unresolved;
    }
    return false;
}

bool subjectShown(const GraphFilter& f, const GraphNode& n)
{
    if (!f.filterSubjects || n.kind == GraphNodeKind::Unresolved)
        return true;
    if (n.subjects.isEmpty())
        return f.unfiled;
    return std::any_of(n.subjects.begin(), n.subjects.end(), [&f](qint64 id) { return f.subjects.contains(id); });
}

}  // namespace

KnowledgeGraph subgraph(const KnowledgeGraph& graph, const QSet<int>& keep)
{
    KnowledgeGraph out;
    QList<int> map(graph.nodes.size(), -1);
    for (int i = 0; i < graph.nodes.size(); ++i) {
        if (keep.contains(i)) {
            map[i] = int(out.nodes.size());
            out.nodes << graph.nodes.at(i);
        }
    }
    for (const GraphEdge& e : graph.edges) {
        if (e.from < 0 || e.to < 0 || e.from >= map.size() || e.to >= map.size())
            continue;
        if (map.at(e.from) >= 0 && map.at(e.to) >= 0)
            out.edges << GraphEdge{map.at(e.from), map.at(e.to), e.derived};
    }
    return out;
}

KnowledgeGraph filterGraph(const KnowledgeGraph& graph, const GraphFilter& filter)
{
    QSet<int> keep;
    for (int i = 0; i < graph.nodes.size(); ++i) {
        const GraphNode& n = graph.nodes.at(i);
        if (n.kind != GraphNodeKind::Unresolved && kindShown(filter, n.kind) && subjectShown(filter, n))
            keep.insert(i);
    }
    if (filter.unresolved) {
        // Un lien non résolu n'a de sens qu'à côté d'une note affichée qui le cite.
        for (const GraphEdge& e : graph.edges) {
            const bool fromShown = keep.contains(e.from);
            if (fromShown && e.to >= 0 && e.to < graph.nodes.size()
                && graph.nodes.at(e.to).kind == GraphNodeKind::Unresolved)
                keep.insert(e.to);
        }
    }
    return subgraph(graph, keep);
}

QSet<int> graphNeighborhood(const KnowledgeGraph& graph, int start, int depth)
{
    QSet<int> seen;
    if (start < 0 || start >= graph.nodes.size())
        return seen;
    const QList<QList<int>> adj = graph.adjacency();
    seen.insert(start);
    QList<int> frontier{start};
    for (int level = 0; (depth < 0 || level < depth) && !frontier.isEmpty(); ++level) {
        QList<int> next;
        for (int n : frontier) {
            for (int m : adj.at(n)) {
                if (!seen.contains(m)) {
                    seen.insert(m);
                    next << m;
                }
            }
        }
        frontier = next;
    }
    return seen;
}

QString graphExcerpt(const QString& markdown, int length)
{
    static const QRegularExpression fence(QStringLiteral("^\\s*(```|~~~)"));
    static const QRegularExpression wikiAlias(QStringLiteral("\\[\\[[^\\]|]*\\|([^\\]]*)\\]\\]"));
    static const QRegularExpression wiki(QStringLiteral("!?\\[\\[([^\\]#^|]*)[^\\]]*\\]\\]"));
    static const QRegularExpression image(QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]*)\\]\\([^)]*\\)"));
    static const QRegularExpression marks(QStringLiteral("(\\*\\*|__|\\*|`|~~|==)"));
    static const QRegularExpression lead(QStringLiteral("^\\s*(>+\\s*|[-*+]\\s+(\\[[ xX]\\]\\s+)?|\\d+[.)]\\s+)"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));

    QStringList parts;
    bool inFence = false;
    const QStringList lines = stripFrontMatter(markdown).split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (fence.match(line).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (inFence || line.trimmed().startsWith(QLatin1Char('#')) || line.trimmed().startsWith(QLatin1Char('|')))
            continue;
        line.replace(lead, QString());
        line.replace(image, QString());
        line.replace(wikiAlias, QStringLiteral("\\1"));
        line.replace(wiki, QStringLiteral("\\1"));
        line.replace(link, QStringLiteral("\\1"));
        line.replace(marks, QString());
        line = line.trimmed();
        if (!line.isEmpty())
            parts << line;
        if (parts.join(QLatin1Char(' ')).size() > length)
            break;
    }
    QString text = parts.join(QLatin1Char(' ')).replace(spaces, QStringLiteral(" ")).trimmed();
    if (text.size() > length) {
        text.truncate(length);
        const qsizetype cut = text.lastIndexOf(QLatin1Char(' '));
        if (cut > length / 2)
            text.truncate(cut);
        text = text.trimmed() + QStringLiteral("…");
    }
    return text;
}

}  // namespace studylab
