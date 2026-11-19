#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace studylab::ui {

/// Icônes au trait (ressources « :/icons/<nom>.svg »), recolorées selon le thème.
class Icons {
public:
    /// Icône d'une couleur donnée, rendue nettement à 1× et 2×.
    static QIcon get(const QString& name, const QColor& color, int size = 18);
    /// Icône dont l'état « coché » ou « actif » utilise une seconde couleur.
    static QIcon stateful(const QString& name, const QColor& normal, const QColor& active, int size = 18);
    static bool exists(const QString& name);
};

}  // namespace studylab::ui
