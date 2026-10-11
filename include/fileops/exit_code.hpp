#pragma once

#include <string_view>

namespace fileops {

/// Process exit codes returned by the CLI. Scripts can branch on these without parsing output.
enum class ExitCode : int {
    /// The operation completed.
    Success = 0,
    /// The request itself was invalid (bad arguments, empty path, same source and destination, path is a
    /// directory).
    UsageError = 1,
    /// A required input file or directory does not exist.
    NotFound = 2,
    /// The destination already exists and overwriting was not requested.
    AlreadyExists = 3,
    /// The file system refused the operation (permissions, disk full, locked file).
    IoError = 4,
    /// An unexpected failure inside the tool. Mirrors EX_SOFTWARE from sysexits.h.
    InternalError = 70,
};

/// The enumerator name, e.g. "NotFound". Used in log records so a reader can match on it.
constexpr std::string_view to_string(ExitCode code) noexcept {
    switch (code) {
        case ExitCode::Success:
            return "Success";
        case ExitCode::UsageError:
            return "UsageError";
        case ExitCode::NotFound:
            return "NotFound";
        case ExitCode::AlreadyExists:
            return "AlreadyExists";
        case ExitCode::IoError:
            return "IoError";
        case ExitCode::InternalError:
            return "InternalError";
    }
    return "Unknown";
}

constexpr int to_int(ExitCode code) noexcept {
    return static_cast<int>(code);
}

}  // namespace fileops
