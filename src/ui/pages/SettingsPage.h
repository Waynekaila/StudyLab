#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace studylab {
struct BackupInfo;
}

namespace studylab {
class TextIndexer;
}

namespace studylab::ui {

/// Propriété de QApplication : relancer StudyLab après la fermeture (restauration d'une sauvegarde).
inline constexpr char kRestartProperty[] = "studylabRestart";

class SegmentedControl;
class ToggleSwitch;

/// Paramètres : Apparence, Bibliothèque et import, Données (sauvegardes, restauration), À propos.
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
    QListWidget* backupList() const { return m_backups; }
    QPushButton* backupButton() const { return m_backupButton; }
    QPushButton* restoreButton() const { return m_restoreButton; }
    QPushButton* cancelRestoreButton() const { return m_cancelRestore; }
    QLabel* backupStatus() const { return m_backupStatus; }
    QLabel* pendingRestoreLabel() const { return m_pendingLabel; }
    QPushButton* reindexButton() const { return m_reindexButton; }
    QLabel* indexStatus() const { return m_indexStatus; }

    /// Indexation du texte intégral (Données) : état et « Réindexer ».
    void setTextIndexer(TextIndexer* indexer);

    /// Confirmation d'une restauration : 0 annuler, 1 au prochain démarrage, 2 redémarrer maintenant.
    /// Par défaut une boîte de dialogue ; les tests fournissent la réponse.
    using RestoreConfirmer = std::function<int(const BackupInfo& backup)>;
    void setRestoreConfirmer(RestoreConfirmer confirmer) { m_confirmRestore = std::move(confirmer); }
    void refreshBackups();

private:
    QWidget* buildAppearance();
    QWidget* buildLibrary();
    QWidget* buildData();
    QWidget* buildAbout();
    void syncFromSettings();
    void createBackup();
    void restoreSelected();

    void refreshIndexStatus();

    AppContext& m_ctx;
    TextIndexer* m_indexer = nullptr;
    QPushButton* m_reindexButton = nullptr;
    QLabel* m_indexStatus = nullptr;
    QListWidget* m_categories = nullptr;
    QStackedWidget* m_stack = nullptr;
    SegmentedControl* m_theme = nullptr;
    QComboBox* m_scale = nullptr;
    ToggleSwitch* m_animations = nullptr;
    SegmentedControl* m_importMode = nullptr;
    QLabel* m_backupStatus = nullptr;
    QLabel* m_lastBackup = nullptr;
    QListWidget* m_backups = nullptr;
    QPushButton* m_backupButton = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_cancelRestore = nullptr;
    QLabel* m_pendingLabel = nullptr;
    RestoreConfirmer m_confirmRestore;
};

}  // namespace studylab::ui
