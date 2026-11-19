#pragma once

#include <QStyledItemDelegate>

namespace studylab::ui {

class ThemeManager;

/// Dessin d'un document : carte de grille (couverture, titre, auteur, statut, progression) ou ligne de liste.
class LibraryDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum class Mode { Grid, List };

    LibraryDelegate(ThemeManager& theme, QObject* parent = nullptr);

    void setMode(Mode mode) { m_mode = mode; }
    Mode mode() const { return m_mode; }
    /// Taille d'une carte de grille (pour QListView::setGridSize).
    QSize gridCellSize() const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    void paintGrid(QPainter& p, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintList(QPainter& p, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    ThemeManager& m_theme;
    Mode m_mode = Mode::Grid;
};

}  // namespace studylab::ui
