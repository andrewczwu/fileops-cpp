#pragma once

#include "fileops/exit_code.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace fileops {

/// A failure the user can act on. Carries the ExitCode the process should return and a message
/// that is safe to print verbatim.
class Error : public std::runtime_error {
public:
    Error(ExitCode code, const std::string& message, std::filesystem::path path = {})
        : std::runtime_error(message), code_(code), path_(std::move(path)) {}

    /// The exit code the process should return for this failure.
    [[nodiscard]] ExitCode code() const noexcept { return code_; }

    /// The path the failure relates to, if any.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    ExitCode code_;
    std::filesystem::path path_;
};

}  // namespace fileops
