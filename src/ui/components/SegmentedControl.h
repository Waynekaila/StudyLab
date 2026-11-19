#pragma once

#include <QFrame>
#include <QStringList>

class QButtonGroup;

namespace studylab::ui {

/// Choix exclusif entre quelques options (ex. Système / Clair / Sombre).
class SegmentedControl : public QFrame {
    Q_OBJECT
public:
    explicit SegmentedControl(const QStringList& labels, QWidget* parent = nullptr);

    int currentIndex() const;
    void setCurrentIndex(int index);  ///< N'émet pas currentIndexChanged.

signals:
    void currentIndexChanged(int index);

private:
    QButtonGroup* m_group;
};

}  // namespace studylab::ui
