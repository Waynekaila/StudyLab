#pragma once

#include "core/Result.h"

#include <QString>
#include <QStringList>

#include <memory>

struct sqlite3;

namespace studylab::db {

class Statement;

/// Connexion SQLite (RAII). Une instance par thread.
///
/// À l'ouverture en écriture : WAL, synchronous=NORMAL, foreign_keys=ON, busy_timeout=5 s.
class Database {
public:
    enum class Mode { ReadWrite, ReadOnly };

    static Result<std::unique_ptr<Database>> open(const QString& path, Mode mode = Mode::ReadWrite);
    /// Base en mémoire (tests).
    static Result<std::unique_ptr<Database>> openInMemory();

    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Exécute une ou plusieurs instructions SQL sans résultat.
    Result<void> exec(const QString& sql);
    Result<Statement> prepare(const QString& sql);

    /// Raccourci : première colonne de la première ligne (texte), ou erreur.
    Result<QString> queryString(const QString& sql);
    Result<qint64> queryInt(const QString& sql);

    qint64 lastInsertRowId() const;
    int changes() const;

    QString path() const { return m_path; }
    bool isReadOnly() const { return m_mode == Mode::ReadOnly; }

    /// Options de compilation de SQLite (ex. "ENABLE_FTS5").
    QStringList compileOptions();
    bool hasCompileOption(const QString& option);
    static QString sqliteVersion();

    /// PRAGMA quick_check (rapide) ou integrity_check (complet). Liste vide = base saine.
    Result<QStringList> checkIntegrity(bool full = false);
    /// Copie cohérente de la base, à chaud (VACUUM INTO). Le fichier cible ne doit pas exister.
    Result<void> backupTo(const QString& targetFile);

    sqlite3* handle() const { return m_db; }
    QString lastErrorMessage() const;

private:
    Database(sqlite3* db, QString path, Mode mode);
    Error makeError(const QString& context) const;

    sqlite3* m_db = nullptr;
    QString m_path;
    Mode m_mode;
};

}  // namespace studylab::db
