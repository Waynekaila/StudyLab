#include "ui/components/Widgets.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace studylab::ui {

QLabel* makeLabel(const QString& text, const char* role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    if (role)
        label->setProperty("role", QString::fromLatin1(role));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    return label;
}

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);
    return card;
}

QFrame* makeSeparator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setProperty("separator", true);
    line->setFrameShape(QFrame::NoFrame);
    return line;
}

QWidget* makeSettingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(16);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto* t = makeLabel(title, "h3", row);
    t->setTextInteractionFlags(Qt::NoTextInteraction);
    texts->addWidget(t);
    if (!description.isEmpty()) {
        auto* d = makeLabel(description, "faint", row);
        d->setWordWrap(true);
        d->setTextInteractionFlags(Qt::NoTextInteraction);
        texts->addWidget(d);
    }
    h->addLayout(texts, 1);
    h->addWidget(control, 0, Qt::AlignVCenter);
    t->setBuddy(control);
    return row;
}

}  // namespace studylab::ui
