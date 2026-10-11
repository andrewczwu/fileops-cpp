#pragma once

// Log sinks for the command line: human-readable text or one JSON object per line, always on
// stderr so stdout stays reserved for command results.

#include "fileops/telemetry/log.hpp"

#include <memory>
#include <ostream>
#include <vector>

namespace fileops::cli {

enum class LogFormat { Text, Json };

/// Creates a sink writing records at or above `minimum` to `out`; LogLevel::None disables it.
std::unique_ptr<telemetry::Logger> make_sink(LogFormat format,
                                             telemetry::LogLevel minimum,
                                             std::ostream& out);

/// A logger that forwards every record to each of the given sinks; a record is produced when any
/// sink wants it. Used to feed the console and the OTLP export at different levels.
std::unique_ptr<telemetry::Logger> make_tee(std::vector<std::unique_ptr<telemetry::Logger>> sinks);

}  // namespace fileops::cli
