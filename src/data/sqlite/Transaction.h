#pragma once

#include "core/Result.h"

namespace studylab::db {

class Database;

/// Transaction RAII : BEGIN IMMEDIATE à la construction, ROLLBACK si commit() n'a pas été appelé.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    /// false si BEGIN a échoué (base verrouillée, lecture seule…).
    bool isActive() const { return m_active; }
    const Error& beginError() const { return m_beginError; }

    Result<void> commit();
    void rollback();

private:
    Database& m_db;
    bool m_active = false;
    Error m_beginError;
};

}  // namespace studylab::db
