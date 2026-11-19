#include "data/sqlite/Statement.h"

#include <sqlite3.h>

#include <utility>

namespace studylab::db {

Statement::Statement(sqlite3_stmt* stmt) : m_stmt(stmt) {}

Statement::~Statement() { sqlite3_finalize(m_stmt); }

Statement::Statement(Statement&& other) noexcept : m_stmt(std::exchange(other.m_stmt, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other) {
        sqlite3_finalize(m_stmt);
        m_stmt = std::exchange(other.m_stmt, nullptr);
    }
    return *this;
}

Statement& Statement::bind(int index, qint64 value)
{
    sqlite3_bind_int64(m_stmt, index, value);
    return *this;
}

Statement& Statement::bind(int index, double value)
{
    sqlite3_bind_double(m_stmt, index, value);
    return *this;
}

Statement& Statement::bind(int index, const QString& value)
{
    if (value.isNull())
        return bindNull(index);
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text64(m_stmt, index, utf8.constData(), sqlite3_uint64(utf8.size()), SQLITE_TRANSIENT, SQLITE_UTF8);
    return *this;
}

Statement& Statement::bind(int index, const QByteArray& value)
{
    sqlite3_bind_blob64(m_stmt, index, value.constData(), sqlite3_uint64(value.size()), SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bindNull(int index)
{
    sqlite3_bind_null(m_stmt, index);
    return *this;
}

int Statement::parameterIndex(const char* name) const { return sqlite3_bind_parameter_index(m_stmt, name); }

Result<bool> Statement::step()
{
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    return Error::make(Error::Code::Database, QString::fromUtf8(sqlite3_errmsg(sqlite3_db_handle(m_stmt))));
}

Result<void> Statement::run()
{
    while (true) {
        auto r = step();
        if (!r)
            return r.error();
        if (!r.value())
            return Result<void>::success();
    }
}

void Statement::reset()
{
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
}

int Statement::columnCount() const { return sqlite3_column_count(m_stmt); }
bool Statement::isNull(int column) const { return sqlite3_column_type(m_stmt, column) == SQLITE_NULL; }
qint64 Statement::columnInt64(int column) const { return sqlite3_column_int64(m_stmt, column); }
double Statement::columnDouble(int column) const { return sqlite3_column_double(m_stmt, column); }

QString Statement::columnText(int column) const
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, column));
    if (!text)
        return {};
    return QString::fromUtf8(text, sqlite3_column_bytes(m_stmt, column));
}

QByteArray Statement::columnBlob(int column) const
{
    const void* data = sqlite3_column_blob(m_stmt, column);
    const int size = sqlite3_column_bytes(m_stmt, column);
    return data ? QByteArray(static_cast<const char*>(data), size) : QByteArray();
}

}  // namespace studylab::db
