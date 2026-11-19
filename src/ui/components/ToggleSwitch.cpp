#include "ui/components/ToggleSwitch.h"

#include "ui/theme/Tokens.h"

#include <QPainter>
#include <QVariantAnimation>

namespace studylab::ui {

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent), m_anim(new QVariantAnimation(this))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    m_anim->setDuration(Metrics::AnimationMs);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_pos = v.toReal();
        update();
    });
    connect(this, &QAbstractButton::toggled, this, [this](bool) { syncPosition(m_animated && isVisible()); });
}

QSize ToggleSwitch::sizeHint() const { return {36, 20}; }

void ToggleSwitch::nextCheckState() { setChecked(!isChecked()); }

void ToggleSwitch::syncPosition(bool animate)
{
    const qreal target = isChecked() ? 1.0 : 0.0;
    m_anim->stop();
    if (!animate) {
        m_pos = target;
        update();
        return;
    }
    m_anim->setStartValue(m_pos);
    m_anim->setEndValue(target);
    m_anim->start();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    if (!m_anim->state() && m_pos != (isChecked() ? 1.0 : 0.0))
        m_pos = isChecked() ? 1.0 : 0.0;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal r = track.height() / 2.0;

    const QColor off = palette().color(QPalette::Mid);
    const QColor on = palette().color(QPalette::Highlight);
    QColor fill;
    fill.setRgbF(off.redF() + (on.redF() - off.redF()) * m_pos, off.greenF() + (on.greenF() - off.greenF()) * m_pos,
                 off.blueF() + (on.blueF() - off.blueF()) * m_pos);
    if (!isEnabled())
        fill.setAlphaF(0.5);

    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(track, r, r);

    const qreal knob = track.height() - 4;
    const qreal x = track.left() + 2 + (track.width() - knob - 4) * m_pos;
    p.setBrush(QColor(0, 0, 0, 30));
    p.drawEllipse(QRectF(x, track.top() + 2.6, knob, knob));
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(x, track.top() + 2, knob, knob));

    if (hasFocus()) {
        QPen pen(on, 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-0.5, -0.5, 0.5, 0.5), r, r);
    }
}

}  // namespace studylab::ui
