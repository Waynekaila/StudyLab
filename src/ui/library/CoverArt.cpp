#include "ui/library/CoverArt.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

namespace studylab::ui::CoverArt {

namespace {

QColor blend(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

// Teintes des couvertures générées : celles de la palette, assourdies pour rester douces.
QColor placeholderColor(const DocumentRecord& r, const Palette& pal)
{
    static const char* const kTones[] = {"#6E8BB8", "#7FA88A", "#8F84C4", "#5E6B7E", "#B98B7A",
                                         "#C9A55A", "#7BA3A8", "#A47C9C", "#4F6E91", "#93A874"};
    const uint h = qHash(r.uuid.isEmpty() ? r.title : r.uuid);
    const QColor c = QColor::fromString(QLatin1String(kTones[h % std::size(kTones)]));
    return pal.dark ? c.darker(125) : c;
}

}  // namespace

void paintCover(QPainter& p, const QRectF& rect, const DocumentRecord& r, const QImage& cover, const Palette& pal,
                double scale)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal radius = 6 * scale;
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    p.setClipPath(clip);

    if (!cover.isNull()) {
        // Remplit le cadre en conservant les proportions (rognage centré).
        const QSizeF target = QSizeF(cover.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF dst(rect.center().x() - target.width() / 2, rect.center().y() - target.height() / 2,
                         target.width(), target.height());
        p.fillRect(rect, Qt::white);
        p.drawImage(dst, cover);
    } else {
        const QColor base = placeholderColor(r, pal);
        p.fillRect(rect, base);
        p.fillRect(QRectF(rect.left(), rect.top(), 5 * scale, rect.height()), base.darker(115));  // dos du livre
        const qreal pad = 12 * scale;
        const QRectF text = rect.adjusted(pad + 3 * scale, pad, -pad, -pad);

        QFont titleFont(QStringLiteral("Literata"));
        titleFont.setStyleHint(QFont::Serif);
        titleFont.setFamilies({QStringLiteral("Literata"), QStringLiteral("Georgia"), QStringLiteral("DejaVu Serif"),
                               QStringLiteral("serif")});
        titleFont.setWeight(QFont::DemiBold);
        titleFont.setPixelSize(qMax(9, int(rect.width() / 11)));
        p.setFont(titleFont);
        p.setPen(QColor(255, 255, 255, 240));
        p.drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, r.title);

        if (!r.authors.isEmpty()) {
            QFont small = p.font();
            small.setFamilies({});
            small.setFamily(QFont().family());
            small.setWeight(QFont::Normal);
            small.setPixelSize(qMax(8, int(rect.width() / 17)));
            p.setFont(small);
            p.setPen(QColor(255, 255, 255, 200));
            const QString author = r.authors.first().section(QLatin1Char(' '), -1);
            p.drawText(text, Qt::AlignLeft | Qt::AlignBottom, QFontMetricsF(small).elidedText(author, Qt::ElideRight, text.width()));
        }
    }
    p.setClipping(false);
    p.setPen(QPen(pal.dark ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 22), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.restore();
}

QPair<QColor, QColor> statusColors(ReadingStatus s, const Palette& pal)
{
    switch (s) {
    case ReadingStatus::Reading:
        return {pal.primarySoft, pal.primaryStrong};
    case ReadingStatus::Paused:
        return {blend(pal.surface, pal.amber, pal.dark ? 0.22 : 0.18), pal.dark ? pal.amber : pal.amber.darker(150)};
    case ReadingStatus::Finished:
        return {pal.sageSoft, pal.dark ? pal.sage : pal.sage.darker(150)};
    case ReadingStatus::ToReread:
        return {pal.lavenderSoft, pal.dark ? pal.lavender : pal.lavender.darker(140)};
    case ReadingStatus::Archived:
        return {pal.sunken, pal.text3};
    case ReadingStatus::ToRead:
        break;
    }
    return {pal.sunken, pal.text2};
}

qreal paintStatusPill(QPainter& p, const QRectF& anchor, ReadingStatus s, const Palette& pal, double scale,
                      Qt::Alignment align)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(int(11.5 * scale));
    f.setWeight(QFont::Medium);
    p.setFont(f);
    const QString label = readingStatusLabel(s);
    const qreal w = QFontMetricsF(f).horizontalAdvance(label) + 14 * scale;
    const qreal h = 20 * scale;
    const qreal x = (align & Qt::AlignLeft) ? anchor.left() : anchor.right() - w;
    const QRectF pill(x, anchor.center().y() - h / 2, w, h);
    const auto [bg, fg] = statusColors(s, pal);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(pill, h / 2, h / 2);
    p.setPen(fg);
    p.drawText(pill, Qt::AlignCenter, label);
    p.restore();
    return w;
}

QString formatFileSize(qint64 bytes)
{
    const QLocale fr(QLocale::French, QLocale::France);
    static const char* const kUnits[] = {"octets", "Ko", "Mo", "Go", "To"};
    double v = double(bytes);
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        ++u;
    }
    return u == 0 ? QStringLiteral("%1 %2").arg(bytes).arg(QLatin1String(kUnits[0]))
                  : QStringLiteral("%1 %2").arg(fr.toString(v, 'f', v < 10 ? 1 : 0), QLatin1String(kUnits[u]));
}

}  // namespace studylab::ui::CoverArt
