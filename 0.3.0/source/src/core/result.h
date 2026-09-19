#pragma once

#include <string>
#include <utility>
#include <variant>

namespace drivelab {

enum class ErrorCode {
    InvalidArgument,
    InvalidIdentity,
    NotFound,
    Unavailable,
    PermissionDenied,
    SafetyBlocked,
    IdentityMismatch,
    InvalidState,
    ExecutionDisabled,
    ProcessFailure,
    TimedOut,
    IoError,
    Internal
};

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string component;
    std::string message;
};

template <typename T>
class Result {
public:
    static Result success(T value) {
        return Result(std::move(value));
    }

    static Result failure(Error error) {
        return Result(std::move(error));
    }

    [[nodiscard]] bool hasValue() const {
        return std::holds_alternative<T>(storage_);
    }

    explicit operator bool() const {
        return hasValue();
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(storage_);
    }

    [[nodiscard]] T& value() {
        return std::get<T>(storage_);
    }

    [[nodiscard]] const Error& error() const {
        return std::get<Error>(storage_);
    }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(Error error) : storage_(std::move(error)) {}

    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    static Result success() {
        return Result(true, {});
    }

    static Result failure(Error error) {
        return Result(false, std::move(error));
    }

    [[nodiscard]] bool hasValue() const {
        return success_;
    }

    explicit operator bool() const {
        return hasValue();
    }

    [[nodiscard]] const Error& error() const {
        return error_;
    }

private:
    Result(bool success, Error error) : success_(success), error_(std::move(error)) {}

    bool success_ = false;
    Error error_;
};

}  // namespace drivelab
