#pragma once

#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>

namespace studylab::db {
class Database;
}

namespace studylab {

enum class ThemeMode { System, Light, Dark };

/// Préférences utilisateur typées, stockées en JSON dans la table « settings ».
///
/// Les valeurs sont mises en cache ; chaque modification est écrite immédiatement.
class SettingsService : public QObject {
    Q_OBJECT
public:
    explicit SettingsService(db::Database& db, QObject* parent = nullptr);

    QJsonValue value(const QString& key, const QJsonValue& defaultValue = {}) const;
    /// Retourne false si l'écriture en base a échoué (la valeur en mémoire n'est alors pas modifiée).
    bool setValue(const QString& key, const QJsonValue& value);

    // Préférences connues
    ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    /// Échelle de l'interface, de 0.9 à 1.5 (1.0 par défaut).
    double uiScale() const;
    void setUiScale(double scale);

    bool animationsEnabled() const;
    void setAnimationsEnabled(bool enabled);

    static QString themeModeToString(ThemeMode mode);
    static ThemeMode themeModeFromString(const QString& value);

signals:
    void changed(const QString& key);

private:
    void load();

    db::Database& m_db;
    QHash<QString, QJsonValue> m_cache;
};

namespace SettingKeys {
inline const QString Theme = QStringLiteral("appearance/theme");
inline const QString UiScale = QStringLiteral("appearance/uiScale");
inline const QString Animations = QStringLiteral("appearance/animations");
}  // namespace SettingKeys

}  // namespace studylab
