#pragma once

#include "ui/theme/Tokens.h"

#include <QObject>
#include <QString>

namespace studylab {
class SettingsService;
}

namespace studylab::ui {

/// Applique le thème (clair, sombre ou système), la police et l'échelle d'interface à toute l'application.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(SettingsService& settings, QObject* parent = nullptr);

    /// Charge les polices embarquées. Retourne la famille d'interface utilisée.
    static QString loadBundledFonts();

    void apply();

    const Palette& palette() const { return m_palette; }
    bool isDark() const { return m_palette.dark; }
    double scale() const { return m_scale; }
    /// Taille en pixels mise à l'échelle.
    int px(int base) const;
    QString styleSheet() const { return m_styleSheet; }

    /// Feuille de style générée pour une palette et une échelle données (testable sans QApplication).
    static QString buildStyleSheet(const Palette& p, double scale, const QString& fontFamily);

signals:
    void themeChanged();

private:
    bool systemPrefersDark() const;

    SettingsService& m_settings;
    Palette m_palette;
    double m_scale = 1.0;
    QString m_styleSheet;
    QString m_fontFamily;
    bool m_initialSystemDark = false;
};

}  // namespace studylab::ui
