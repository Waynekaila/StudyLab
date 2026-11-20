#include "ui/components/Charts.h"

#include "ui/theme/ThemeManager.h"

#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace studylab::ui {

ProgressLine::ProgressLine(ThemeManager& theme, QWidget* parent) : QWidget(parent), m_theme(theme)
{
    setFixedHeight(theme.px(5));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ProgressLine::setValue(double value)
{
    m_value = std::clamp(value, 0.0, 1.0);
    setAccessibleDescription(tr("%1 %").arg(qRound(m_value * 100)));
    update();
}

void ProgressLine::setColor(const QColor& color)
{
    m_color = color;
    update();
}

QSize ProgressLine::sizeHint() const { return {m_theme.px(120), m_theme.px(5)}; }

void ProgressLine::paintEvent(QPaintEvent*)
{
    const Palette& p = m_theme.palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF r(rect());
    const qreal radius = r.height() / 2;
    painter.setBrush(p.sunken);
    painter.drawRoundedRect(r, radius, radius);
    if (m_value <= 0)
        return;
    painter.setBrush(m_color.isValid() ? m_color : p.primary);
    painter.drawRoundedRect(QRectF(r.left(), r.top(), std::max(r.height(), r.width() * m_value), r.height()), radius,
                            radius);
}

// ---------------------------------------------------------------------------------------------------------------

GoalRing::GoalRing(ThemeManager& theme, QWidget* parent) : QWidget(parent), m_theme(theme)
{
    setFixedSize(sizeHint());
}

void GoalRing::setValues(int value, int target, const QString& caption)
{
    m_value = value;
    m_target = target;
    m_caption = caption;
    setAccessibleName(target > 0 ? tr("%1 sur %2").arg(value).arg(target) : QString::number(value));
    update();
}

QSize GoalRing::sizeHint() const { return {m_theme.px(104), m_theme.px(104)}; }

void GoalRing::paintEvent(QPaintEvent*)
{
    const Palette& p = m_theme.palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal stroke = m_theme.px(9);
    const QRectF r = QRectF(rect()).adjusted(stroke / 2 + 1, stroke / 2 + 1, -stroke / 2 - 1, -stroke / 2 - 1);
    QPen track(p.sunken, stroke, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(track);
    painter.drawEllipse(r);
    if (m_target > 0 && m_value > 0) {
        const double f = std::min(1.0, double(m_value) / m_target);
        QPen arc(p.sage, stroke, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arc);
        painter.drawArc(r, 90 * 16, -int(f * 360 * 16));
    }
    QFont big = font();
    big.setPixelSize(m_theme.px(22));
    big.setWeight(QFont::DemiBold);
    QFont small = font();
    small.setPixelSize(m_theme.px(11));
    const QFontMetricsF fmBig(big);
    const QFontMetricsF fmSmall(small);
    const qreal total = fmBig.height() + (m_caption.isEmpty() ? 0 : fmSmall.height());
    QRectF line(rect().left(), (height() - total) / 2, width(), fmBig.height());
    painter.setPen(p.text);
    painter.setFont(big);
    painter.drawText(line, Qt::AlignCenter, QString::number(m_value));
    if (!m_caption.isEmpty()) {
        painter.setPen(p.text2);
        painter.setFont(small);
        painter.drawText(QRectF(rect().left(), line.bottom(), width(), fmSmall.height()), Qt::AlignCenter, m_caption);
    }
}

// ---------------------------------------------------------------------------------------------------------------

BarChart::BarChart(ThemeManager& theme, QWidget* parent) : QWidget(parent), m_theme(theme)
{
    setMinimumHeight(theme.px(120));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void BarChart::setData(const QList<int>& values, const QStringList& labels, const QStringList& tooltips, int highlight)
{
    m_values = values;
    m_labels = labels;
    m_tooltips = tooltips;
    m_highlight = highlight;
    setAccessibleDescription(tooltips.join(QStringLiteral(" ; ")));
    update();
}

QSize BarChart::sizeHint() const { return {m_theme.px(480), m_theme.px(140)}; }

int BarChart::barAt(const QPoint& pos) const
{
    const int n = int(m_values.size());
    if (n == 0)
        return -1;
    const qreal slot = qreal(width()) / n;
    const int i = int(pos.x() / slot);
    return i >= 0 && i < n ? i : -1;
}

bool BarChart::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int i = barAt(help->pos());
        if (i >= 0 && i < m_tooltips.size())
            QToolTip::showText(help->globalPos(), m_tooltips.at(i), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void BarChart::paintEvent(QPaintEvent*)
{
    const int n = int(m_values.size());
    if (n == 0)
        return;
    const Palette& p = m_theme.palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont small = font();
    small.setPixelSize(m_theme.px(10));
    const qreal labelH = m_labels.isEmpty() ? 0 : QFontMetricsF(small).height() + m_theme.px(4);
    const int max = std::max(1, *std::max_element(m_values.begin(), m_values.end()));
    const qreal slot = qreal(width()) / n;
    const qreal gap = std::min<qreal>(m_theme.px(6), slot * 0.3);
    const qreal w = slot - gap;
    const qreal h = height() - labelH;
    const qreal minH = m_theme.px(3);
    painter.setFont(small);
    // Une étiquette sur deux (ou moins) quand la place manque.
    const QFontMetricsF fm(small);
    qreal widest = 0;
    for (const QString& l : m_labels)
        widest = std::max(widest, fm.horizontalAdvance(l));
    const int every = std::max(1, int(std::ceil((widest + m_theme.px(4)) / slot)));
    for (int i = 0; i < n; ++i) {
        const qreal bh = m_values[i] > 0 ? std::max(minH, h * m_values[i] / max) : minH;
        const QRectF bar(i * slot + gap / 2, h - bh, w, bh);
        painter.setPen(Qt::NoPen);
        QColor c = i == m_highlight ? p.lavender : p.primary;
        if (m_values[i] == 0)
            c = p.dark ? p.surface2.lighter(130) : p.sunken;
        painter.setBrush(c);
        const qreal radius = std::min<qreal>(3, w / 2);
        painter.drawRoundedRect(bar, radius, radius);
        if (i < m_labels.size() && (i % every == 0 || i == m_highlight)) {
            painter.setPen(i == m_highlight ? p.text : p.text3);
            painter.drawText(QRectF(i * slot - slot, h + m_theme.px(2), slot * 3, labelH), Qt::AlignHCenter | Qt::AlignTop,
                             m_labels.at(i));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------

ActivityHeatmap::ActivityHeatmap(ThemeManager& theme, QWidget* parent) : QWidget(parent), m_theme(theme)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumWidth(theme.px(160));
}

void ActivityHeatmap::setData(const QDate& first, const QList<int>& levels, const QStringList& tooltips)
{
    m_first = first;
    m_levels = levels;
    m_tooltips = tooltips;
    int active = 0;
    for (int l : levels)
        if (l > 0)
            ++active;
    setAccessibleDescription(tr("%1 jours actifs sur %2").arg(active).arg(levels.size()));
    updateGeometry();
    update();
}

int ActivityHeatmap::columns() const
{
    if (!m_first.isValid() || m_levels.isEmpty())
        return 0;
    const int offset = m_first.dayOfWeek() - 1;
    return (offset + int(m_levels.size()) + 6) / 7;
}

qreal ActivityHeatmap::cell() const
{
    // Cases de 13 px au plus, réduites pour que toutes les semaines tiennent dans la largeur.
    const int n = std::max(1, columns());
    return std::max<qreal>(m_theme.px(5), std::min<qreal>(m_theme.px(13), qreal(width()) / n - m_theme.px(3)));
}

QSize ActivityHeatmap::sizeHint() const
{
    const qreal step = m_theme.px(16);
    return {int(std::max(1, columns()) * step), int(7 * step)};
}

int ActivityHeatmap::heightForWidth(int w) const
{
    const int n = std::max(1, columns());
    const qreal c = std::max<qreal>(m_theme.px(5), std::min<qreal>(m_theme.px(13), qreal(w) / n - m_theme.px(3)));
    return int(7 * (c + m_theme.px(3)));
}

QRectF ActivityHeatmap::cellRect(int index) const
{
    const int pos = m_first.dayOfWeek() - 1 + index;
    const qreal step = cell() + m_theme.px(3);
    return {(pos / 7) * step, (pos % 7) * step, cell(), cell()};
}

bool ActivityHeatmap::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        for (int i = 0; i < m_levels.size(); ++i) {
            if (cellRect(i).contains(help->pos()) && i < m_tooltips.size()) {
                QToolTip::showText(help->globalPos(), m_tooltips.at(i), this);
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void ActivityHeatmap::paintEvent(QPaintEvent*)
{
    const Palette& p = m_theme.palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QColor empty = p.dark ? p.surface2.lighter(125) : p.sunken;
    for (int i = 0; i < m_levels.size(); ++i) {
        const int level = std::clamp(m_levels[i], 0, 4);
        QColor c = empty;
        if (level > 0) {
            c = p.sage;
            c.setAlphaF(0.3 + 0.175 * level);
        }
        painter.setBrush(c);
        painter.drawRoundedRect(cellRect(i), 3, 3);
    }
}

}  // namespace studylab::ui
