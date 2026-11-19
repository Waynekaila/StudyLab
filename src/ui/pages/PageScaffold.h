#pragma once

#include <QScrollArea>

class QVBoxLayout;

namespace studylab::ui {

/// Zone de page défilante aux marges généreuses ; le contenu s'ajoute dans body().
class PageScaffold : public QScrollArea {
    Q_OBJECT
public:
    explicit PageScaffold(QWidget* parent = nullptr, int maxContentWidth = 1100);
    QVBoxLayout* body() const { return m_body; }

private:
    QVBoxLayout* m_body;
};

}  // namespace studylab::ui
