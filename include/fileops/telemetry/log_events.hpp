#pragma once

#include <string_view>

namespace fileops::telemetry::log_events {

/// Stable event identifiers for every structured log record the library emits. Tests and log
/// consumers match on these rather than on message text. Identical to the .NET sibling project.
/// Ranges: 1xxx lifecycle, 2xxx create, 3xxx copy, 4xxx combine, 5xxx delete.

/// An operation is about to run.
inline constexpr int operation_started = 1000;
/// An operation finished successfully.
inline constexpr int operation_completed = 1001;
/// An operation failed with a known exit code.
inline constexpr int operation_failed = 1002;
/// A file was created.
inline constexpr int file_created = 2001;
/// A file was copied.
inline constexpr int file_copied = 3001;
/// Two files were concatenated into a third.
inline constexpr int files_combined = 4001;
/// A file was deleted.
inline constexpr int file_deleted = 5001;
/// Delete was asked to ignore a missing file.
inline constexpr int delete_skipped_missing = 5002;

/// The event name for an id, e.g. "OperationStarted"; "Unknown" otherwise.
constexpr std::string_view name(int event_id) noexcept {
    switch (event_id) {
        case operation_started:
            return "OperationStarted";
        case operation_completed:
            return "OperationCompleted";
        case operation_failed:
            return "OperationFailed";
        case file_created:
            return "FileCreated";
        case file_copied:
            return "FileCopied";
        case files_combined:
            return "FilesCombined";
        case file_deleted:
            return "FileDeleted";
        case delete_skipped_missing:
            return "DeleteSkippedMissing";
        default:
            return "Unknown";
    }
}

}  // namespace fileops::telemetry::log_events
