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

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(8, 4, 8, 14);
    brandRow->setSpacing(10);
    m_logo = new QLabel(this);
    m_logo->setFixedSize(28, 28);
    auto* brand = new QLabel(QStringLiteral("StudyLab"), this);
    brand->setObjectName(QStringLiteral("Brand"));
    brandRow->addWidget(m_logo);
    brandRow->addWidget(brand, 1);
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

    for (int i = 0; i < m_entries.size(); ++i) {
        auto* b = new QToolButton(this);
        b->setProperty("nav", true);
        b->setText(QStringLiteral("  ") + m_entries[i].label);  // espace entre icône et libellé
        b->setAccessibleName(m_entries[i].label);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);  // anneau de focus au clavier seulement
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setIconSize(QSize(18, 18));
        // Pastille (nombre) calée à droite du bouton, masquée tant qu'elle est vide.
        auto* row = new QHBoxLayout(b);
        row->setContentsMargins(0, 0, 8, 0);
        row->addStretch(1);
        auto* badge = new QLabel(b);
        badge->setObjectName(QStringLiteral("NavBadge"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        badge->hide();
        row->addWidget(badge, 0, Qt::AlignVCenter);
        m_badges.append(badge);
        m_group->addButton(b, i);
        m_buttons.append(b);
        layout->addWidget(b);
    }
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) { emit navigated(m_entries[id].id); });

    layout->addStretch(1);
    auto* line = new QFrame(this);
    line->setObjectName(QStringLiteral("SidebarFooterLine"));
    line->setFixedHeight(1);
    layout->addWidget(line);
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(10, 10, 10, 2);
    footer->setSpacing(8);
    m_footerIcon = new QLabel(this);
    auto* footerText = new QLabel(tr("Hors ligne · données locales"), this);
    footerText->setObjectName(QStringLiteral("SidebarFooter"));
    footer->addWidget(m_footerIcon);
    footer->addWidget(footerText, 1);
    layout->addLayout(footer);

    connect(&m_theme, &ThemeManager::themeChanged, this, &Sidebar::refreshIcons);
    refreshIcons();
}

void Sidebar::refreshIcons()
{
    const Palette& p = m_theme.palette();
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons[i]->setIcon(Icons::stateful(m_entries[i].icon, p.text2, p.primaryStrong));
    m_logo->setPixmap(logoPixmap(p, 28, devicePixelRatioF()));
    m_search->setIcon(Icons::get(QStringLiteral("search"), p.text3, 16));
    m_footerIcon->setPixmap(Icons::get(QStringLiteral("shield"), p.text3, 14).pixmap(14, 14));
    setFixedWidth(m_theme.px(228));
}

void Sidebar::setCurrent(const QString& id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id)
            m_buttons[i]->setChecked(true);
    }
}

QString Sidebar::current() const
{
    const int id = m_group->checkedId();
    return id >= 0 ? m_entries[id].id : QString();
}

void Sidebar::setBadge(const QString& id, int count)
{
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
