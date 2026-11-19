#pragma once

#include "core/AppPaths.h"
#include "core/Result.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

namespace db {
class Database;
}

enum class BackupKind { Automatic, Manual, BeforeMigration, BeforeRestore };

/// Une sauvegarde du dossier backups/ : copie complète et cohérente de la base (VACUUM INTO).
struct BackupInfo {
    QString path;
    BackupKind kind = BackupKind::Manual;
    QDateTime createdAt;  ///< heure locale, lue dans le nom du fichier
    qint64 size = 0;
};

QString backupKindLabel(BackupKind kind);  ///< « Automatique », « Manuelle »…

/// Sauvegarde et restauration de la base (02-architecture §4) :
///  - sauvegarde automatique au démarrage si la dernière date de plus de 24 h, et avant chaque migration ;
///  - rotation des sauvegardes automatiques : toutes celles des 7 derniers jours, puis une par semaine
///    (4 semaines), puis une par mois (6 mois) ; les 5 dernières d'avant migration et les 3 dernières
///    d'avant restauration ; les sauvegardes manuelles ne sont jamais supprimées ;
///  - restauration en deux temps : programmée depuis l'application ouverte, appliquée au démarrage
///    suivant, avant l'ouverture de la base (la base remplacée est d'abord sauvegardée).
class BackupService {
public:
    BackupService(AppPaths paths, db::Database& db);

    /// Sauvegardes présentes, les plus récentes d'abord.
    QList<BackupInfo> list() const;
    static QList<BackupInfo> list(const QString& directory);
    std::optional<BackupInfo> latest() const;

    Result<BackupInfo> create(BackupKind kind, const QDateTime& now = QDateTime::currentDateTime());
    /// Sauvegarde automatique si aucune sauvegarde ne date de moins de 24 h, puis rotation.
    Result<std::optional<BackupInfo>> backupIfDue(const QDateTime& now = QDateTime::currentDateTime());
    /// Supprime les sauvegardes que la rotation ne garde pas ; renvoie les fichiers supprimés.
    QStringList rotate(const QDateTime& now = QDateTime::currentDateTime());

    /// Vérifie qu'un fichier est une base StudyLab saine (ouverture, quick_check, table des migrations).
    static Result<void> validate(const QString& file);
    /// Programme la restauration de `file` au prochain démarrage.
    Result<void> scheduleRestore(const QString& file);
    std::optional<QString> pendingRestore() const;
    void cancelPendingRestore();

    /// Au démarrage, base fermée : applique la restauration programmée s'il y en a une.
    /// Renvoie le fichier restauré (vide si rien n'était programmé).
    static Result<QString> applyPendingRestore(const AppPaths& paths);
    /// Remplace la base (fermée) par la sauvegarde, après avoir sauvegardé la base actuelle si elle s'ouvre.
    static Result<void> restoreNow(const AppPaths& paths, const QString& file);

    static QString fileNameFor(BackupKind kind, const QDateTime& when, const QString& detail = {});
    static std::optional<BackupInfo> parse(const QString& path);

private:
    static QString pendingFile(const AppPaths& paths);

    AppPaths m_paths;
    db::Database& m_db;
};

}  // namespace studylab
