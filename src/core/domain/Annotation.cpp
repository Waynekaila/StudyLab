#include "core/domain/Annotation.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace studylab {

namespace {

struct TypeEntry {
    AnnotationType type;
    const char* key;
    const char* label;
};
constexpr TypeEntry kTypes[] = {
    {AnnotationType::Highlight, "highlight", QT_TRANSLATE_NOOP("Annotation", "Surlignage")},
    {AnnotationType::Underline, "underline", QT_TRANSLATE_NOOP("Annotation", "Soulignement")},
    {AnnotationType::Strikeout, "strikeout", QT_TRANSLATE_NOOP("Annotation", "Barré")},
    {AnnotationType::Note, "note", QT_TRANSLATE_NOOP("Annotation", "Note")},
    {AnnotationType::Marker, "marker", QT_TRANSLATE_NOOP("Annotation", "Zone")},
    {AnnotationType::Bookmark, "bookmark", QT_TRANSLATE_NOOP("Annotation", "Signet")},
};

struct ColorEntry {
    AnnotationColor color;
    const char* key;
    const char* label;
};
constexpr ColorEntry kColors[] = {
    {AnnotationColor::Yellow, "yellow", QT_TRANSLATE_NOOP("Annotation", "Jaune")},
    {AnnotationColor::Green, "green", QT_TRANSLATE_NOOP("Annotation", "Vert")},
    {AnnotationColor::Blue, "blue", QT_TRANSLATE_NOOP("Annotation", "Bleu")},
    {AnnotationColor::Pink, "pink", QT_TRANSLATE_NOOP("Annotation", "Rose")},
    {AnnotationColor::Purple, "purple", QT_TRANSLATE_NOOP("Annotation", "Violet")},
};

double round4(double v)
{
    return std::round(v * 10000.0) / 10000.0;
}

}  // namespace

QString annotationTypeKey(AnnotationType t)
{
    for (const auto& e : kTypes)
        if (e.type == t)
            return QString::fromLatin1(e.key);
    return QStringLiteral("highlight");
}

std::optional<AnnotationType> annotationTypeFromKey(const QString& key)
{
    for (const auto& e : kTypes)
        if (key == QLatin1String(e.key))
            return e.type;
    return std::nullopt;
}

QString annotationTypeLabel(AnnotationType t)
{
    for (const auto& e : kTypes)
        if (e.type == t)
            return QCoreApplication::translate("Annotation", e.label);
    return {};
}

QList<AnnotationType> allAnnotationTypes()
{
    QList<AnnotationType> out;
    for (const auto& e : kTypes)
        out << e.type;
    return out;
}

QString annotationColorKey(AnnotationColor c)
{
    for (const auto& e : kColors)
        if (e.color == c)
            return QString::fromLatin1(e.key);
    return QStringLiteral("yellow");
}

std::optional<AnnotationColor> annotationColorFromKey(const QString& key)
{
    for (const auto& e : kColors)
        if (key == QLatin1String(e.key))
            return e.color;
    return std::nullopt;
}

QString annotationColorLabel(AnnotationColor c)
{
    for (const auto& e : kColors)
        if (e.color == c)
            return QCoreApplication::translate("Annotation", e.label);
    return {};
}

QList<AnnotationColor> allAnnotationColors()
{
    QList<AnnotationColor> out;
    for (const auto& e : kColors)
        out << e.color;
    return out;
}

QJsonObject PdfLocator::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QStringLiteral("pdf"));
    o.insert(QStringLiteral("page"), page);
    QJsonArray arr;
    for (const QRectF& r : rects)
        arr.append(QJsonArray{round4(r.left()), round4(r.top()), round4(r.right()), round4(r.bottom())});
    o.insert(QStringLiteral("rects"), arr);
    if (!exact.isEmpty()) {
        o.insert(QStringLiteral("textQuote"), QJsonObject{{QStringLiteral("exact"), exact},
                                                          {QStringLiteral("prefix"), prefix},
                                                          {QStringLiteral("suffix"), suffix}});
    }
    if (start >= 0 && end >= start)
        o.insert(QStringLiteral("textPosition"),
                 QJsonObject{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}});
    return o;
}

std::optional<PdfLocator> PdfLocator::fromJson(const QJsonObject& o)
{
    if (o.value(QStringLiteral("kind")).toString() != QLatin1String("pdf") || !o.value(QStringLiteral("page")).isDouble())
        return std::nullopt;
    PdfLocator l;
    l.page = o.value(QStringLiteral("page")).toInt();
    for (const QJsonValue& v : o.value(QStringLiteral("rects")).toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() != 4)
            continue;
        l.rects << QRectF(QPointF(a[0].toDouble(), a[1].toDouble()), QPointF(a[2].toDouble(), a[3].toDouble())).normalized();
    }
    const QJsonObject q = o.value(QStringLiteral("textQuote")).toObject();
    l.exact = q.value(QStringLiteral("exact")).toString();
    l.prefix = q.value(QStringLiteral("prefix")).toString();
    l.suffix = q.value(QStringLiteral("suffix")).toString();
    const QJsonObject p = o.value(QStringLiteral("textPosition")).toObject();
    l.start = p.value(QStringLiteral("start")).toInt(-1);
    l.end = p.value(QStringLiteral("end")).toInt(-1);
    return l;
}

QRectF PdfLocator::bounds() const
{
    QRectF b;
    for (const QRectF& r : rects)
        b = b.isNull() ? r : b.united(r);
    return b;
}

double PdfLocator::relativeY() const
{
    return rects.isEmpty() ? 0.0 : std::clamp(bounds().top(), 0.0, 0.999);
}

QJsonObject EpubLocator::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QStringLiteral("epub"));
    o.insert(QStringLiteral("cfi"), cfi);
    o.insert(QStringLiteral("section"), section);
    o.insert(QStringLiteral("fraction"), round4(fraction));
    o.insert(QStringLiteral("progress"), round4(progress));
    if (!exact.isEmpty()) {
        o.insert(QStringLiteral("textQuote"), QJsonObject{{QStringLiteral("exact"), exact},
                                                          {QStringLiteral("prefix"), prefix},
                                                          {QStringLiteral("suffix"), suffix}});
    }
    return o;
}

std::optional<EpubLocator> EpubLocator::fromJson(const QJsonObject& o)
{
    if (o.value(QStringLiteral("kind")).toString() != QLatin1String("epub")
        || !o.value(QStringLiteral("cfi")).toString().startsWith(QLatin1String("epubcfi(")))
        return std::nullopt;
    EpubLocator l;
    l.cfi = o.value(QStringLiteral("cfi")).toString();
    l.section = o.value(QStringLiteral("section")).toInt();
    l.fraction = o.value(QStringLiteral("fraction")).toDouble();
    l.progress = o.value(QStringLiteral("progress")).toDouble();
    const QJsonObject q = o.value(QStringLiteral("textQuote")).toObject();
    l.exact = q.value(QStringLiteral("exact")).toString();
    l.prefix = q.value(QStringLiteral("prefix")).toString();
    l.suffix = q.value(QStringLiteral("suffix")).toString();
    return l;
}

QString annotationLocation(const AnnotationRecord& r)
{
    if (const auto epub = EpubLocator::fromJson(r.locator))
        return QStringLiteral("%1 %").arg(qRound(epub->progress * 100));
    return QStringLiteral("p. %1").arg(r.pageLabel.isEmpty() ? QString::number(r.unitIndex + 1) : r.pageLabel);
}

QString annotationGroupFallback(const AnnotationRecord& r)
{
    if (EpubLocator::fromJson(r.locator))
        return QCoreApplication::translate("Annotation", "Section %1").arg(r.unitIndex + 1);
    return QCoreApplication::translate("Annotation", "Page %1")
        .arg(r.pageLabel.isEmpty() ? QString::number(r.unitIndex + 1) : r.pageLabel);
}

double annotationSortKey(int unitIndex, double relativeY)
{
    return unitIndex + std::clamp(relativeY, 0.0, 0.999);
}

QString normalizeQuote(const QString& text)
{
    QString t = text;
    static const QRegularExpression hyphenBreak(QStringLiteral("(\\w)-\\s*\\n\\s*(\\w)"),
                                                QRegularExpression::UseUnicodePropertiesOption);
    t.replace(hyphenBreak, QStringLiteral("\\1\\2"));
    return t.simplified();
}

}  // namespace studylab
