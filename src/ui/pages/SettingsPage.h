#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace studylab {
struct BackupInfo;
}

namespace studylab {
class OcrIndexer;
class SettingsService;
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
    ToggleSwitch* ocrSwitch() const { return m_ocrSwitch; }
    QPushButton* ocrLanguagesButton() const { return m_ocrLanguages; }
    QPushButton* ocrRerunButton() const { return m_ocrRerun; }
    QLabel* ocrStatus() const { return m_ocrStatus; }

    /// Indexation du texte intégral (Données) : état et « Réindexer ».
    void setTextIndexer(TextIndexer* indexer);
    /// OCR des pages numérisées (Données) : activation, langues, état, « Relire ».
    void setOcrIndexer(OcrIndexer* ocr);

    /// Langues d'OCR choisies (installées seulement) ; aucune choisie : langues par défaut.
    static QStringList ocrLanguages(const SettingsService& settings);
    /// Nom français d'une langue Tesseract (« fra » → « Français »), sinon le code.
    static QString ocrLanguageName(const QString& code);

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
    void refreshOcrStatus();
    void buildOcrSection(QVBoxLayout* v);

    AppContext& m_ctx;
    TextIndexer* m_indexer = nullptr;
    QPushButton* m_reindexButton = nullptr;
    QLabel* m_indexStatus = nullptr;
    OcrIndexer* m_ocr = nullptr;
    ToggleSwitch* m_ocrSwitch = nullptr;
    QPushButton* m_ocrLanguages = nullptr;
    QMenu* m_ocrLanguageMenu = nullptr;
    QPushButton* m_ocrRerun = nullptr;
    QLabel* m_ocrStatus = nullptr;
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
