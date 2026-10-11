#include "sinks.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace fileops::cli {
namespace {

using telemetry::LogLevel;
using telemetry::LogRecord;

class StreamSink : public telemetry::Logger {
public:
    StreamSink(LogLevel minimum, std::ostream& out) : minimum_(minimum), out_(out) {}

    [[nodiscard]] bool enabled(LogLevel level) const noexcept override {
        return minimum_ != LogLevel::None && level >= minimum_;
    }

protected:
    std::ostream& out() noexcept { return out_; }

private:
    LogLevel minimum_;
    std::ostream& out_;
};

/// `16:48:15.060 info: fileops.core.FileOperations[3001] Copied a to b (1 bytes)`
/// Times are UTC: the same clock the JSON sink uses, and the only one libc++ can format without a
/// time-zone database.
class TextSink final : public StreamSink {
public:
    using StreamSink::StreamSink;

    void log(const LogRecord& record) override {
        const auto time = std::chrono::floor<std::chrono::milliseconds>(record.timestamp);
        out() << std::format("{:%H:%M:%S} {}: {}[{}] {}\n",
                             time,
                             short_level(record.level),
                             record.category,
                             record.event_id,
                             record.message);
    }

private:
    static constexpr std::string_view short_level(LogLevel level) noexcept {
        switch (level) {
            case LogLevel::Trace:
                return "trce";
            case LogLevel::Debug:
                return "dbug";
            case LogLevel::Information:
                return "info";
            case LogLevel::Warning:
                return "warn";
            case LogLevel::Error:
                return "fail";
            case LogLevel::Critical:
                return "crit";
            case LogLevel::None:
                return "none";
        }
        return "????";
    }
};

/// One compact JSON object per line: Timestamp (ISO 8601, UTC), EventId, EventName, LogLevel,
/// Category, Message, State{named fields}. The schema is part of the observability contract.
class JsonSink final : public StreamSink {
public:
    using StreamSink::StreamSink;

    void log(const LogRecord& record) override {
        nlohmann::ordered_json state = nlohmann::ordered_json::object();
        for (const auto& field : record.state) {
            std::visit([&](const auto& value) { state[field.name] = value; }, field.value);
        }
        const auto utc = std::chrono::floor<std::chrono::microseconds>(record.timestamp);
        const nlohmann::ordered_json line = {
            {"Timestamp", std::format("{:%FT%T}Z", utc)},
            {"EventId", record.event_id},
            {"EventName", record.event_name},
            {"LogLevel", std::string{to_string(record.level)}},
            {"Category", record.category},
            {"Message", record.message},
            {"State", state},
        };
        out() << line.dump() << '\n';
    }
};

class TeeLogger final : public telemetry::Logger {
public:
    explicit TeeLogger(std::vector<std::unique_ptr<telemetry::Logger>> sinks) : sinks_(std::move(sinks)) {}

    [[nodiscard]] bool enabled(LogLevel level) const noexcept override {
        return std::ranges::any_of(sinks_, [&](const auto& sink) { return sink->enabled(level); });
    }

    void log(const LogRecord& record) override {
        for (const auto& sink : sinks_) {
            if (sink->enabled(record.level)) {
                sink->log(record);
            }
        }
    }

private:
    std::vector<std::unique_ptr<telemetry::Logger>> sinks_;
};

}  // namespace

std::unique_ptr<telemetry::Logger> make_tee(std::vector<std::unique_ptr<telemetry::Logger>> sinks) {
    return std::make_unique<TeeLogger>(std::move(sinks));
}

std::unique_ptr<telemetry::Logger> make_sink(LogFormat format, LogLevel minimum, std::ostream& out) {
    if (minimum == LogLevel::None) {
        return std::make_unique<telemetry::NullLogger>();
    }
    if (format == LogFormat::Json) {
        return std::make_unique<JsonSink>(minimum, out);
    }
    return std::make_unique<TextSink>(minimum, out);
}

}  // namespace fileops::cli
