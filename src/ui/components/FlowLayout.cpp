#include "ui/components/FlowLayout.h"

#include <QWidget>

namespace studylab::ui {

FlowLayout::FlowLayout(QWidget* parent, int spacing) : QLayout(parent), m_spacing(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item) { m_items.append(item); }
int FlowLayout::count() const { return int(m_items.size()); }
QLayoutItem* FlowLayout::itemAt(int index) const { return m_items.value(index); }
QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem* item : m_items) {
        // isEmpty() : élément masqué explicitement. (isVisible() serait faux tant que la fenêtre n'est pas
        // affichée : les éléments n'auraient alors jamais reçu de position.)
        if (item->isEmpty())
            continue;
        const QSize hint = item->sizeHint();
        if (x + hint.width() > area.right() + 1 && lineHeight > 0) {
            x = area.x();
            y += lineHeight + m_spacing;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + m_spacing;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

}  // namespace studylab::ui
