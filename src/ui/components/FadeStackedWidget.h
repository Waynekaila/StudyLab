#pragma once

#include <QStackedWidget>

namespace studylab::ui {

/// QStackedWidget avec un fondu enchaîné court lors du changement de page (désactivable).
class FadeStackedWidget : public QStackedWidget {
    Q_OBJECT
public:
    explicit FadeStackedWidget(QWidget* parent = nullptr);

    void setAnimationsEnabled(bool enabled) { m_animations = enabled; }
    void fadeTo(int index);

private:
    bool m_animations = true;
};

}  // namespace studylab::ui
