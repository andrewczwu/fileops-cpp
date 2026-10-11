#pragma once

#include "fileops/operation.hpp"
#include "fileops/telemetry/log.hpp"
#include "fileops/telemetry/observer.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace fileops {

/// The four file operations the tool offers. Every call is timed, logged with a stable event id and
/// reported to an Observer (the span and metric facts), so callers and tests can observe exactly
/// what happened without inspecting the file system.
class FileOperations {
public:
    explicit FileOperations(telemetry::Logger& logger,
                            telemetry::Observer& observer = telemetry::null_observer())
        : logger_(logger), observer_(observer) {}

    /// Creates a file, empty or with the given text (UTF-8, written exactly as given).
    Result create(const std::filesystem::path& path,
                  std::optional<std::string_view> text = std::nullopt,
                  bool overwrite = false);

    /// Copies `source` to `destination`, preserving Unix permission bits.
    Result copy(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                bool overwrite = false);

    /// Concatenates `first` then `second` into `output`. Both inputs are read before the output is
    /// renamed into place, so an input may also be the output (`combine a b a --force` appends).
    Result combine(const std::filesystem::path& first,
                   const std::filesystem::path& second,
                   const std::filesystem::path& output,
                   bool overwrite = false);

    /// Deletes a file. Directories are refused. With `ignore_missing`, a missing file succeeds with a
    /// warning.
    Result remove(const std::filesystem::path& path, bool ignore_missing = false);

private:
    Result execute(const telemetry::OperationContext& context, const std::function<Result()>& body);

    telemetry::Logger& logger_;
    telemetry::Observer& observer_;
};

}  // namespace fileops
