#pragma once

#include "core/domain/Annotation.h"
#include "ui/theme/Tokens.h"

#include <QColor>
#include <QIcon>

namespace studylab::ui {

/// Couleurs d'une annotation. Sur la page (toujours blanche), on utilise les teintes claires même en
/// thème sombre ; dans l'interface (volet, pastilles), celles du thème courant.
QColor annotationFill(const Palette& pal, AnnotationColor c);
QColor annotationAccent(const Palette& pal, AnnotationColor c);
QColor annotationPageFill(AnnotationColor c);
QColor annotationPageStroke(AnnotationColor c);

/// Pastille ronde de couleur ; l'état « coché » ajoute un anneau.
QIcon colorSwatchIcon(const QColor& color, const QColor& ring, int size = 18);

/// « à l'instant », « il y a 5 min », « il y a 3 h », « hier », « 12 nov. 2026 ».
QString relativeTime(const QString& isoUtc);

}  // namespace studylab::ui
