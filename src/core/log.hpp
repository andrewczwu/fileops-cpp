#pragma once

// Internal helpers that build the library's structured log records. Each function corresponds to
// one event id in fileops/telemetry/log_events.hpp and carries a fixed message template and field
// set, so the emitted records form a stable contract (see docs/OBSERVABILITY.md).

#include "fileops/exit_code.hpp"
#include "fileops/operation.hpp"
#include "fileops/telemetry/log.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fileops::core_log {

void operation_started(telemetry::Logger& logger, Operation operation, const std::filesystem::path& path);
void operation_completed(telemetry::Logger& logger,
                         Operation operation,
                         const std::filesystem::path& path,
                         std::uint64_t bytes,
                         double duration_ms);
void operation_failed(telemetry::Logger& logger,
                      Operation operation,
                      const std::filesystem::path& path,
                      ExitCode exit_code,
                      std::string_view reason);
void file_created(telemetry::Logger& logger, const std::filesystem::path& path, std::uint64_t bytes);
void file_copied(telemetry::Logger& logger,
                 const std::filesystem::path& source,
                 const std::filesystem::path& destination,
                 std::uint64_t bytes);
void files_combined(telemetry::Logger& logger,
                    const std::filesystem::path& first,
                    const std::filesystem::path& second,
                    const std::filesystem::path& output,
                    std::uint64_t bytes);
void file_deleted(telemetry::Logger& logger, const std::filesystem::path& path, std::uint64_t bytes);
void delete_skipped_missing(telemetry::Logger& logger, const std::filesystem::path& path);

}  // namespace fileops::core_log
