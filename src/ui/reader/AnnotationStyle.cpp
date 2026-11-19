#include "ui/reader/AnnotationStyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QPainter>
#include <QPixmap>

namespace studylab::ui {

QColor annotationFill(const Palette& pal, AnnotationColor c)
{
    switch (c) {
    case AnnotationColor::Yellow: return pal.hlYellow;
    case AnnotationColor::Green: return pal.hlGreen;
    case AnnotationColor::Blue: return pal.hlBlue;
    case AnnotationColor::Pink: return pal.hlPink;
    case AnnotationColor::Purple: return pal.hlPurple;
    }
    return pal.hlYellow;
}

QColor annotationAccent(const Palette& pal, AnnotationColor c)
{
    switch (c) {
    case AnnotationColor::Yellow: return pal.accYellow;
    case AnnotationColor::Green: return pal.accGreen;
    case AnnotationColor::Blue: return pal.accBlue;
    case AnnotationColor::Pink: return pal.accPink;
    case AnnotationColor::Purple: return pal.accPurple;
    }
    return pal.accYellow;
}

QColor annotationPageFill(AnnotationColor c)
{
    static const Palette light = Palette::light();
    return annotationFill(light, c);
}

QColor annotationPageStroke(AnnotationColor c)
{
    static const Palette light = Palette::light();
    return annotationAccent(light, c);
}

QIcon colorSwatchIcon(const QColor& color, const QColor& ring, int size)
{
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0}) {
        for (const bool on : {false, true}) {
            QPixmap pm(QSize(size, size) * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r(0, 0, size, size);
            if (on) {
                p.setPen(QPen(ring, 2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(r.adjusted(1, 1, -1, -1));
            }
            p.setPen(QPen(color.darker(115), 1));
            p.setBrush(color);
            p.drawEllipse(r.adjusted(4, 4, -4, -4));
            p.end();
            icon.addPixmap(pm, QIcon::Normal, on ? QIcon::On : QIcon::Off);
        }
    }
    return icon;
}

QString relativeTime(const QString& isoUtc)
{
    const QDateTime t = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!t.isValid())
        return {};
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 secs = t.secsTo(now);
    const auto tr = [](const char* s) { return QCoreApplication::translate("RelativeTime", s); };
    if (secs < 60)
        return tr("à l'instant");
    if (secs < 3600)
        return tr("il y a %1 min").arg(secs / 60);
    const QDate day = t.toLocalTime().date();
    const QDate today = now.toLocalTime().date();
    if (day == today)
        return tr("il y a %1 h").arg(secs / 3600);
    if (day.daysTo(today) == 1)
        return tr("hier");
    return QLocale(QLocale::French).toString(day, day.year() == today.year() ? QStringLiteral("d MMM")
                                                                             : QStringLiteral("d MMM yyyy"));
}

}  // namespace studylab::ui
