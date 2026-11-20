#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace studylab {
class SettingsService;
}

namespace studylab::ui {

/// Entrées de la navigation latérale et leur disposition choisie par l'utilisateur (V2-6).
///
/// La disposition est enregistrée dans la préférence `ui/navigation` : `{"order": [ids], "hidden": [ids]}`. Un
/// identifiant inconnu est ignoré ; une entrée absente de l'ordre enregistré (ajoutée par une version ultérieure)
/// reprend sa place par défaut. Les Paramètres ne peuvent pas être masqués : c'est par eux qu'on rétablit le reste.
namespace Navigation {

struct Entry {
    QString id;
    QString icon;
    QString label;
};

inline const QString kSettingKey = QStringLiteral("ui/navigation");

/// Toutes les entrées, dans l'ordre par défaut.
const QList<Entry>& entries();
const Entry* find(const QString& id);
bool canHide(const QString& id);

struct Layout {
    QStringList order;   ///< toutes les entrées connues, dans l'ordre choisi
    QSet<QString> hidden;
};

Layout layout(const SettingsService& settings);
void setLayout(SettingsService& settings, const Layout& layout);
void reset(SettingsService& settings);
bool isDefault(const SettingsService& settings);
/// Entrées affichées, dans l'ordre choisi.
QList<Entry> visibleEntries(const SettingsService& settings);

}  // namespace Navigation

}  // namespace studylab::ui
