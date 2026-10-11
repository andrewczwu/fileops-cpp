#include "log.hpp"

#include "fileops/telemetry/log_events.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace fileops::telemetry {

std::optional<LogLevel> parse_log_level(std::string_view text) noexcept {
    constexpr std::array<std::pair<std::string_view, LogLevel>, 7> names{{
        {"trace", LogLevel::Trace},
        {"debug", LogLevel::Debug},
        {"information", LogLevel::Information},
        {"warning", LogLevel::Warning},
        {"error", LogLevel::Error},
        {"critical", LogLevel::Critical},
        {"none", LogLevel::None},
    }};
    for (const auto& [name, level] : names) {
        if (name.size() == text.size() &&
            std::equal(name.begin(), name.end(), text.begin(), [](char a, char b) {
                return a == static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
            })) {
            return level;
        }
    }
    return std::nullopt;
}

}  // namespace fileops::telemetry

namespace fileops::core_log {
namespace {

using telemetry::Field;
using telemetry::Logger;
using telemetry::LogLevel;
using telemetry::LogRecord;

constexpr std::string_view category = "fileops.core.FileOperations";

void emit(Logger& logger, LogLevel level, int event_id, std::string message, std::vector<Field> state) {
    if (!logger.enabled(level)) {
        return;
    }
    logger.log(LogRecord{
        .timestamp = std::chrono::system_clock::now(),
        .level = level,
        .event_id = event_id,
        .event_name = std::string{telemetry::log_events::name(event_id)},
        .category = std::string{category},
        .message = std::move(message),
        .state = std::move(state),
    });
}

Field str(std::string_view name, const std::filesystem::path& value) {
    return Field{std::string{name}, value.string()};
}

Field str(std::string_view name, std::string_view value) {
    return Field{std::string{name}, std::string{value}};
}

Field num(std::string_view name, std::uint64_t value) {
    return Field{std::string{name}, static_cast<std::int64_t>(value)};
}

}  // namespace

void operation_started(Logger& logger, Operation operation, const std::filesystem::path& path) {
    emit(logger,
         LogLevel::Debug,
         telemetry::log_events::operation_started,
         std::format("Starting {} on {}", to_string(operation), path.string()),
         {str("Operation", to_string(operation)), str("Path", path)});
}

void operation_completed(Logger& logger,
                         Operation operation,
                         const std::filesystem::path& path,
                         std::uint64_t bytes,
                         double duration_ms) {
    emit(logger,
         LogLevel::Information,
         telemetry::log_events::operation_completed,
         std::format("{} completed on {}: {} bytes in {:.2f} ms",
                     to_string(operation),
                     path.string(),
                     bytes,
                     duration_ms),
         {str("Operation", to_string(operation)),
          str("Path", path),
          num("Bytes", bytes),
          Field{"DurationMs", duration_ms}});
}

void operation_failed(Logger& logger,
                      Operation operation,
                      const std::filesystem::path& path,
                      ExitCode exit_code,
                      std::string_view reason) {
    emit(logger,
         LogLevel::Error,
         telemetry::log_events::operation_failed,
         std::format("{} failed on {} with exit code {}: {}",
                     to_string(operation),
                     path.string(),
                     to_string(exit_code),
                     reason),
         {str("Operation", to_string(operation)),
          str("Path", path),
          str("ExitCode", to_string(exit_code)),
          str("Reason", reason)});
}

void file_created(Logger& logger, const std::filesystem::path& path, std::uint64_t bytes) {
    emit(logger,
         LogLevel::Information,
         telemetry::log_events::file_created,
         std::format("Created {} ({} bytes)", path.string(), bytes),
         {str("Path", path), num("Bytes", bytes)});
}

void file_copied(Logger& logger,
                 const std::filesystem::path& source,
                 const std::filesystem::path& destination,
                 std::uint64_t bytes) {
    emit(logger,
         LogLevel::Information,
         telemetry::log_events::file_copied,
         std::format("Copied {} to {} ({} bytes)", source.string(), destination.string(), bytes),
         {str("Source", source), str("Destination", destination), num("Bytes", bytes)});
}

void files_combined(Logger& logger,
                    const std::filesystem::path& first,
                    const std::filesystem::path& second,
                    const std::filesystem::path& output,
                    std::uint64_t bytes) {
    emit(
        logger,
        LogLevel::Information,
        telemetry::log_events::files_combined,
        std::format(
            "Combined {} and {} into {} ({} bytes)", first.string(), second.string(), output.string(), bytes),
        {str("First", first), str("Second", second), str("Output", output), num("Bytes", bytes)});
}

void file_deleted(Logger& logger, const std::filesystem::path& path, std::uint64_t bytes) {
    emit(logger,
         LogLevel::Information,
         telemetry::log_events::file_deleted,
         std::format("Deleted {} ({} bytes)", path.string(), bytes),
         {str("Path", path), num("Bytes", bytes)});
}

void delete_skipped_missing(Logger& logger, const std::filesystem::path& path) {
    emit(logger,
         LogLevel::Warning,
         telemetry::log_events::delete_skipped_missing,
         std::format("Nothing to delete: {} does not exist", path.string()),
         {str("Path", path)});
}

}  // namespace fileops::core_log
