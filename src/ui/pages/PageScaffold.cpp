#include "ui/pages/PageScaffold.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace studylab::ui {

PageScaffold::PageScaffold(QWidget* parent, int maxContentWidth) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("PageScroll"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* outer = new QWidget(this);
    outer->setObjectName(QStringLiteral("PageBody"));
    auto* h = new QHBoxLayout(outer);
    h->setContentsMargins(32, 24, 32, 32);

    auto* content = new QWidget(outer);
    content->setMaximumWidth(maxContentWidth);
    m_body = new QVBoxLayout(content);
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(18);

    h->addWidget(content, 1);
    h->addStretch(0);
    setWidget(outer);
}

}  // namespace studylab::ui
