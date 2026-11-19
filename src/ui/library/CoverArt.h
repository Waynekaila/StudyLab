#pragma once

#include "core/domain/Document.h"
#include "ui/theme/Tokens.h"

#include <QColor>
#include <QImage>
#include <QRect>

class QPainter;

namespace studylab::ui {

/// Dessin des couvertures et des pastilles de statut (grille, liste, panneau de détail).
namespace CoverArt {

/// Couverture réelle si disponible, sinon couverture générée (couleur douce dérivée du titre, titre, auteur).
void paintCover(QPainter& p, const QRectF& rect, const DocumentRecord& r, const QImage& cover, const Palette& pal,
                double scale);

/// Couleurs (fond, texte) de la pastille d'un statut de lecture.
QPair<QColor, QColor> statusColors(ReadingStatus s, const Palette& pal);

/// Dessine une pastille « En cours » alignée à droite de `anchor` ; retourne sa largeur.
qreal paintStatusPill(QPainter& p, const QRectF& anchor, ReadingStatus s, const Palette& pal, double scale,
                      Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter);

QString formatFileSize(qint64 bytes);

}  // namespace CoverArt

}  // namespace studylab::ui
