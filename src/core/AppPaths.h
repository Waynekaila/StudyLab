#pragma once

#include <QString>

namespace studylab {

/// Emplacements des données de l'application.
///
/// Windows : %APPDATA%/StudyLab · Debian : ~/.local/share/StudyLab.
/// La variable d'environnement STUDYLAB_DATA_DIR remplace ce dossier (tests, installation portable).
class AppPaths {
public:
    explicit AppPaths(QString dataDir);

    /// Dossier par défaut de la plateforme (ou STUDYLAB_DATA_DIR).
    static AppPaths standard();

    QString dataDir() const { return m_dataDir; }
    QString databaseFile() const;
    QString libraryDir() const;
    QString coversDir() const;
    QString attachmentsDir() const;
    QString backupsDir() const;
    QString runningMarkerFile() const;

    /// Crée les dossiers manquants. Retourne false si l'un d'eux ne peut pas être créé.
    bool ensureCreated() const;

private:
    QString m_dataDir;
};

}  // namespace studylab
