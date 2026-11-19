#pragma once

#include "core/Result.h"

#include <QList>
#include <QString>

#include <functional>

namespace studylab::db {

class Database;

struct Migration {
    int version = 0;
    QString name;
    QString sql;
};

/// Applique les migrations SQL versionnées (fichiers « NNNN_nom.sql »), chacune dans sa transaction.
class Migrator {
public:
    /// Charge les migrations depuis un dossier (par défaut les ressources « :/migrations »).
    static Result<QList<Migration>> loadFrom(const QString& directory = QStringLiteral(":/migrations"));

    explicit Migrator(QList<Migration> migrations);

    int latestVersion() const;
    static Result<int> currentVersion(Database& db);

    /// Appelé une fois, avant la première migration, si la base contient déjà des données (version > 0).
    using BeforeMigrateHook = std::function<Result<void>(int fromVersion, int toVersion)>;

    /// Met la base à jour. Erreur SchemaTooNew si la base vient d'une version plus récente de StudyLab.
    Result<int> migrate(Database& db, const BeforeMigrateHook& beforeMigrate = {}) const;

private:
    QList<Migration> m_migrations;
};

}  // namespace studylab::db
