#include "ui/shell/Sidebar.h"

#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {
QPixmap logoPixmap(const Palette& p, int size, qreal dpr)
{
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, QColor(0x6E, 0x8F, 0xC2));
    g.setColorAt(1, p.lavender);
    painter.setPen(Qt::NoPen);
    painter.setBrush(g);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 8, 8);
    const int inner = size * 4 / 7;
    const QPixmap book = Icons::get(QStringLiteral("book"), Qt::white, inner).pixmap(inner, inner);
    painter.drawPixmap((size - inner) / 2, (size - inner) / 2, book);
    return pm;
}
}  // namespace

Sidebar::Sidebar(ThemeManager& theme, const QList<Entry>& entries, QWidget* parent)
    : QFrame(parent), m_theme(theme), m_entries(entries), m_group(new QButtonGroup(this))
{
    setObjectName(QStringLiteral("Sidebar"));
    setFixedWidth(228);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 14, 10, 10);
    layout->setSpacing(2);

    auto* brandRow = m_brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(8, 0, 0, 10);
    brandRow->setSpacing(10);
    m_logo = new QLabel(this);
    m_logo->setFixedSize(28, 28);
    auto* brand = m_brand = new QLabel(QStringLiteral("StudyLab"), this);
    brand->setObjectName(QStringLiteral("Brand"));
    // Réduire la navigation : en haut, à côté du nom, là où l'œil cherche ce réglage.
    m_compactButton = new QToolButton(this);
    m_compactButton->setObjectName(QStringLiteral("SidebarCompact"));
    m_compactButton->setProperty("readerTool", true);
    m_compactButton->setAutoRaise(true);
    m_compactButton->setCursor(Qt::PointingHandCursor);
    m_compactButton->setFocusPolicy(Qt::TabFocus);
    connect(m_compactButton, &QToolButton::clicked, this, [this] {
        setCompact(!m_compact);
        emit compactToggled(m_compact);
    });
    brandRow->addWidget(m_logo);
    brandRow->addWidget(brand, 1);
    brandRow->addWidget(m_compactButton);
    layout->addLayout(brandRow);

    m_search = new QPushButton(tr("Rechercher…"), this);
    m_search->setObjectName(QStringLiteral("SidebarSearch"));
    m_search->setToolTip(tr("Rechercher partout (Ctrl+K)"));
    m_search->setAccessibleName(tr("Rechercher partout"));
    m_search->setCursor(Qt::PointingHandCursor);
    m_search->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(m_search);
    layout->addSpacing(8);
    connect(m_search, &QPushButton::clicked, this, &Sidebar::searchRequested);

    m_navLayout = new QVBoxLayout;
    m_navLayout->setContentsMargins(0, 0, 0, 0);
    m_navLayout->setSpacing(2);
    layout->addLayout(m_navLayout);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
        m_current = m_entries[id].id;
        emit navigated(m_entries[id].id);
    });
    rebuild();

    layout->addStretch(1);
    auto* line = new QFrame(this);
    line->setObjectName(QStringLiteral("SidebarFooterLine"));
    line->setFixedHeight(1);
    layout->addWidget(line);
    auto* footer = m_footer = new QHBoxLayout;
    footer->setContentsMargins(10, 10, 10, 2);
    footer->setSpacing(8);
    m_footerIcon = new QLabel(this);
    m_footerText = new QLabel(tr("Hors ligne · données locales"), this);
    m_footerText->setObjectName(QStringLiteral("SidebarFooter"));
    footer->addWidget(m_footerIcon);
    footer->addWidget(m_footerText, 1);
    layout->addLayout(footer);

    connect(&m_theme, &ThemeManager::themeChanged, this, &Sidebar::refreshIcons);
    refreshIcons();
    applyCompact();
}

void Sidebar::rebuild()
{
    for (QToolButton* b : std::as_const(m_buttons)) {
        m_group->removeButton(b);
        b->hide();
        b->deleteLater();
    }
    m_buttons.clear();
    m_badges.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        auto* b = new QToolButton(this);
        b->setProperty("nav", true);
        b->setObjectName(QStringLiteral("nav:") + m_entries[i].id);
        b->setText(m_compact ? QString() : QStringLiteral("  ") + m_entries[i].label);  // espace icône / libellé
        b->setAccessibleName(m_entries[i].label);
        b->setToolButtonStyle(m_compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        b->setToolTip(m_entries[i].label);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);  // anneau de focus au clavier seulement
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setIconSize(QSize(18, 18));
        // Pastille (nombre) calée à droite du bouton, masquée tant qu'elle est vide.
        auto* row = new QHBoxLayout(b);
        row->setContentsMargins(0, m_compact ? 2 : 0, m_compact ? 2 : 8, 0);
        row->addStretch(1);
        auto* badge = new QLabel(b);
        badge->setObjectName(QStringLiteral("NavBadge"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        badge->hide();
        badge->setProperty("compact", m_compact);
        row->addWidget(badge, 0, m_compact ? Qt::AlignTop : Qt::AlignVCenter);
        m_badges.append(badge);
        m_group->addButton(b, i);
        m_buttons.append(b);
        m_navLayout->addWidget(b);
    }
    for (auto it = m_badgeCounts.cbegin(); it != m_badgeCounts.cend(); ++it)
        setBadge(it.key(), it.value());
    // Aucune entrée cochée si l'espace courant est masqué.
    m_group->setExclusive(false);
    for (int i = 0; i < m_entries.size(); ++i)
        m_buttons[i]->setChecked(m_entries[i].id == m_current);
    m_group->setExclusive(true);
}

void Sidebar::setEntries(const QList<Entry>& entries)
{
    m_entries = entries;
    rebuild();
    refreshIcons();
}

QStringList Sidebar::entryIds() const
{
    QStringList out;
    for (const Entry& e : m_entries)
        out << e.id;
    return out;
}

void Sidebar::refreshIcons()
{
    const Palette& p = m_theme.palette();
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons[i]->setIcon(Icons::stateful(m_entries[i].icon, p.text2, p.primaryStrong));
    m_logo->setPixmap(logoPixmap(p, 28, devicePixelRatioF()));
    m_search->setIcon(Icons::get(QStringLiteral("search"), p.text3, 16));
    m_footerIcon->setPixmap(Icons::get(QStringLiteral("shield"), p.text3, 14).pixmap(14, 14));
    m_compactButton->setIcon(Icons::get(QStringLiteral("panel-left"), p.text3, 16));
    setFixedWidth(m_theme.px(m_compact ? 60 : 228));
}

void Sidebar::setCompactShortcut(const QString& text)
{
    m_compactShortcut = text;
    applyCompact();
}

void Sidebar::setCompact(bool compact)
{
    if (compact == m_compact)
        return;
    m_compact = compact;
    rebuild();
    refreshIcons();
    applyCompact();
}

void Sidebar::applyCompact()
{
    // Réduite, la colonne garde le bouton pour se déplier à la place du logo.
    m_brand->setVisible(!m_compact);
    m_logo->setVisible(!m_compact);
    m_footerIcon->setVisible(!m_compact);
    m_footerText->setVisible(!m_compact);
    m_brandRow->setContentsMargins(m_compact ? 0 : 8, 0, 0, 10);
    m_brandRow->setAlignment(m_compactButton, m_compact ? Qt::AlignHCenter : Qt::Alignment());
    m_search->setText(m_compact ? QString() : tr("Rechercher…"));
    const QString key = m_compactShortcut.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(m_compactShortcut);
    m_compactButton->setToolTip((m_compact ? tr("Déplier la navigation") : tr("Réduire la navigation")) + key);
    m_compactButton->setAccessibleName(m_compact ? tr("Déplier la navigation") : tr("Réduire la navigation"));
    setFixedWidth(m_theme.px(m_compact ? 60 : 228));
}

void Sidebar::setCurrent(const QString& id)
{
    m_current = id;
    m_group->setExclusive(false);
    for (int i = 0; i < m_entries.size(); ++i)
        m_buttons[i]->setChecked(m_entries[i].id == id);
    m_group->setExclusive(true);
}

QString Sidebar::current() const { return m_current; }

void Sidebar::setBadge(const QString& id, int count)
{
    m_badgeCounts.insert(id, count);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id != id)
            continue;
        m_badges[i]->setText(count > 999 ? QStringLiteral("999+") : QString::number(count));
        m_badges[i]->setVisible(count > 0);
        m_buttons[i]->setAccessibleName(count > 0 ? tr("%1, %2 à revoir").arg(m_entries[i].label).arg(count)
                                                  : m_entries[i].label);
    }
}

QLabel* Sidebar::badge(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id)
            return m_badges[i];
    }
    return nullptr;
}

QToolButton* Sidebar::button(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id)
            return m_buttons[i];
    }
    return nullptr;
}

}  // namespace studylab::ui
