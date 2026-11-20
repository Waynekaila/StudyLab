#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

#include <functional>

class QShortcut;
class QWidget;

namespace studylab {
class SettingsService;
}

namespace studylab::ui {

/// Raccourcis clavier configurables (V2-6).
///
/// Chaque action a un identifiant stable, un groupe (où elle agit) et une touche par défaut. La touche choisie est
/// enregistrée dans la préférence `shortcuts/<id>` (texte portable, chaîne vide : aucun raccourci) ; sans
/// préférence, la touche par défaut s'applique. Les raccourcis liés par `bind` suivent les changements sans
/// redémarrage. Échap, Entrée, Suppr, Ctrl+Z et les commandes d'édition (copier, coller…) restent fixes.
namespace Shortcuts {

enum class Group { General, Reader, Notes, Revisions };

struct Action {
    QString id;
    Group group;
    QString label;
    QKeySequence defaultKey;
};

/// Toutes les actions configurables, dans l'ordre d'affichage.
const QList<Action>& actions();
const Action* find(const QString& id);
QString groupLabel(Group group);
QString settingKey(const QString& id);

/// Touche en vigueur (préférence ou défaut ; vide si l'action n'a pas de raccourci).
QKeySequence key(const SettingsService& settings, const QString& id);
bool isCustomized(const SettingsService& settings, const QString& id);

/// Motif du refus d'une touche pour une action (vide si elle est acceptée) : touche seule hors des révisions,
/// touche réservée, ou déjà prise par une action qui peut agir au même endroit (`conflictId` la désigne).
QString validate(const SettingsService& settings, const QString& id, const QKeySequence& sequence,
                 QString* conflictId = nullptr);
/// Enregistre une touche (vide : aucun raccourci). Refusée si `validate` renvoie un motif.
bool setKey(SettingsService& settings, const QString& id, const QKeySequence& sequence);
void reset(SettingsService& settings, const QString& id);
void resetAll(SettingsService& settings);

/// Texte de la touche pour l'interface (« Ctrl+K »), vide si aucune.
QString keyText(const SettingsService& settings, const QString& id);
/// « libellé (Ctrl+K) », ou le libellé seul si l'action n'a pas de raccourci.
QString withKey(const SettingsService& settings, const QString& label, const QString& id);

/// Crée un QShortcut sur `parent` pour l'action, mis à jour quand la préférence change.
QShortcut* create(SettingsService& settings, const QString& id, QWidget* parent);
/// Appelle `update` tout de suite puis à chaque changement d'un raccourci (infobulles, aides affichées).
/// La connexion vit aussi longtemps que `context`.
void onChanged(SettingsService& settings, QObject* context, const std::function<void()>& update);

}  // namespace Shortcuts

}  // namespace studylab::ui
