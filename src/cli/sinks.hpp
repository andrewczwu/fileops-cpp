#pragma once

// Log sinks for the command line: human-readable text or one JSON object per line, always on
// stderr so stdout stays reserved for command results.

#include "fileops/telemetry/log.hpp"

#include <memory>
#include <ostream>

namespace fileops::cli {

enum class LogFormat { Text, Json };

/// Creates a sink writing records at or above `minimum` to `out`; LogLevel::None disables it.
std::unique_ptr<telemetry::Logger> make_sink(LogFormat format,
                                             telemetry::LogLevel minimum,
                                             std::ostream& out);

}  // namespace fileops::cli
