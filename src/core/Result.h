#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace studylab {

/// Erreur métier ou technique, toujours accompagnée d'un message lisible.
struct Error {
    enum class Code {
        Generic,
        Io,
        Database,
        SchemaTooNew,
        NotFound,
        InvalidArgument,
    };

    Code code = Code::Generic;
    QString message;

    static Error make(Code c, QString msg) { return Error{c, std::move(msg)}; }
};

/// Résultat d'une opération pouvant échouer, sans exception.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : m_data(std::move(value)) {}            // NOLINT(google-explicit-constructor)
    Result(Error error) : m_data(std::move(error)) {}        // NOLINT(google-explicit-constructor)

    bool ok() const { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const { return ok(); }

    T& value() & { return std::get<T>(m_data); }
    const T& value() const& { return std::get<T>(m_data); }
    T&& value() && { return std::get<T>(std::move(m_data)); }

    const Error& error() const { return std::get<Error>(m_data); }

private:
    std::variant<T, Error> m_data;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)), m_ok(false) {}  // NOLINT(google-explicit-constructor)

    static Result success() { return {}; }

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const Error& error() const { return m_error; }

private:
    Error m_error;
    bool m_ok = true;
};

}  // namespace studylab
