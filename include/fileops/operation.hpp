#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace fileops {

/// The commands the tool supports. Used as a structured field in logs and spans.
enum class Operation {
    Create,
    Copy,
    Combine,
    Delete,
};

/// The enumerator name, e.g. "Copy".
constexpr std::string_view to_string(Operation operation) noexcept {
    switch (operation) {
        case Operation::Create:
            return "Create";
        case Operation::Copy:
            return "Copy";
        case Operation::Combine:
            return "Combine";
        case Operation::Delete:
            return "Delete";
    }
    return "Unknown";
}

/// Lower-case form used for span names and metric tags, e.g. "copy".
constexpr std::string_view lower_name(Operation operation) noexcept {
    switch (operation) {
        case Operation::Create:
            return "create";
        case Operation::Copy:
            return "copy";
        case Operation::Combine:
            return "combine";
        case Operation::Delete:
            return "delete";
    }
    return "unknown";
}

/// What an operation produced. Returned to the CLI for reporting and recorded in telemetry.
struct Result {
    Operation operation;
    /// The file the operation produced or removed.
    std::filesystem::path path;
    /// Bytes written (create/copy/combine) or removed (delete).
    std::uint64_t bytes;

    bool operator==(const Result&) const = default;
};

}  // namespace fileops
