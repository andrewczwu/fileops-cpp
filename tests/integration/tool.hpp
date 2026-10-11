#pragma once

// Runs the real `fileops` executable as a child process and captures everything it produces. By
// default the binary this build produced is used; set FILEOPS_BIN to run the same suite against a
// packaged binary (the smoke test does this inside the Debian containers).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fileops::test {

using Environment = std::map<std::string, std::optional<std::string>>;

/// Captured outcome of one CLI invocation.
struct CliResult {
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;

    /// Lines on stderr that are JSON log records, parsed.
    [[nodiscard]] std::vector<nlohmann::json> log_records() const;
    /// Lines on stderr that are user-facing `fileops: ...` messages.
    [[nodiscard]] std::vector<std::string> messages() const;
    /// Non-empty stderr lines, in order.
    [[nodiscard]] std::vector<std::string> stderr_lines() const;
    /// The single record with the given event id; throws if there is not exactly one.
    [[nodiscard]] nlohmann::json log(int event_id) const;
    [[nodiscard]] bool has_log(int event_id) const;
    [[nodiscard]] std::vector<int> event_ids() const;
};

class Tool {
public:
    /// Which binary is exercised, for diagnostics.
    static std::string description();

    /// Runs with JSON logging at Debug level so every test has log evidence to inspect.
    static CliResult run(const std::filesystem::path& working_directory,
                         const std::vector<std::string>& args);

    /// Runs with exactly the given environment overrides (nullopt removes a variable). FILEOPS_*
    /// variables inherited from the host are always cleared first.
    static CliResult run(const std::filesystem::path& working_directory,
                         const Environment& environment,
                         const std::vector<std::string>& args);
};

}  // namespace fileops::test
