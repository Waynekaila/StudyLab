#include "data/sqlite/Transaction.h"

#include "core/Log.h"
#include "data/sqlite/Database.h"

namespace studylab::db {

Transaction::Transaction(Database& db) : m_db(db)
{
    auto r = m_db.exec(QStringLiteral("BEGIN IMMEDIATE"));
    m_active = r.ok();
    if (!r)
        m_beginError = r.error();
}

Transaction::~Transaction()
{
    if (m_active)
        rollback();
}

Result<void> Transaction::commit()
{
    if (!m_active)
        return Error::make(Error::Code::Database, QStringLiteral("Aucune transaction active"));
    auto r = m_db.exec(QStringLiteral("COMMIT"));
    if (r)
        m_active = false;
    return r;
}

void Transaction::rollback()
{
    if (!m_active)
        return;
    if (auto r = m_db.exec(QStringLiteral("ROLLBACK")); !r)
        qCWarning(lcDb) << "ROLLBACK a échoué :" << r.error().message;
    m_active = false;
}

}  // namespace studylab::db
