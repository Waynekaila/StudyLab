#include "ui/graph/GraphPage.h"

#include "services/knowledge/ForceLayout.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "ui/components/PanelSplitter.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/components/Widgets.h"
#include "ui/graph/GraphView.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QCheckBox>
#include <QCompleter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <numbers>

namespace studylab::ui {

namespace {

const QList<GraphNodeKind> kKinds{GraphNodeKind::Document, GraphNodeKind::Note, GraphNodeKind::Concept,
                                  GraphNodeKind::Unresolved};

/// Pictogramme d'un type de nœud, dessiné comme dans le graphe.
QIcon kindIcon(GraphNodeKind kind, const Palette& p, int size)
{
    const qreal dpr = 2.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const QRectF r(1.5, 1.5, size - 3, size - 3);
    const QColor c = p.text2;
    switch (kind) {
    case GraphNodeKind::Document:
        g.setPen(QPen(c, 1.6));
        g.setBrush(Qt::NoBrush);
        g.drawRoundedRect(r.adjusted(0.5, 1.5, -0.5, -1.5), 2, 2);
        break;
    case GraphNodeKind::Note:
        g.setPen(Qt::NoPen);
        g.setBrush(c);
        g.drawEllipse(r.adjusted(1, 1, -1, -1));
        break;
    case GraphNodeKind::Concept: {
        g.setPen(Qt::NoPen);
        g.setBrush(c);
        const QPointF m = r.center();
        const double d = r.width() / 2;
        g.drawPolygon(QPolygonF({m + QPointF(0, -d), m + QPointF(d, 0), m + QPointF(0, d), m + QPointF(-d, 0)}));
        break;
    }
    case GraphNodeKind::Unresolved:
        g.setPen(QPen(c, 1.4, Qt::DashLine));
        g.setBrush(Qt::NoBrush);
        g.drawEllipse(r.adjusted(1, 1, -1, -1));
        break;
    }
    return QIcon(pm);
}

QString kindTitle(GraphNodeKind kind)
{
    switch (kind) {
    case GraphNodeKind::Document:
        return GraphPage::tr("Documents");
    case GraphNodeKind::Note:
        return GraphPage::tr("Notes");
    case GraphNodeKind::Concept:
        return GraphPage::tr("Concepts");
    case GraphNodeKind::Unresolved:
        return GraphPage::tr("Liens non résolus");
    }
    return {};
}

/// Position provisoire (avant la disposition) : près d'un voisin déjà placé, sinon sur une spirale.
QList<QPointF> provisionalPositions(const KnowledgeGraph& g, const QHash<QString, QPointF>& known)
{
    QList<QPointF> pos(g.nodes.size());
    QList<bool> placed(g.nodes.size(), false);
    for (int i = 0; i < g.nodes.size(); ++i) {
        const auto it = known.constFind(g.nodes.at(i).key);
        if (it != known.constEnd()) {
            pos[i] = *it;
            placed[i] = true;
        }
    }
    const QList<QList<int>> adj = g.adjacency();
    int spiral = 0;
    for (int i = 0; i < g.nodes.size(); ++i) {
        if (placed.at(i))
            continue;
        int near = -1;
        for (int j : adj.at(i)) {
            if (placed.at(j)) {
                near = j;
                break;
            }
        }
        const double a = 2.4 * double(spiral);  // angle d'or, à peu près
        const double r = 26.0 * std::sqrt(double(spiral) + 1.0);
        ++spiral;
        pos[i] = near >= 0 ? pos.at(near) + QPointF(std::cos(a) * 40, std::sin(a) * 40)
                           : QPointF(std::cos(a) * r, std::sin(a) * r);
        placed[i] = true;
    }
    return pos;
}

QLabel* statLabel(QWidget* parent)
{
    auto* l = makeLabel(QString(), "faint", parent);
    return l;
}

}  // namespace

GraphPage::GraphPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx), m_knowledge(ctx.db)
{
    setObjectName(QStringLiteral("GraphPage"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_panels = new PanelSplitter(ctx.settings, ctx.theme, QStringLiteral("graph"), this);
    h->addWidget(m_panels, 1);
    buildFilters();
    buildCenter();

    m_watcher = new QFutureWatcher<QList<QPointF>>(this);
    connect(m_watcher, &QFutureWatcher<QList<QPointF>>::finished, this, &GraphPage::onLayoutFinished);

    // Rechargé à l'affichage si quelque chose a changé ailleurs.
    const auto changed = [this] {
        m_dirty = true;
        if (isVisible())
            refresh();
    };
    connect(&m_ctx.notes, &NoteService::noteAdded, this, changed);
    connect(&m_ctx.notes, &NoteService::noteChanged, this, changed);
    connect(&m_ctx.notes, &NoteService::noteRemoved, this, changed);
    connect(&m_ctx.notes, &NoteService::linksChanged, this, changed);
    connect(&m_ctx.library, &LibraryService::documentsChanged, this, changed);
    connect(&m_ctx.organize, &OrganizeService::subjectsChanged, this, changed);
    connect(&m_ctx.organize, &OrganizeService::membershipChanged, this, changed);
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] {
        refreshIcons();
        refreshSubjects();
        m_view->refreshStyle();
        updateCard();
    });
    refreshIcons();
    m_dirty = true;  // calculé au premier affichage : rien à faire au démarrage si l'écran n'est pas ouvert
}

GraphPage::~GraphPage()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher->waitForFinished();
}

// ───────────────────────── Construction ─────────────────────────

void GraphPage::buildFilters()
{
    ThemeManager& theme = m_ctx.theme;
    auto* nav = new QFrame(this);
    nav->setObjectName(QStringLiteral("NotesNav"));
    auto* outer = new QVBoxLayout(nav);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(nav);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("GraphFilters"));
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(8);

    m_navHead = new QHBoxLayout;
    m_navHead->setContentsMargins(0, 0, 0, 0);
    m_navHead->addWidget(makeLabel(tr("Filtres"), "h2", body), 1);
    v->addLayout(m_navHead);
    m_find = new QLineEdit(body);
    m_find->setObjectName(QStringLiteral("GraphFind"));
    m_find->setPlaceholderText(tr("Trouver un nœud…"));
    m_find->setClearButtonEnabled(true);
    m_find->setAccessibleName(tr("Trouver un nœud"));
    auto* completer = new QCompleter(new QStringListModel(m_find), m_find);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_find->setCompleter(completer);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& text) { findNode(text); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { findNode(m_find->text()); });
    v->addWidget(m_find);

    const auto row = [body, v](QLabel* icon, const QString& text, QWidget* control) {
        auto* h = new QHBoxLayout;
        h->setSpacing(10);
        if (icon)
            h->addWidget(icon);
        auto* label = makeLabel(text, nullptr, body);
        label->setBuddy(control);
        h->addWidget(label, 1);
        h->addWidget(control);
        v->addLayout(h);
    };

    v->addSpacing(8);
    v->addWidget(makeLabel(tr("TYPES"), "caption", body));
    for (GraphNodeKind kind : kKinds) {
        auto* toggle = new ToggleSwitch(body);
        toggle->setChecked(kind != GraphNodeKind::Unresolved);
        toggle->setAccessibleName(kindTitle(kind));
        connect(toggle, &ToggleSwitch::toggled, this, [this] { rebuildShown(); });
        auto* icon = new QLabel(body);
        icon->setFixedSize(theme.px(16), theme.px(16));
        m_typeIcons << icon;
        row(icon, kindTitle(kind), toggle);
        switch (kind) {
        case GraphNodeKind::Document:
            m_typeDocs = toggle;
            break;
        case GraphNodeKind::Note:
            m_typeNotes = toggle;
            break;
        case GraphNodeKind::Concept:
            m_typeConcepts = toggle;
            break;
        case GraphNodeKind::Unresolved:
            m_typeUnresolved = toggle;
            break;
        }
    }

    v->addSpacing(8);
    v->addWidget(makeLabel(tr("MATIÈRES"), "caption", body));
    m_subjectsLayout = new QVBoxLayout;
    m_subjectsLayout->setSpacing(4);
    v->addLayout(m_subjectsLayout);
    m_noSubjects = makeLabel(tr("Aucune matière. Les nœuds prennent la couleur de leur matière."), "faint", body);
    m_noSubjects->setWordWrap(true);
    v->addWidget(m_noSubjects);

    v->addSpacing(8);
    v->addWidget(makeLabel(tr("AFFICHAGE"), "caption", body));
    v->addWidget(makeLabel(tr("Profondeur autour de la sélection"), nullptr, body));
    m_depth = new SegmentedControl({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), tr("Tout")}, body);
    m_depth->setObjectName(QStringLiteral("GraphDepth"));
    m_depth->setCurrentIndex(1);
    connect(m_depth, &SegmentedControl::currentIndexChanged, this, [this] {
        if (!m_isolateKey.isEmpty())
            rebuildShown();
        else
            applySelection();
    });
    v->addWidget(m_depth, 0, Qt::AlignLeft);
    v->addSpacing(4);

    m_labels = new ToggleSwitch(body);
    m_labels->setChecked(true);
    m_labels->setAccessibleName(tr("Étiquettes"));
    connect(m_labels, &ToggleSwitch::toggled, this, [this](bool on) { m_view->setLabelsVisible(on); });
    row(nullptr, tr("Étiquettes"), m_labels);
    m_group = new ToggleSwitch(body);
    m_group->setAccessibleName(tr("Grouper par matière"));
    connect(m_group, &ToggleSwitch::toggled, this, [this] {
        m_fitPending = true;
        startLayout();
    });
    row(nullptr, tr("Grouper par matière"), m_group);
    v->addStretch(1);

    scroll->setWidget(body);
    outer->addWidget(scroll);
    m_panels->addPanel(nav, tr("Filtres"), theme.px(260), theme.px(200), theme.px(420));
    if (m_navHead)
        m_navHead->addWidget(m_panels->makeCollapseButton(nav, nav));
}

void GraphPage::buildCenter()
{
    ThemeManager& theme = m_ctx.theme;
    m_stack = new QStackedWidget(this);

    // Graphe vide : rien à relier pour l'instant.
    m_empty = new QWidget(m_stack);
    auto* ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto* et = makeLabel(tr("Votre graphe apparaîtra ici"), "h2", m_empty);
    et->setAlignment(Qt::AlignCenter);
    ev->addWidget(et);
    auto* ed = makeLabel(tr("Il montre vos documents et vos notes, reliés par les [[liens]] que vous écrivez et par les "
                            "notes tirées de vos lectures."),
                         "muted", m_empty);
    ed->setWordWrap(true);
    ed->setAlignment(Qt::AlignCenter);
    ed->setMaximumWidth(theme.px(460));
    // Centré par une rangée, pas par un alignement : un texte replié aligné perd sa hauteur et sa fin est coupée.
    auto* edRow = new QHBoxLayout;
    edRow->addStretch(1);
    edRow->addWidget(ed, 6);
    edRow->addStretch(1);
    ev->addLayout(edRow);
    auto* go = new QPushButton(tr("Mes notes"), m_empty);
    go->setProperty("variant", QStringLiteral("primary"));
    go->setCursor(Qt::PointingHandCursor);
    connect(go, &QPushButton::clicked, this, &GraphPage::notesRequested);
    ev->addWidget(go, 0, Qt::AlignHCenter);
    ev->addStretch(2);
    m_stack->addWidget(m_empty);

    m_canvas = new QWidget(m_stack);
    auto* grid = new QGridLayout(m_canvas);
    grid->setContentsMargins(0, 0, 0, 0);
    m_view = new GraphView(theme, m_canvas);
    grid->addWidget(m_view, 0, 0);
    connect(m_view, &GraphView::nodeClicked, this, [this](int i) {
        m_selectedKey = i >= 0 && i < m_shown.nodes.size() ? m_shown.nodes.at(i).key : QString();
        applySelection();
    });
    connect(m_view, &GraphView::nodeActivated, this, [this](int i) {
        if (i < 0 || i >= m_shown.nodes.size())
            return;
        m_selectedKey = m_shown.nodes.at(i).key;
        applySelection();
        openSelected();
    });

    // Fiche du nœud, en haut à droite
    auto* cardHost = new QWidget(m_canvas);
    auto* ch = new QVBoxLayout(cardHost);
    ch->setContentsMargins(0, theme.px(20), theme.px(20), 0);
    m_card = makeCard(cardHost);
    m_card->setObjectName(QStringLiteral("GraphCard"));
    m_card->setFixedWidth(theme.px(340));
    auto* cl = static_cast<QVBoxLayout*>(m_card->layout());
    cl->setSpacing(8);
    auto* kindRow = new QHBoxLayout;
    kindRow->setSpacing(8);
    m_cardDot = new QLabel(m_card);
    kindRow->addWidget(m_cardDot);
    m_cardKind = makeLabel(QString(), "muted", m_card);
    kindRow->addWidget(m_cardKind, 1);
    cl->addLayout(kindRow);
    m_cardTitle = makeLabel(QString(), "h2", m_card);
    m_cardTitle->setWordWrap(true);
    cl->addWidget(m_cardTitle);
    m_cardExcerpt = makeLabel(QString(), "muted", m_card);
    m_cardExcerpt->setWordWrap(true);
    cl->addWidget(m_cardExcerpt);
    auto* stats = new QHBoxLayout;
    stats->setSpacing(16);
    for (int i = 0; i < 3; ++i) {
        auto* col = new QVBoxLayout;
        col->setSpacing(0);
        auto* value = makeLabel(QString(), "h3", m_card);
        auto* label = statLabel(m_card);
        col->addWidget(value);
        col->addWidget(label);
        stats->addLayout(col);
        m_statValues << value;
        m_statLabels << label;
    }
    stats->addStretch(1);
    cl->addSpacing(2);
    cl->addLayout(stats);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_open = new QPushButton(tr("Ouvrir"), m_card);
    m_open->setProperty("variant", QStringLiteral("primary"));
    m_open->setCursor(Qt::PointingHandCursor);
    connect(m_open, &QPushButton::clicked, this, &GraphPage::openSelected);
    m_center = new QPushButton(tr("Centrer"), m_card);
    m_center->setCursor(Qt::PointingHandCursor);
    connect(m_center, &QPushButton::clicked, this, [this] { m_view->centerOnNode(m_view->selected()); });
    m_isolate = new QPushButton(tr("Isoler"), m_card);
    m_isolate->setCursor(Qt::PointingHandCursor);
    connect(m_isolate, &QPushButton::clicked, this, [this] {
        isolate(m_isolateKey == m_selectedKey ? QString() : m_selectedKey);
    });
    actions->addWidget(m_open);
    actions->addWidget(m_center);
    actions->addWidget(m_isolate);
    actions->addStretch(1);
    cl->addSpacing(4);
    cl->addLayout(actions);
    ch->addWidget(m_card);
    m_card->hide();
    grid->addWidget(cardHost, 0, 0, Qt::AlignTop | Qt::AlignRight);

    // Zoom et état, en bas à gauche
    auto* barHost = new QWidget(m_canvas);
    auto* bh = new QHBoxLayout(barHost);
    bh->setContentsMargins(theme.px(20), 0, 0, theme.px(16));
    m_toolbar = new QFrame(barHost);
    m_toolbar->setObjectName(QStringLiteral("GraphToolbar"));
    auto* tb = new QHBoxLayout(m_toolbar);
    tb->setContentsMargins(0, 0, 0, 0);
    tb->setSpacing(8);
    const auto tool = [this, tb](const QString& name, const QString& text, auto slot) {
        auto* b = new QToolButton(m_toolbar);
        b->setObjectName(name);
        b->setToolTip(text);
        b->setAccessibleName(text);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, slot);
        tb->addWidget(b);
        return b;
    };
    tool(QStringLiteral("GraphZoomIn"), tr("Zoom avant"), [this] { m_view->zoomBy(1.25); });
    tool(QStringLiteral("GraphZoomOut"), tr("Zoom arrière"), [this] { m_view->zoomBy(0.8); });
    tool(QStringLiteral("GraphFit"), tr("Tout voir"), [this] { m_view->fitAll(); });
    m_showAll = tool(QStringLiteral("GraphShowAll"), tr("Tout afficher"), [this] { isolate(QString()); });
    m_showAll->setText(tr("Tout afficher"));
    m_showAll->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_showAll->hide();
    m_status = makeLabel(QString(), "faint", m_toolbar);
    tb->addSpacing(6);
    tb->addWidget(m_status);
    bh->addWidget(m_toolbar);
    grid->addWidget(barHost, 0, 0, Qt::AlignBottom | Qt::AlignLeft);

    m_stack->addWidget(m_canvas);
    m_panels->addWorkArea(m_stack);
}

void GraphPage::refreshIcons()
{
    const Palette& p = m_ctx.theme.palette();
    const int s = m_ctx.theme.px(16);
    for (int i = 0; i < m_typeIcons.size() && i < kKinds.size(); ++i)
        m_typeIcons.at(i)->setPixmap(kindIcon(kKinds.at(i), p, s).pixmap(s, s));
    const QList<QPair<QString, QString>> icons{{QStringLiteral("GraphZoomIn"), QStringLiteral("plus")},
                                               {QStringLiteral("GraphZoomOut"), QStringLiteral("minus")},
                                               {QStringLiteral("GraphFit"), QStringLiteral("maximize")},
                                               {QStringLiteral("GraphShowAll"), QStringLiteral("x")}};
    for (const auto& [name, icon] : icons) {
        if (auto* b = m_toolbar->findChild<QToolButton*>(name))
            b->setIcon(Icons::get(icon, p.text2, 16));
    }
}

// ───────────────────────── Données ─────────────────────────

void GraphPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty)
        refresh();
    QTimer::singleShot(0, this, [this] {
        if (m_fitPending && !m_running && isVisible()) {
            m_fitPending = false;
            m_view->fitAll();
        }
    });
}

void GraphPage::refresh()
{
    m_dirty = false;
    auto graph = m_knowledge.graph();
    m_full = graph ? graph.value() : KnowledgeGraph{};
    refreshSubjects();
    rebuildShown();
}

void GraphPage::refreshSubjects()
{
    while (QLayoutItem* item = m_subjectsLayout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_subjectBoxes.clear();
    auto subjects = m_ctx.organize.subjects();
    QList<SubjectRecord> active;
    if (subjects) {
        for (const SubjectRecord& s : subjects.value()) {
            if (!s.archived)
                active << s;
        }
    }
    m_noSubjects->setVisible(active.isEmpty());
    if (active.isEmpty())
        return;
    const Palette& p = m_ctx.theme.palette();
    QWidget* parent = m_noSubjects->parentWidget();
    const auto box = [this, parent, &p](const QString& text, const QString& color, qint64 id) {
        auto* b = new QCheckBox(text, parent);
        b->setProperty("subjectId", id);
        if (!color.isEmpty())
            b->setIcon(colorDot(p, color));
        b->setChecked(id > 0 ? !m_hiddenSubjects.contains(id) : !m_hideUnfiled);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QCheckBox::toggled, this, [this, id](bool on) {
            if (id > 0) {
                if (on)
                    m_hiddenSubjects.remove(id);
                else
                    m_hiddenSubjects.insert(id);
            } else {
                m_hideUnfiled = !on;
            }
            rebuildShown();
        });
        m_subjectsLayout->addWidget(b);
        m_subjectBoxes << b;
    };
    for (const SubjectRecord& s : active)
        box(s.name, s.color, s.id);
    box(tr("Sans matière"), QString(), 0);
}

GraphFilter GraphPage::currentFilter() const
{
    GraphFilter f;
    f.documents = m_typeDocs->isChecked();
    f.notes = m_typeNotes->isChecked();
    f.concepts = m_typeConcepts->isChecked();
    f.unresolved = m_typeUnresolved->isChecked();
    f.filterSubjects = !m_hiddenSubjects.isEmpty() || m_hideUnfiled;
    f.unfiled = !m_hideUnfiled;
    for (QCheckBox* b : m_subjectBoxes) {
        const qint64 id = b->property("subjectId").toLongLong();
        if (id > 0 && !m_hiddenSubjects.contains(id))
            f.subjects.insert(id);
    }
    return f;
}

int GraphPage::depth() const
{
    const int i = m_depth->currentIndex();
    return i >= 0 && i < 3 ? i + 1 : -1;
}

void GraphPage::rebuildShown()
{
    KnowledgeGraph g = filterGraph(m_full, currentFilter());
    if (!m_isolateKey.isEmpty()) {
        const int center = g.indexOf(m_isolateKey);
        if (center < 0)
            m_isolateKey.clear();
        else
            g = subgraph(g, graphNeighborhood(g, center, depth()));
    }

    QStringList sig;
    QStringList titles;
    bool unknown = false;
    for (const GraphNode& n : g.nodes) {
        sig << n.key;
        titles << n.title;
        unknown = unknown || !m_positions.contains(n.key);
    }
    for (const GraphEdge& e : g.edges)
        sig << QStringLiteral("%1>%2").arg(g.nodes.at(e.from).key, g.nodes.at(e.to).key);
    const QString signature = sig.join(QLatin1Char('|'));

    // Les positions de la vue (nœuds déplacés à la main) valent mieux que celles du dernier calcul.
    const QHash<QString, QPointF> current = m_view->positionsByKey();
    for (auto it = current.begin(); it != current.end(); ++it)
        m_positions.insert(it.key(), it.value());

    m_shown = g;
    m_view->setGraph(m_shown, provisionalPositions(m_shown, m_positions));
    m_view->setLabelsVisible(m_labels->isChecked());
    if (auto* model = qobject_cast<QStringListModel*>(m_find->completer()->model())) {
        titles.removeDuplicates();
        model->setStringList(titles);
    }
    m_stack->setCurrentWidget(m_full.nodes.isEmpty() ? m_empty : m_canvas);
    m_showAll->setVisible(!m_isolateKey.isEmpty());
    applySelection();

    if (signature != m_shownSignature || unknown) {
        m_shownSignature = signature;
        startLayout();
    } else if (m_fitPending && !m_running && isVisible()) {
        m_fitPending = false;
        m_view->fitAll();
    }
    updateStatus();
}

// ───────────────────────── Disposition ─────────────────────────

void GraphPage::startLayout()
{
    if (m_running) {
        // Le calcul en cours porte sur un graphe dépassé : on l'arrête et on recommence à la fin.
        if (m_cancel)
            m_cancel->store(true);
        m_relayoutPending = true;
        return;
    }
    m_running = true;
    LayoutOptions options;
    options.groupBySubject = m_group->isChecked();
    options.initial = m_view->positionsByKey();
    for (auto it = m_positions.begin(); it != m_positions.end(); ++it) {
        if (!options.initial.contains(it.key()))
            options.initial.insert(it.key(), it.value());
    }
    // Un nouveau nœud part de la position provisoire où la vue l'a placé : déjà dans options.initial.
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const KnowledgeGraph graph = m_shown;
    const auto cancel = m_cancel;
    ++m_generation;
    m_watcher->setFuture(QtConcurrent::run([graph, options, cancel] { return forceLayout(graph, options, cancel.get()); }));
    updateStatus();
}

void GraphPage::onLayoutFinished()
{
    m_running = false;
    if (m_relayoutPending) {
        m_relayoutPending = false;
        startLayout();
        return;
    }
    const QList<QPointF> result = m_watcher->result();
    if (result.size() == m_shown.nodes.size()) {
        for (int i = 0; i < result.size(); ++i)
            m_positions.insert(m_shown.nodes.at(i).key, result.at(i));
        // Cadrage seulement quand la vue a sa taille réelle (page affichée) ; sinon à l'affichage.
        const bool fit = m_fitPending && isVisible();
        m_view->setPositions(result, !m_fitPending && isVisible());
        if (fit) {
            m_fitPending = false;
            m_view->fitAll();
        }
    }
    updateStatus();
}

// ───────────────────────── Sélection et fiche ─────────────────────────

bool GraphPage::selectNode(const QString& key)
{
    if (m_shown.indexOf(key) < 0)
        return false;
    m_selectedKey = key;
    applySelection();
    return true;
}

bool GraphPage::findNode(const QString& title)
{
    const QString wanted = title.trimmed();
    if (wanted.isEmpty())
        return false;
    int found = -1;
    for (int i = 0; i < m_shown.nodes.size() && found < 0; ++i) {
        if (m_shown.nodes.at(i).title.compare(wanted, Qt::CaseInsensitive) == 0)
            found = i;
    }
    for (int i = 0; i < m_shown.nodes.size() && found < 0; ++i) {
        if (m_shown.nodes.at(i).title.contains(wanted, Qt::CaseInsensitive))
            found = i;
    }
    if (found < 0)
        return false;
    m_selectedKey = m_shown.nodes.at(found).key;
    applySelection();
    m_view->centerOnNode(found);
    return true;
}

void GraphPage::isolate(const QString& key)
{
    m_isolateKey = key;
    m_fitPending = true;
    rebuildShown();
}

void GraphPage::applySelection()
{
    const int index = m_shown.indexOf(m_selectedKey);
    if (index < 0)
        m_selectedKey.clear();
    m_view->setSelected(index);
    m_view->setHighlight(index >= 0 ? graphNeighborhood(m_shown, index, depth()) : QSet<int>());
    updateCard();
}

void GraphPage::updateCard()
{
    const int index = m_shown.indexOf(m_selectedKey);
    if (index < 0) {
        m_card->hide();
        return;
    }
    const GraphNode& n = m_shown.nodes.at(index);
    const Palette& p = m_ctx.theme.palette();
    const int s = m_ctx.theme.px(10);
    m_cardDot->setPixmap(kindIcon(n.kind, p, s).pixmap(s, s));
    if (!n.color.isEmpty() && n.kind != GraphNodeKind::Unresolved)
        m_cardDot->setPixmap(colorDot(p, n.color, s).pixmap(s, s));

    QString kind = graphNodeKindLabel(n.kind);
    if (n.kind == GraphNodeKind::Note || (n.kind == GraphNodeKind::Document && !n.detail.isEmpty()))
        kind += QStringLiteral(" · ") + n.detail;
    m_cardKind->setText(kind);
    m_cardTitle->setText(n.title);

    QString excerpt = n.excerpt;
    if (n.kind == GraphNodeKind::Unresolved)
        excerpt = tr("Aucune note ne porte encore ce titre.");
    m_cardExcerpt->setText(excerpt);
    m_cardExcerpt->setVisible(!excerpt.isEmpty());

    struct Stat {
        int value;
        QString singular, plural;
    };
    QList<Stat> stats;
    switch (n.kind) {
    case GraphNodeKind::Note:
    case GraphNodeKind::Concept:
        stats = {{n.outLinks, tr("lien sortant"), tr("liens sortants")},
                 {n.backlinks, tr("rétrolien"), tr("rétroliens")},
                 {n.sources, tr("source"), tr("sources")}};
        break;
    case GraphNodeKind::Document:
        stats = {{n.backlinks, tr("note liée"), tr("notes liées")},
                 {n.sources, tr("note tirée"), tr("notes tirées")},
                 {n.annotations, tr("annotation"), tr("annotations")}};
        break;
    case GraphNodeKind::Unresolved:
        stats = {{n.backlinks, tr("note y renvoie"), tr("notes y renvoient")}};
        break;
    }
    for (int i = 0; i < m_statValues.size(); ++i) {
        const bool shown = i < stats.size();
        m_statValues.at(i)->setVisible(shown);
        m_statLabels.at(i)->setVisible(shown);
        if (!shown)
            continue;
        m_statValues.at(i)->setText(QString::number(stats.at(i).value));
        m_statLabels.at(i)->setText(stats.at(i).value > 1 ? stats.at(i).plural : stats.at(i).singular);
    }
    m_open->setText(n.kind == GraphNodeKind::Unresolved ? tr("Créer la note") : tr("Ouvrir"));
    m_isolate->setText(m_isolateKey == n.key ? tr("Tout afficher") : tr("Isoler"));
    m_card->show();
}

void GraphPage::updateStatus()
{
    QStringList parts;
    if (!m_isolateKey.isEmpty()) {
        const int i = m_shown.indexOf(m_isolateKey);
        if (i >= 0)
            parts << tr("Voisinage de « %1 »").arg(m_shown.nodes.at(i).title);
    }
    parts << countLabel(m_shown.nodes.size(), tr("nœud"), tr("nœuds"));
    parts << countLabel(m_shown.edges.size(), tr("lien"), tr("liens"));
    parts << (m_running ? tr("calcul de la disposition…") : tr("disposition calculée en arrière-plan"));
    m_status->setText(parts.join(QStringLiteral(" · ")));
}

void GraphPage::openSelected()
{
    const int index = m_shown.indexOf(m_selectedKey);
    if (index < 0)
        return;
    const GraphNode& n = m_shown.nodes.at(index);
    switch (n.kind) {
    case GraphNodeKind::Document:
        emit openDocumentRequested(n.id);
        break;
    case GraphNodeKind::Note:
    case GraphNodeKind::Concept:
        emit openNoteRequested(n.id);
        break;
    case GraphNodeKind::Unresolved:
        emit createNoteRequested(n.title);
        break;
    }
}

ToggleSwitch* GraphPage::typeToggle(GraphNodeKind kind) const
{
    switch (kind) {
    case GraphNodeKind::Document:
        return m_typeDocs;
    case GraphNodeKind::Note:
        return m_typeNotes;
    case GraphNodeKind::Concept:
        return m_typeConcepts;
    case GraphNodeKind::Unresolved:
        return m_typeUnresolved;
    }
    return nullptr;
}

}  // namespace studylab::ui
