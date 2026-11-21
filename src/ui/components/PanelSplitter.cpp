#include "ui/components/PanelSplitter.h"

#include "services/settings/SettingsService.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QResizeEvent>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace studylab::ui {

namespace {
QString settingsKey(const QString& id) { return QStringLiteral("ui/panels/") + id; }
}  // namespace

PanelSplitter::PanelSplitter(SettingsService& settings, ThemeManager& theme, const QString& id, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_theme(theme), m_id(id), m_split(new QSplitter(Qt::Horizontal, this))
{
    setObjectName(QStringLiteral("PanelSplitter"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_split->setObjectName(QStringLiteral("PanelSplit"));
    m_split->setHandleWidth(m_theme.px(5));
    m_split->setOpaqueResize(true);
    h->addWidget(m_split, 1);
    connect(m_split, &QSplitter::splitterMoved, this, &PanelSplitter::onSplitterMoved);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &PanelSplitter::save);
    connect(&m_theme, &ThemeManager::themeChanged, this, &PanelSplitter::refreshIcons);
}

PanelSplitter::~PanelSplitter()
{
    if (m_saveTimer.isActive())
        save();
}

void PanelSplitter::addPanel(QWidget* panel, const QString& title, int defaultWidth, int minWidth, int maxWidth, Mode mode)
{
    Panel p;
    p.widget = panel;
    p.title = title;
    p.width = defaultWidth;
    p.minWidth = minWidth;
    p.maxWidth = maxWidth;
    p.mode = mode;
    p.leading = m_workArea == nullptr;

    // Largeur et état mémorisés (même ordre d'ajout que lors de l'enregistrement).
    const QJsonArray saved = m_settings.value(settingsKey(m_id)).toArray();
    const int index = int(m_panels.size());
    if (index < saved.size()) {
        const QJsonObject o = saved.at(index).toObject();
        if (o.contains(QStringLiteral("w")))
            p.width = std::clamp(o.value(QStringLiteral("w")).toInt(defaultWidth), minWidth, maxWidth);
        if (mode == Mode::Collapsible)
            p.open = o.value(QStringLiteral("open")).toBool(true);
    }

    panel->setMinimumWidth(minWidth);
    panel->setMaximumWidth(maxWidth);
    m_split->addWidget(panel);
    const int splitIndex = m_split->indexOf(panel);
    // Seul un panneau rétractable se replie en le tirant : les autres ont leur propre bouton.
    m_split->setCollapsible(splitIndex, mode == Mode::Collapsible);
    m_split->setStretchFactor(splitIndex, 0);
    if (mode == Mode::Collapsible)
        panel->setVisible(p.open);
    panel->installEventFilter(this);

    if (mode == Mode::Collapsible) {
        // Bande du panneau replié : un bouton pour le rouvrir, toujours à portée.
        auto* rail = new QFrame(this);
        rail->setObjectName(QStringLiteral("PanelRail"));
        rail->setProperty("side", p.leading ? QStringLiteral("left") : QStringLiteral("right"));
        rail->setFixedWidth(m_theme.px(36));
        auto* rv = new QVBoxLayout(rail);
        rv->setContentsMargins(0, m_theme.px(10), 0, m_theme.px(10));
        rv->setSpacing(0);
        auto* open = new QToolButton(rail);
        open->setObjectName(QStringLiteral("PanelRailButton"));
        open->setProperty("readerTool", true);
        open->setAutoRaise(true);
        open->setCursor(Qt::PointingHandCursor);
        open->setToolTip(tr("Afficher : %1").arg(title));
        open->setAccessibleName(tr("Afficher le panneau %1").arg(title));
        rv->addWidget(open, 0, Qt::AlignHCenter);
        rv->addStretch(1);
        rail->setVisible(!p.open);
        auto* box = static_cast<QHBoxLayout*>(layout());
        if (p.leading)
            box->insertWidget(box->indexOf(m_split), rail);
        else
            box->addWidget(rail);
        p.rail = rail;
        p.railButton = open;
        connect(open, &QToolButton::clicked, this, [this, panel] { setPanelOpen(panel, true); });
    }

    m_panels.append(p);
    refreshIcons();
}

void PanelSplitter::addWorkArea(QWidget* area)
{
    m_workArea = area;
    m_split->addWidget(area);
    const int i = m_split->indexOf(area);
    m_split->setCollapsible(i, false);
    m_split->setStretchFactor(i, 1);
}

PanelSplitter::Panel* PanelSplitter::find(QWidget* panel)
{
    for (Panel& p : m_panels)
        if (p.widget == panel)
            return &p;
    return nullptr;
}

const PanelSplitter::Panel* PanelSplitter::find(QWidget* panel) const
{
    for (const Panel& p : m_panels)
        if (p.widget == panel)
            return &p;
    return nullptr;
}

bool PanelSplitter::isPanelOpen(QWidget* panel) const
{
    const Panel* p = find(panel);
    if (!p)
        return false;
    return p->mode == Mode::Collapsible ? p->open : !panel->isHidden();
}

int PanelSplitter::panelWidth(QWidget* panel) const
{
    const Panel* p = find(panel);
    return p ? p->width : 0;
}

QToolButton* PanelSplitter::railButton(QWidget* panel) const
{
    const Panel* p = find(panel);
    return p ? p->railButton : nullptr;
}

QWidget* PanelSplitter::leadingPanel() const
{
    for (const Panel& p : m_panels)
        if (p.leading)
            return p.widget;
    return nullptr;
}

QWidget* PanelSplitter::trailingPanel() const
{
    for (const Panel& p : m_panels)
        if (!p.leading)
            return p.widget;
    return nullptr;
}

void PanelSplitter::setPanelOpen(QWidget* panel, bool open)
{
    Panel* p = find(panel);
    if (!p)
        return;
    if (p->mode == Mode::External) {
        if (p->toggleHandler)
            p->toggleHandler(open);
        else
            panel->setVisible(open);
        return;
    }
    if (p->open == open)
        return;
    p->open = open;
    p->rail->setVisible(!open);
    panel->setVisible(open);
    // La bande ou le panneau reprend le focus clavier du bouton qui vient de disparaître.
    if (open)
        panel->setFocus(Qt::OtherFocusReason);
    else if (p->railButton->isVisibleTo(this))
        p->railButton->setFocus(Qt::OtherFocusReason);
    scheduleSave();
    emit panelToggled(panel, open);
}

void PanelSplitter::togglePanel(QWidget* panel)
{
    setPanelOpen(panel, !isPanelOpen(panel));
}

void PanelSplitter::setToggleHandler(QWidget* panel, std::function<void(bool)> handler)
{
    if (Panel* p = find(panel))
        p->toggleHandler = std::move(handler);
}

QToolButton* PanelSplitter::makeCollapseButton(QWidget* panel, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("PanelCollapseButton"));
    b->setProperty("readerTool", true);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    if (Panel* p = find(panel)) {
        b->setToolTip(tr("Replier : %1").arg(p->title));
        b->setAccessibleName(tr("Replier le panneau %1").arg(p->title));
        p->collapseButtons.append(b);
    }
    connect(b, &QToolButton::clicked, this, [this, panel] { setPanelOpen(panel, false); });
    refreshIcons();
    return b;
}

void PanelSplitter::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_applied && m_split->width() > 0) {
        m_applied = true;
        applySizes();
    }
}

bool PanelSplitter::eventFilter(QObject* watched, QEvent* event)
{
    // Un panneau qui réapparaît reprend sa largeur mémorisée ; la zone de travail prend le reste.
    if (event->type() == QEvent::ShowToParent && m_applied && find(static_cast<QWidget*>(watched)))
        QTimer::singleShot(0, this, &PanelSplitter::applySizes);
    return QWidget::eventFilter(watched, event);
}

void PanelSplitter::applySizes()
{
    if (m_split->width() <= 0)
        return;
    m_applying = true;
    QList<int> sizes;
    int used = 0;
    int workIndex = -1;
    int visible = 0;
    for (int i = 0; i < m_split->count(); ++i) {
        QWidget* w = m_split->widget(i);
        if (!w->isHidden())
            ++visible;
        if (w == m_workArea) {
            workIndex = i;
            sizes << 0;
            continue;
        }
        const Panel* p = find(w);
        const int size = p && !w->isHidden() ? p->width : 0;
        sizes << size;
        used += size;
    }
    if (workIndex >= 0) {
        const int handles = m_split->handleWidth() * std::max(0, visible - 1);
        sizes[workIndex] = std::max(m_workArea->minimumSizeHint().width(), m_split->width() - used - handles);
    }
    m_split->setSizes(sizes);
    m_applying = false;
}

void PanelSplitter::onSplitterMoved()
{
    if (m_applying)
        return;
    const QList<int> sizes = m_split->sizes();
    for (Panel& p : m_panels) {
        const int i = m_split->indexOf(p.widget);
        if (i < 0 || p.widget->isHidden())
            continue;
        if (sizes.value(i) <= 0) {
            // Tiré sous sa largeur minimale : replié, en gardant sa dernière largeur pour la réouverture.
            if (p.mode == Mode::Collapsible) {
                QWidget* w = p.widget;
                setPanelOpen(w, false);
            }
        } else {
            p.width = sizes.value(i);
        }
    }
    scheduleSave();
}

void PanelSplitter::scheduleSave() { m_saveTimer.start(); }

void PanelSplitter::save()
{
    m_saveTimer.stop();
    QJsonArray out;
    for (const Panel& p : m_panels)
        out.append(QJsonObject{{QStringLiteral("w"), p.width}, {QStringLiteral("open"), p.open}});
    m_settings.setValue(settingsKey(m_id), out);
}

void PanelSplitter::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    for (Panel& p : m_panels) {
        // Même icône pour replier et rouvrir : celle du panneau, du côté où il se trouve.
        const QString side = p.leading ? QStringLiteral("panel-left") : QStringLiteral("panel-right");
        if (p.railButton)
            p.railButton->setIcon(Icons::get(side, pal.text2, 18));
        for (const QPointer<QToolButton>& b : std::as_const(p.collapseButtons))
            if (b)
                b->setIcon(Icons::get(side, pal.text2, 18));
    }
}

}  // namespace studylab::ui
