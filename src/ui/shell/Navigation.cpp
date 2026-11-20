#include "ui/shell/Navigation.h"

#include "services/settings/SettingsService.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace studylab::ui::Navigation {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Navigation", s); }
}  // namespace

const QList<Entry>& entries()
{
    // Seuls les espaces terminés apparaissent dans la navigation.
    static const QList<Entry> list = {
        {QStringLiteral("home"), QStringLiteral("home"), tr("Accueil")},
        {QStringLiteral("library"), QStringLiteral("library"), tr("Bibliothèque")},
        {QStringLiteral("reader"), QStringLiteral("book"), tr("Lecture")},
        {QStringLiteral("notes"), QStringLiteral("note"), tr("Mes notes")},
        {QStringLiteral("subjects"), QStringLiteral("folder"), tr("Matières")},
        {QStringLiteral("collections"), QStringLiteral("layers"), tr("Collections")},
        {QStringLiteral("graph"), QStringLiteral("graph"), tr("Graphe")},
        {QStringLiteral("revisions"), QStringLiteral("cards"), tr("Révisions")},
        {QStringLiteral("activity"), QStringLiteral("activity"), tr("Activité")},
        {QStringLiteral("references"), QStringLiteral("quote"), tr("Références")},
        {QStringLiteral("settings"), QStringLiteral("settings"), tr("Paramètres")},
    };
    return list;
}

const Entry* find(const QString& id)
{
    for (const Entry& e : entries())
        if (e.id == id)
            return &e;
    return nullptr;
}

bool canHide(const QString& id) { return id != QLatin1String("settings"); }

Layout layout(const SettingsService& settings)
{
    const QJsonObject stored = settings.value(kSettingKey).toObject();
    Layout out;
    for (const QJsonValue& v : stored.value(QStringLiteral("order")).toArray()) {
        const QString id = v.toString();
        if (find(id) && !out.order.contains(id))
            out.order << id;
    }
    // Entrées absentes de l'ordre enregistré : à la suite de celle qui les précède par défaut.
    const QList<Entry>& all = entries();
    for (int i = 0; i < all.size(); ++i) {
        if (out.order.contains(all[i].id))
            continue;
        int at = 0;
        for (int k = i - 1; k >= 0; --k)
            if (const int pos = int(out.order.indexOf(all[k].id)); pos >= 0) {
                at = pos + 1;
                break;
            }
        out.order.insert(at, all[i].id);
    }
    for (const QJsonValue& v : stored.value(QStringLiteral("hidden")).toArray()) {
        const QString id = v.toString();
        if (find(id) && canHide(id))
            out.hidden.insert(id);
    }
    return out;
}

void setLayout(SettingsService& settings, const Layout& layout)
{
    QJsonArray order;
    for (const QString& id : layout.order)
        if (find(id))
            order.append(id);
    QJsonArray hidden;
    for (const QString& id : layout.order)  // ordre stable dans la préférence
        if (layout.hidden.contains(id) && canHide(id))
            hidden.append(id);
    settings.setValue(kSettingKey, QJsonObject{{QStringLiteral("order"), order}, {QStringLiteral("hidden"), hidden}});
}

void reset(SettingsService& settings) { settings.setValue(kSettingKey, QJsonObject{}); }

bool isDefault(const SettingsService& settings)
{
    const Layout l = layout(settings);
    if (!l.hidden.isEmpty())
        return false;
    for (int i = 0; i < entries().size(); ++i)
        if (l.order.value(i) != entries().at(i).id)
            return false;
    return true;
}

QList<Entry> visibleEntries(const SettingsService& settings)
{
    const Layout l = layout(settings);
    QList<Entry> out;
    for (const QString& id : l.order)
        if (!l.hidden.contains(id))
            out << *find(id);
    return out;
}

}  // namespace studylab::ui::Navigation
