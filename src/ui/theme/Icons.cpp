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

QIcon Icons::get(const QString& name, const QColor& color, int size)
{
    QIcon icon;
    for (qreal dpr : {1.0, 2.0})
        icon.addPixmap(render(name, color, size, dpr), QIcon::Normal, QIcon::Off);
    return icon;
}

QIcon Icons::stateful(const QString& name, const QColor& normal, const QColor& active, int size)
{
    QIcon icon;
    for (qreal dpr : {1.0, 2.0}) {
        icon.addPixmap(render(name, normal, size, dpr), QIcon::Normal, QIcon::Off);
        icon.addPixmap(render(name, active, size, dpr), QIcon::Normal, QIcon::On);
        icon.addPixmap(render(name, active, size, dpr), QIcon::Active, QIcon::On);
    }
    return icon;
}

bool Icons::exists(const QString& name) { return !svgSource(name).isEmpty(); }

}  // namespace studylab::ui
