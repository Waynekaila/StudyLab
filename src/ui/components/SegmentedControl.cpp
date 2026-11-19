#include "ui/components/SegmentedControl.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

namespace studylab::ui {

SegmentedControl::SegmentedControl(const QStringList& labels, QWidget* parent)
    : QFrame(parent), m_group(new QButtonGroup(this))
{
    setProperty("segmented", true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new QPushButton(labels[i], this);
        b->setProperty("segment", true);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        m_group->addButton(b, i);
        layout->addWidget(b);
    }
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, &SegmentedControl::currentIndexChanged);
}

int SegmentedControl::currentIndex() const { return m_group->checkedId(); }

void SegmentedControl::setCurrentIndex(int index)
{
    const QSignalBlocker blocker(m_group);
    if (auto* b = m_group->button(index))
        b->setChecked(true);
}

}  // namespace studylab::ui
