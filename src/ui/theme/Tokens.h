#pragma once

#include <QColor>

namespace studylab::ui {

/// Jetons de couleur de StudyLab (voir docs/design/01-vision.md §1.7).
struct Palette {
    bool dark = false;

    QColor bg, surface, surface2, sunken, border, borderSoft;
    QColor text, text2, text3;
    QColor primary, primaryStrong, primarySoft, onPrimary;
    QColor lavender, lavenderSoft, sage, sageSoft, amber;
    QColor danger;

    // Annotations : fond sur la page et barre d'accent
    QColor hlYellow, hlGreen, hlBlue, hlPink, hlPurple;
    QColor accYellow, accGreen, accBlue, accPink, accPurple;

    static Palette light();
    static Palette darkPalette();
};

/// Thèmes de page du lecteur EPUB (05-maquettes §4) : blanc, sépia, vert d'eau, sombre.
enum class ReadingTheme { White, Sepia, Green, Dark };
struct ReadingColors {
    bool dark = false;
    QColor bg, text, muted, link, selection;
};
ReadingColors readingColors(ReadingTheme theme);

/// Rapport de contraste WCAG 2.x entre deux couleurs opaques (1 à 21).
double contrastRatio(const QColor& a, const QColor& b);

/// Dimensions communes (px logiques, avant mise à l'échelle).
namespace Metrics {
constexpr int RadiusSmall = 6;
constexpr int Radius = 8;
constexpr int RadiusLarge = 12;
constexpr int SidebarWidth = 228;
constexpr int BaseFontPx = 13;
constexpr int AnimationMs = 160;
}  // namespace Metrics

}  // namespace studylab::ui
