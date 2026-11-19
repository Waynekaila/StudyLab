#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace studylab::ui {

class SegmentedControl;
class ToggleSwitch;

/// Paramètres : Apparence, Données, À propos.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(AppContext& ctx, QWidget* parent = nullptr);

    // Pour les tests
    SegmentedControl* themeControl() const { return m_theme; }
    QComboBox* scaleCombo() const { return m_scale; }
    ToggleSwitch* animationsSwitch() const { return m_animations; }
    SegmentedControl* importModeControl() const { return m_importMode; }
    QListWidget* categories() const { return m_categories; }

private:
    QWidget* buildAppearance();
    QWidget* buildLibrary();
    QWidget* buildData();
    QWidget* buildAbout();
    void syncFromSettings();
    void createBackup();

    AppContext& m_ctx;
    QListWidget* m_categories = nullptr;
    QStackedWidget* m_stack = nullptr;
    SegmentedControl* m_theme = nullptr;
    QComboBox* m_scale = nullptr;
    ToggleSwitch* m_animations = nullptr;
    SegmentedControl* m_importMode = nullptr;
    QLabel* m_backupStatus = nullptr;
};

}  // namespace studylab::ui
