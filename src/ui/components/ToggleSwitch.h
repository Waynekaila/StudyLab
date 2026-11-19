#pragma once

#include <QAbstractButton>

class QVariantAnimation;

namespace studylab::ui {

/// Interrupteur marche/arrêt animé. Couleurs : QPalette::Highlight (actif) et QPalette::Mid (inactif).
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void setAnimated(bool animated) { m_animated = animated; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;

private:
    void syncPosition(bool animate);

    QVariantAnimation* m_anim = nullptr;
    qreal m_pos = 0.0;  // 0 = inactif, 1 = actif
    bool m_animated = true;
};

}  // namespace studylab::ui
