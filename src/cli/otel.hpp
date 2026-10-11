#pragma once

// OpenTelemetry wiring for the command line: one span per operation, three instruments, and an
// OTLP log sink, all exported over OTLP/HTTP when OTEL_EXPORTER_OTLP_ENDPOINT is set. The mapping
// from the core's Observer/Logger facts to OpenTelemetry lives here and is unit-tested with
// in-memory exporters; the exporters themselves are configured by the standard OTEL_* variables.

#include "fileops/telemetry/log.hpp"
#include "fileops/telemetry/observer.hpp"

#include <memory>
#include <string>

#include <opentelemetry/logs/logger_provider.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/tracer_provider.h>

namespace fileops::cli::otel {

/// Instrumentation scope shared by the tracer, meter and logger.
inline constexpr const char* scope_name = "fileops";

/// Service resource attributes.
inline constexpr const char* service_name = "fileops";

/// Observer that records one span (`fileops.<operation>`) and the three instruments per operation.
std::unique_ptr<telemetry::Observer> make_observer(
    const opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>& tracers,
    const opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>& meters,
    const std::string& version);

/// Logger that forwards records at or above `minimum` to the OpenTelemetry logs API.
std::unique_ptr<telemetry::Logger> make_log_sink(
    const opentelemetry::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>& loggers,
    const std::string& version,
    telemetry::LogLevel minimum);

/// The SDK providers with OTLP/HTTP exporters behind them, alive for the duration of one run.
class Runtime {
public:
    /// Creates the providers when OTEL_EXPORTER_OTLP_ENDPOINT is set and non-empty; nullptr otherwise.
    static std::unique_ptr<Runtime> start_if_configured(const std::string& version);

    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    [[nodiscard]] telemetry::Observer& observer() noexcept { return *observer_; }
    /// A log sink exporting at or above `minimum`.
    std::unique_ptr<telemetry::Logger> log_sink(telemetry::LogLevel minimum);

    /// Flushes and shuts the providers down. Called by the destructor if not called before.
    void shutdown() noexcept;

private:
    struct Providers;
    explicit Runtime(std::unique_ptr<Providers> providers, const std::string& version);

    std::unique_ptr<Providers> providers_;
    std::unique_ptr<telemetry::Observer> observer_;
    std::string version_;
    bool shut_down_ = false;
};

}  // namespace fileops::cli::otel
