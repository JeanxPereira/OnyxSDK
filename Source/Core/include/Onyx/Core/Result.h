#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Onyx {

struct Error {
    std::string code;
    std::string message;
    int32_t native = 0;
};

template <class T>
class Result {
public:
    static Result Ok(T value) {
        Result r;
        r.m_value.emplace(std::move(value));
        return r;
    }

    static Result Fail(Error error) {
        Result r;
        r.m_error = std::move(error);
        return r;
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }
    T& value() { return *m_value; }
    const T& value() const { return *m_value; }
    const Error& error() const { return m_error; }

private:
    Result() = default;

    std::optional<T> m_value;
    Error m_error;
};

template <>
class Result<void> {
public:
    static Result Ok() { return Result(); }

    static Result Fail(Error error) {
        Result r;
        r.m_ok = false;
        r.m_error = std::move(error);
        return r;
    }

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const Error& error() const { return m_error; }

private:
    Result() = default;

    bool m_ok = true;
    Error m_error;
};

}
