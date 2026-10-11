// Command-line front end: parses arguments with CLI11, wires a log sink, runs one operation and maps
// the outcome to a process exit code. All behaviour lives in fileops::core; this file is glue.

#include "fileops/error.hpp"
#include "fileops/operations.hpp"
#include "fileops/telemetry/log.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

#include "otel.hpp"
#include "sinks.hpp"

#ifndef FILEOPS_VERSION_STRING
#define FILEOPS_VERSION_STRING "0.0.0"
#endif

namespace {

using fileops::ExitCode;
using fileops::FileOperations;
using fileops::Result;
using fileops::telemetry::LogLevel;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

/// Parsed global options, resolved from the command line with environment-variable defaults.
struct GlobalOptions {
    std::string log_level = "None";
    std::string log_format = "text";
    bool verbose = false;
    /// Whether --log-level (or FILEOPS_LOG_LEVEL) was given at all; it then governs every sink.
    bool log_level_given = false;

    [[nodiscard]] LogLevel minimum_level() const {
        LogLevel level = fileops::telemetry::parse_log_level(log_level).value_or(LogLevel::None);
        if (verbose && level > LogLevel::Information) {
            level = LogLevel::Information;
        }
        return level;
    }

    /// Level for the OTLP export: whatever was asked for, or Information when nothing was, so an
    /// endpoint receives useful records while the console stays silent.
    [[nodiscard]] LogLevel export_level() const {
        return (log_level_given || verbose) ? minimum_level() : LogLevel::Information;
    }

    [[nodiscard]] fileops::cli::LogFormat format() const {
        return lower(log_format) == "json" ? fileops::cli::LogFormat::Json : fileops::cli::LogFormat::Text;
    }
};

using Action = std::function<Result(FileOperations&)>;

/// Runs the chosen operation: log records first (the sinks write synchronously), telemetry is
/// flushed, then on failure one `fileops: <message>` line on stderr, then the exit code.
int run(const GlobalOptions& options, const Action& action) {
    // With OTEL_EXPORTER_OTLP_ENDPOINT set, spans, metrics and log records go out over OTLP/HTTP.
    auto runtime = fileops::cli::otel::Runtime::start_if_configured(FILEOPS_VERSION_STRING);

    std::vector<std::unique_ptr<fileops::telemetry::Logger>> sinks;
    sinks.push_back(fileops::cli::make_sink(options.format(), options.minimum_level(), std::cerr));
    if (runtime) {
        sinks.push_back(runtime->log_sink(options.export_level()));
    }
    auto logger = fileops::cli::make_tee(std::move(sinks));
    FileOperations operations(*logger, runtime ? runtime->observer() : fileops::telemetry::null_observer());

    int code = fileops::to_int(ExitCode::Success);
    std::string message;
    try {
        action(operations);
    } catch (const fileops::Error& e) {
        code = fileops::to_int(e.code());
        message = e.what();
    } catch (const std::exception& e) {
        code = fileops::to_int(ExitCode::InternalError);
        message = std::string{"unexpected error: "} + e.what();
    }

    // Everything is exported before the summary line so the records always come first.
    if (runtime) {
        runtime->shutdown();
    }
    if (!message.empty()) {
        std::cerr << "fileops: " << message << '\n';
    }
    return code;
}

}  // namespace

int run_main(int argc, char** argv) {
    CLI::App app{
        "Create, copy, combine and delete files. Writes are atomic; every run can emit structured logs.",
        "fileops"};
    app.set_version_flag("--version", FILEOPS_VERSION_STRING);
    app.require_subcommand(1);
    app.fallthrough(true);  // global options may follow the subcommand

    GlobalOptions options;
    app.add_option(
           "--log-level",
           options.log_level,
           "Minimum log level written to stderr (Trace, Debug, Information, Warning, Error, Critical, None)")
        ->envname("FILEOPS_LOG_LEVEL")
        ->default_str("None")
        ->check(CLI::IsMember({"Trace", "Debug", "Information", "Warning", "Error", "Critical", "None"},
                              CLI::ignore_case));
    app.add_option("--log-format", options.log_format, "Log format: text or json")
        ->envname("FILEOPS_LOG_FORMAT")
        ->default_str("text")
        ->check(CLI::IsMember({"text", "json"}, CLI::ignore_case));
    app.add_flag("-v,--verbose", options.verbose, "Shorthand for --log-level Information");
    app.footer(
        "Set OTEL_EXPORTER_OTLP_ENDPOINT to export a span, metrics and log records per run over OTLP/HTTP.");

    Action action;

    {
        auto* create = app.add_subcommand("create", "Create a file, empty or with the given text.");
        auto path = std::make_shared<std::string>();
        auto text = std::make_shared<std::optional<std::string>>();
        auto force = std::make_shared<bool>(false);
        create->add_option("path", *path, "File to create.")->required();
        create->add_option(
            "-t,--text",
            *text,
            "Initial content, written as UTF-8 exactly as given (no trailing newline is added).");
        create->add_flag("-f,--force", *force, "Overwrite the file if it already exists.");
        create->callback([=, &action] {
            action = [=](FileOperations& ops) {
                return ops.create(
                    *path, *text ? std::optional<std::string_view>{**text} : std::nullopt, *force);
            };
        });
    }
    {
        auto* copy = app.add_subcommand("copy", "Copy a file to a new path, preserving permissions.");
        auto source = std::make_shared<std::string>();
        auto destination = std::make_shared<std::string>();
        auto force = std::make_shared<bool>(false);
        copy->add_option("source", *source, "Existing file to copy.")->required();
        copy->add_option("destination", *destination, "Path of the new copy.")->required();
        copy->add_flag("-f,--force", *force, "Overwrite the destination if it already exists.");
        copy->callback([=, &action] {
            action = [=](FileOperations& ops) { return ops.copy(*source, *destination, *force); };
        });
    }
    {
        auto* combine = app.add_subcommand("combine", "Concatenate two files into a third.");
        auto first = std::make_shared<std::string>();
        auto second = std::make_shared<std::string>();
        auto output = std::make_shared<std::string>();
        auto force = std::make_shared<bool>(false);
        combine->add_option("first", *first, "First input file.")->required();
        combine->add_option("second", *second, "Second input file, appended after the first.")->required();
        combine
            ->add_option("output", *output, "File to write. May be one of the inputs when --force is given.")
            ->required();
        combine->add_flag("-f,--force", *force, "Overwrite the output if it already exists.");
        combine->callback([=, &action] {
            action = [=](FileOperations& ops) { return ops.combine(*first, *second, *output, *force); };
        });
    }
    {
        auto* del = app.add_subcommand("delete", "Delete a file.");
        auto path = std::make_shared<std::string>();
        auto force = std::make_shared<bool>(false);
        del->add_option("path", *path, "File to delete. Directories are refused.")->required();
        del->add_flag("-f,--force", *force, "Succeed even if the file does not exist (like rm -f).");
        del->callback(
            [=, &action] { action = [=](FileOperations& ops) { return ops.remove(*path, *force); }; });
    }

    try {
        app.parse(argc, argv);
        options.log_level_given = app.count("--log-level") > 0;
    } catch (const CLI::Success& e) {
        return app.exit(e);  // --help and --version: text on stdout, exit 0
    } catch (const CLI::ParseError& e) {
        // Usage errors follow the same contract as every other failure: `fileops: <message>` lines on
        // stderr, exit 1, nothing on stdout.
        std::cerr << "fileops: " << e.what() << '\n' << "fileops: run 'fileops --help' for usage\n";
        return fileops::to_int(ExitCode::UsageError);
    }

    return run(options, action);
}

int main(int argc, char** argv) {
    // Nothing may escape main: an exception here (out of memory, a CLI11 setup error) would end the
    // process with a signal instead of the documented internal-error exit code.
    try {
        return run_main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "fileops: unexpected error: " << e.what() << '\n';
        return fileops::to_int(ExitCode::InternalError);
    } catch (...) {
        std::cerr << "fileops: unexpected error\n";
        return fileops::to_int(ExitCode::InternalError);
    }
}
