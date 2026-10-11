#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fileops::telemetry {

/// Log levels, in the same order and with the same names as the .NET sibling project so the two
/// tools' output is interchangeable for log consumers.
enum class LogLevel {
    Trace,
    Debug,
    Information,
    Warning,
    Error,
    Critical,
    None,
};

constexpr std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "Trace";
        case LogLevel::Debug:
            return "Debug";
        case LogLevel::Information:
            return "Information";
        case LogLevel::Warning:
            return "Warning";
        case LogLevel::Error:
            return "Error";
        case LogLevel::Critical:
            return "Critical";
        case LogLevel::None:
            return "None";
    }
    return "Unknown";
}

/// Case-insensitive parse of a level name; nullopt for anything else.
std::optional<LogLevel> parse_log_level(std::string_view text) noexcept;

/// A named, typed value attached to a record ("Path": "a.txt", "Bytes": 12).
using FieldValue = std::variant<std::string, std::int64_t, double, bool>;

struct Field {
    std::string name;
    FieldValue value;
};

/// One structured log record. The message is for humans; the event id and fields are for machines.
struct LogRecord {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level = LogLevel::Information;
    int event_id = 0;
    std::string event_name;
    std::string category;
    std::string message;
    std::vector<Field> state;
};

/// Where records go. The core library only depends on this interface; sinks (text, JSON, in-memory
/// for tests) live in the CLI and the tests.
class Logger {
public:
    virtual ~Logger() = default;
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    [[nodiscard]] virtual bool enabled(LogLevel level) const noexcept = 0;
    virtual void log(const LogRecord& record) = 0;
};

/// Discards everything.
class NullLogger final : public Logger {
public:
    [[nodiscard]] bool enabled(LogLevel /*level*/) const noexcept override { return false; }
    void log(const LogRecord& /*record*/) override {}
};

}  // namespace fileops::telemetry
