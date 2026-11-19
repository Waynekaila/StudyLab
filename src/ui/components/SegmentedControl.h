#pragma once

#include <QFrame>
#include <QIcon>
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
    /// Remplace le libellé d'un segment par une icône (le libellé devient l'infobulle et le nom accessible).
    void setSegmentIcon(int index, const QIcon& icon);
    void setSegmentText(int index, const QString& text);

signals:
    void currentIndexChanged(int index);

private:
    QButtonGroup* m_group;
};

}  // namespace studylab::ui
