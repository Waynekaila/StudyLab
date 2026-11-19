#include "ui/components/FadeStackedWidget.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>

namespace studylab::ui {

FadeStackedWidget::FadeStackedWidget(QWidget* parent) : QStackedWidget(parent) {}

void FadeStackedWidget::fadeTo(int index)
{
    if (index == currentIndex() || index < 0 || index >= count())
        return;
    if (!m_animations || !isVisible() || !currentWidget()) {
        setCurrentIndex(index);
        return;
    }

    // Capture de la page sortante, posée au-dessus de la nouvelle puis estompée.
    auto* overlay = new QLabel(this);
    overlay->setPixmap(currentWidget()->grab());
    overlay->setGeometry(currentWidget()->geometry());
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);

    setCurrentIndex(index);
    overlay->show();
    overlay->raise();

    auto* effect = new QGraphicsOpacityEffect(overlay);
    overlay->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", overlay);
    anim->setDuration(140);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, overlay, &QObject::deleteLater);
    anim->start();
}

}  // namespace studylab::ui
