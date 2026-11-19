#include "services/settings/SettingsService.h"

#include "core/Log.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace studylab {

namespace {
// Une valeur JSON isolée est enveloppée dans un tableau pour rester un document JSON valide.
QString encode(const QJsonValue& v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
}

QJsonValue decode(const QString& text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isArray() || doc.array().isEmpty())
        return {};
    return doc.array().first();
}
}  // namespace

SettingsService::SettingsService(db::Database& db, QObject* parent) : QObject(parent), m_db(db)
{
    load();
}

void SettingsService::load()
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT key, value FROM settings"));
    if (!stmt) {
        qCWarning(lcDb) << "Lecture des préférences impossible :" << stmt.error().message;
        return;
    }
    while (true) {
        auto row = stmt.value().step();
        if (!row || !row.value())
            break;
        m_cache.insert(stmt.value().columnText(0), decode(stmt.value().columnText(1)));
    }
}

QJsonValue SettingsService::value(const QString& key, const QJsonValue& defaultValue) const
{
    const auto it = m_cache.constFind(key);
    if (it == m_cache.cend() || it->isUndefined() || it->isNull())
        return defaultValue;
    return *it;
}

bool SettingsService::setValue(const QString& key, const QJsonValue& value)
{
    if (m_cache.value(key) == value && m_cache.contains(key))
        return true;
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO settings(key, value) VALUES (?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    if (!stmt)
        return false;
    stmt.value().bind(1, key).bind(2, encode(value));
    if (auto r = stmt.value().run(); !r) {
        qCWarning(lcDb) << "Écriture de la préférence" << key << "impossible :" << r.error().message;
        return false;
    }
    m_cache.insert(key, value);
    emit changed(key);
    return true;
}

ThemeMode SettingsService::themeMode() const
{
    return themeModeFromString(value(SettingKeys::Theme, QStringLiteral("system")).toString());
}

void SettingsService::setThemeMode(ThemeMode mode) { setValue(SettingKeys::Theme, themeModeToString(mode)); }

double SettingsService::uiScale() const
{
    return std::clamp(value(SettingKeys::UiScale, 1.0).toDouble(1.0), 0.9, 1.5);
}

void SettingsService::setUiScale(double scale) { setValue(SettingKeys::UiScale, std::clamp(scale, 0.9, 1.5)); }

bool SettingsService::animationsEnabled() const { return value(SettingKeys::Animations, true).toBool(true); }

void SettingsService::setAnimationsEnabled(bool enabled) { setValue(SettingKeys::Animations, enabled); }

QString SettingsService::themeModeToString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark: return QStringLiteral("dark");
    case ThemeMode::System: break;
    }
    return QStringLiteral("system");
}

ThemeMode SettingsService::themeModeFromString(const QString& value)
{
    if (value == QLatin1String("light"))
        return ThemeMode::Light;
    if (value == QLatin1String("dark"))
        return ThemeMode::Dark;
    return ThemeMode::System;
}

}  // namespace studylab
