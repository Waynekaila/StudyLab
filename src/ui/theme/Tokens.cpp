#include "ui/theme/Tokens.h"

#include <QtMath>

#include <algorithm>

namespace studylab::ui {

namespace {
QColor c(const char* hex) { return QColor(QLatin1String(hex)); }

double channel(double v)
{
    return v <= 0.04045 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& col)
{
    return 0.2126 * channel(col.redF()) + 0.7152 * channel(col.greenF()) + 0.0722 * channel(col.blueF());
}
}  // namespace

Palette Palette::light()
{
    Palette p;
    p.dark = false;
    p.bg = c("#F6F5F2");
    p.surface = c("#FFFFFF");
    p.surface2 = c("#FBFAF8");
    p.sunken = c("#F1EFEB");
    p.border = c("#E6E3DD");
    p.borderSoft = c("#EEECE7");
    p.text = c("#2A2F36");
    p.text2 = c("#5F6672");
    p.text3 = c("#8A919B");
    p.primary = c("#5B7DB1");
    p.primaryStrong = c("#456594");
    p.primarySoft = c("#E9EEF6");
    p.onPrimary = c("#FFFFFF");
    p.lavender = c("#9C8FD0");
    p.lavenderSoft = c("#F2EFFA");
    p.sage = c("#7FA88A");
    p.sageSoft = c("#EAF2EC");
    p.amber = c("#C9A24A");
    p.danger = c("#B5566F");
    p.hlYellow = c("#F6E7A1");
    p.hlGreen = c("#D3EACF");
    p.hlBlue = c("#D3E3F6");
    p.hlPink = c("#F6D5DF");
    p.hlPurple = c("#E4D9F4");
    p.accYellow = c("#DDBE4E");
    p.accGreen = c("#79B472");
    p.accBlue = c("#6E9BD6");
    p.accPink = c("#D98AA6");
    p.accPurple = c("#A88BD8");
    return p;
}

Palette Palette::darkPalette()
{
    Palette p;
    p.dark = true;
    p.bg = c("#1D2026");
    p.surface = c("#252930");
    p.surface2 = c("#2A2F37");
    p.sunken = c("#21252B");
    p.border = c("#363C46");
    p.borderSoft = c("#30353E");
    p.text = c("#E3E6EA");
    p.text2 = c("#A0A8B3");
    p.text3 = c("#7C8590");
    p.primary = c("#8AA6D1");
    p.primaryStrong = c("#A3BADD");
    p.primarySoft = c("#2E3747");
    p.onPrimary = c("#1D2026");
    p.lavender = c("#B4A9DF");
    p.lavenderSoft = c("#312E42");
    p.sage = c("#9BBFA4");
    p.sageSoft = c("#2B3830");
    p.amber = c("#D9BD72");
    p.danger = c("#E08A9F");
    p.hlYellow = c("#4D4630");
    p.hlGreen = c("#3A4E3A");
    p.hlBlue = c("#34465F");
    p.hlPink = c("#5A3B48");
    p.hlPurple = c("#463C5C");
    p.accYellow = c("#DDBE4E");
    p.accGreen = c("#8CC285");
    p.accBlue = c("#8AB0E0");
    p.accPink = c("#E09AB3");
    p.accPurple = c("#B59CE0");
    return p;
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace studylab::ui
