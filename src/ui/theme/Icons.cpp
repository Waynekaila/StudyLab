#include "ui/theme/Icons.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace studylab::ui {

namespace {
QByteArray svgSource(const QString& name)
{
    static QHash<QString, QByteArray> cache;
    auto it = cache.constFind(name);
    if (it != cache.cend())
        return *it;
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    QByteArray data;
    if (f.open(QIODevice::ReadOnly))
        data = f.readAll();
    cache.insert(name, data);
    return data;
}

QPixmap render(const QString& name, const QColor& color, int size, qreal dpr)
{
    QByteArray svg = svgSource(name);
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(svg);
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p);
    }
    pm.setDevicePixelRatio(dpr);
    return pm;
}
}  // namespace

namespace {
/// Icônes déjà dessinées (nom, couleurs, taille) : une liste de 3 000 notes demandait 3 000 rendus SVG identiques.
/// Le nombre de combinaisons reste petit (icônes × couleurs du thème × tailles) ; QIcon se partage sans copie.
QHash<QString, QIcon>& iconCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}
}  // namespace

QIcon Icons::get(const QString& name, const QColor& color, int size)
{
    const QString key = QStringLiteral("%1|%2|%3").arg(name, color.name(QColor::HexArgb)).arg(size);
    if (const auto it = iconCache().constFind(key); it != iconCache().cend())
        return *it;
    QIcon icon;
    for (qreal dpr : {1.0, 2.0})
        icon.addPixmap(render(name, color, size, dpr), QIcon::Normal, QIcon::Off);
    iconCache().insert(key, icon);
    return icon;
}

QIcon Icons::stateful(const QString& name, const QColor& normal, const QColor& active, int size)
{
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(name, normal.name(QColor::HexArgb), active.name(QColor::HexArgb)).arg(size);
    if (const auto it = iconCache().constFind(key); it != iconCache().cend())
        return *it;
    QIcon icon;
    for (qreal dpr : {1.0, 2.0}) {
        icon.addPixmap(render(name, normal, size, dpr), QIcon::Normal, QIcon::Off);
        icon.addPixmap(render(name, active, size, dpr), QIcon::Normal, QIcon::On);
        icon.addPixmap(render(name, active, size, dpr), QIcon::Active, QIcon::On);
    }
    iconCache().insert(key, icon);
    return icon;
}

bool Icons::exists(const QString& name) { return !svgSource(name).isEmpty(); }

}  // namespace studylab::ui
