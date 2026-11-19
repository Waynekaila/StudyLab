#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

struct sqlite3_stmt;

namespace studylab::db {

/// Requête préparée (RAII, déplaçable, non copiable). Index de paramètres à partir de 1.
class Statement {
public:
    explicit Statement(sqlite3_stmt* stmt);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, qint64 value);
    Statement& bind(int index, int value) { return bind(index, qint64(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, const QString& value);  ///< QString nul → NULL
    Statement& bind(int index, const QByteArray& value);
    Statement& bindNull(int index);
    /// Paramètre nommé, ex. ":title".
    int parameterIndex(const char* name) const;

    /// Avance d'une ligne : true = une ligne est disponible, false = terminé.
    Result<bool> step();
    /// Exécute jusqu'à la fin (INSERT/UPDATE/DELETE).
    Result<void> run();
    void reset();

    int columnCount() const;
    bool isNull(int column) const;
    qint64 columnInt64(int column) const;
    double columnDouble(int column) const;
    QString columnText(int column) const;
    QByteArray columnBlob(int column) const;

private:
    sqlite3_stmt* m_stmt = nullptr;
};

}  // namespace studylab::db
