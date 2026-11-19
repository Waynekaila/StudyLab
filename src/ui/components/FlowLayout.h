#pragma once

#include <QLayout>
#include <QList>

namespace studylab::ui {

/// Disposition qui place les éléments en ligne et passe à la ligne suivante si nécessaire (tags, puces).
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& rect) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    QList<QLayoutItem*> m_items;
    int m_spacing;
};

}  // namespace studylab::ui
