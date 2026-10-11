#include "otel.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <opentelemetry/common/attribute_value.h>
#include <opentelemetry/common/key_value_iterable_view.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/logs/logger.h>
#include <opentelemetry/logs/severity.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/sdk/common/global_log_handler.h>
#include <opentelemetry/sdk/logs/logger_provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/tracer.h>

namespace fileops::cli::otel {
namespace {

namespace api_trace = opentelemetry::trace;
namespace api_metrics = opentelemetry::metrics;
namespace api_logs = opentelemetry::logs;
namespace nostd = opentelemetry::nostd;
namespace sdk = opentelemetry::sdk;
using opentelemetry::common::AttributeValue;
using telemetry::OperationContext;
using telemetry::OperationOutcome;

using Attributes = std::vector<std::pair<std::string, AttributeValue>>;

opentelemetry::common::KeyValueIterableView<Attributes> view(const Attributes& attributes) {
    return opentelemetry::common::KeyValueIterableView<Attributes>{attributes};
}

/// One span plus the instrument measurements it ends with. The span is the active one while the
/// operation runs so log records exported in the meantime carry its trace and span ids.
class Scope final : public telemetry::OperationScope {
public:
    Scope(nostd::shared_ptr<api_trace::Span> span,
          api_trace::Scope active,
          std::string operation,
          api_metrics::Counter<std::uint64_t>& operations,
          api_metrics::Histogram<double>& duration,
          api_metrics::Counter<std::uint64_t>& bytes)
        : span_(std::move(span)),
          active_(std::move(active)),
          operation_(std::move(operation)),
          operations_(operations),
          duration_(duration),
          bytes_(bytes) {}

    void finish(const OperationOutcome& outcome) override {
        span_->SetAttribute("file.size", static_cast<std::int64_t>(outcome.bytes));
        span_->SetAttribute("fileops.exit_code", static_cast<std::int64_t>(to_int(outcome.exit_code)));
        if (outcome.succeeded()) {
            span_->SetStatus(api_trace::StatusCode::kOk);
        } else {
            span_->SetAttribute("error.type", outcome.error_type);
            span_->SetStatus(api_trace::StatusCode::kError, outcome.error_message);
        }
        span_->End();

        const std::string outcome_tag = telemetry::outcome_tag(outcome.exit_code);
        const Attributes tags{{"operation", operation_}, {"outcome", outcome_tag}};
        operations_.Add(1, view(tags));
        duration_.Record(outcome.duration.count(), view(tags), opentelemetry::context::Context{});
        if (outcome.bytes > 0) {
            bytes_.Add(outcome.bytes, view(tags));
        }
    }

private:
    nostd::shared_ptr<api_trace::Span> span_;
    api_trace::Scope active_;
    std::string operation_;
    api_metrics::Counter<std::uint64_t>& operations_;
    api_metrics::Histogram<double>& duration_;
    api_metrics::Counter<std::uint64_t>& bytes_;
};

class OtelObserver final : public telemetry::Observer {
public:
    OtelObserver(const nostd::shared_ptr<api_trace::TracerProvider>& tracers,
                 const nostd::shared_ptr<api_metrics::MeterProvider>& meters,
                 const std::string& version)
        : tracer_(tracers->GetTracer(scope_name, version)),
          meter_(meters->GetMeter(scope_name, version)),
          operations_(
              meter_->CreateUInt64Counter("fileops.operations", "Operations run, by outcome", "{operation}")),
          duration_(meter_->CreateDoubleHistogram("fileops.operation.duration", "Operation duration", "s")),
          bytes_(meter_->CreateUInt64Counter("fileops.bytes", "Bytes written or removed", "By")) {}

    std::unique_ptr<telemetry::OperationScope> start(const OperationContext& context) override {
        const std::string operation{lower_name(context.operation)};
        // AttributeValue holds string views, so every string must outlive StartSpan(): they are
        // kept in `owned`, reserved so the views never move.
        std::vector<std::string> owned;
        owned.reserve(5);
        const auto keep = [&owned](std::string value) -> nostd::string_view {
            owned.push_back(std::move(value));
            return owned.back();
        };
        Attributes attributes{{"fileops.operation", keep(operation)},
                              {"file.path", keep(context.path.string())}};
        if (context.source) {
            attributes.emplace_back("file.source", keep(context.source->string()));
        }
        if (context.source2) {
            attributes.emplace_back("file.source2", keep(context.source2->string()));
        }
        if (context.destination) {
            attributes.emplace_back("file.destination", keep(context.destination->string()));
        }
        if (context.overwrite) {
            attributes.emplace_back("fileops.overwrite", *context.overwrite);
        }
        auto span = tracer_->StartSpan("fileops." + operation, view(attributes));
        auto active = tracer_->WithActiveSpan(span);
        return std::make_unique<Scope>(
            std::move(span), std::move(active), operation, *operations_, *duration_, *bytes_);
    }

private:
    nostd::shared_ptr<api_trace::Tracer> tracer_;
    nostd::shared_ptr<api_metrics::Meter> meter_;
    nostd::unique_ptr<api_metrics::Counter<std::uint64_t>> operations_;
    nostd::unique_ptr<api_metrics::Histogram<double>> duration_;
    nostd::unique_ptr<api_metrics::Counter<std::uint64_t>> bytes_;
};

api_logs::Severity severity(telemetry::LogLevel level) noexcept {
    switch (level) {
        case telemetry::LogLevel::Trace:
            return api_logs::Severity::kTrace;
        case telemetry::LogLevel::Debug:
            return api_logs::Severity::kDebug;
        case telemetry::LogLevel::Information:
            return api_logs::Severity::kInfo;
        case telemetry::LogLevel::Warning:
            return api_logs::Severity::kWarn;
        case telemetry::LogLevel::Error:
            return api_logs::Severity::kError;
        case telemetry::LogLevel::Critical:
            return api_logs::Severity::kFatal;
        case telemetry::LogLevel::None:
            return api_logs::Severity::kInvalid;
    }
    return api_logs::Severity::kInvalid;
}

/// Forwards the structured log records to the logs API: message as body, State fields plus the
/// event id, name and category as attributes, severity from the level.
class LogSink final : public telemetry::Logger {
public:
    LogSink(const nostd::shared_ptr<api_logs::LoggerProvider>& loggers,
            const std::string& version,
            telemetry::LogLevel minimum)
        : logger_(loggers->GetLogger(scope_name, scope_name, version)), minimum_(minimum) {}

    [[nodiscard]] bool enabled(telemetry::LogLevel level) const noexcept override {
        return minimum_ != telemetry::LogLevel::None && level >= minimum_;
    }

    void log(const telemetry::LogRecord& record) override {
        Attributes attributes{{"EventId", static_cast<std::int64_t>(record.event_id)},
                              {"EventName", record.event_name},
                              {"Category", record.category}};
        for (const auto& field : record.state) {
            std::visit([&](const auto& value) { attributes.emplace_back(field.name, value); }, field.value);
        }
        logger_->EmitLogRecord(severity(record.level), record.message, view(attributes), record.timestamp);
    }

private:
    nostd::shared_ptr<api_logs::Logger> logger_;
    telemetry::LogLevel minimum_;
};

sdk::resource::Resource service_resource(const std::string& version) {
    // Create() merges in OTEL_RESOURCE_ATTRIBUTES and OTEL_SERVICE_NAME from the environment.
    return sdk::resource::Resource::Create({{"service.name", service_name}, {"service.version", version}});
}

}  // namespace

std::unique_ptr<telemetry::Observer> make_observer(
    const nostd::shared_ptr<api_trace::TracerProvider>& tracers,
    const nostd::shared_ptr<api_metrics::MeterProvider>& meters,
    const std::string& version) {
    return std::make_unique<OtelObserver>(tracers, meters, version);
}

std::unique_ptr<telemetry::Logger> make_log_sink(const nostd::shared_ptr<api_logs::LoggerProvider>& loggers,
                                                 const std::string& version,
                                                 telemetry::LogLevel minimum) {
    return std::make_unique<LogSink>(loggers, version, minimum);
}

struct Runtime::Providers {
    std::unique_ptr<sdk::trace::TracerProvider> tracers;
    std::unique_ptr<sdk::metrics::MeterProvider> meters;
    std::unique_ptr<sdk::logs::LoggerProvider> loggers;
    nostd::shared_ptr<api_trace::TracerProvider> tracer_api;
    nostd::shared_ptr<api_metrics::MeterProvider> meter_api;
    nostd::shared_ptr<api_logs::LoggerProvider> logger_api;
};

std::unique_ptr<Runtime> Runtime::start_if_configured(const std::string& version) {
    const char* endpoint = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT");  // NOLINT(concurrency-mt-unsafe)
    if (endpoint == nullptr || *endpoint == '\0') {
        return nullptr;
    }

    // The SDK reports its own problems (an unreachable endpoint, a rejected payload) through an
    // internal logger that writes to stderr. Errors are worth seeing; warnings would only add noise
    // to a stream whose contract is "log records, then the summary line".
    sdk::common::internal_log::GlobalLogHandler::SetLogLevel(sdk::common::internal_log::LogLevel::Error);

    // The exporter options read OTEL_EXPORTER_OTLP_* (endpoint, headers, timeout, compression)
    // from the environment; nothing is hard-coded here.
    auto providers = std::make_unique<Providers>();
    const auto resource = service_resource(version);

    auto span_exporter = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create();
    providers->tracers = sdk::trace::TracerProviderFactory::Create(
        sdk::trace::SimpleSpanProcessorFactory::Create(std::move(span_exporter)), resource);

    auto metric_exporter = opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create();
    sdk::metrics::PeriodicExportingMetricReaderOptions reader_options;
    // One export at shutdown is what a short-lived process needs; the interval only matters if a
    // run outlives it.
    reader_options.export_interval_millis = std::chrono::milliseconds(60'000);
    reader_options.export_timeout_millis = std::chrono::milliseconds(10'000);
    providers->meters = sdk::metrics::MeterProviderFactory::Create();
    providers->meters->AddMetricReader(sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
        std::move(metric_exporter), reader_options));

    auto log_exporter = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create();
    providers->loggers = sdk::logs::LoggerProviderFactory::Create(
        sdk::logs::SimpleLogRecordProcessorFactory::Create(std::move(log_exporter)), resource);

    // Non-owning API handles over the SDK providers (nostd::shared_ptr adopts a std::shared_ptr).
    providers->tracer_api = nostd::shared_ptr<api_trace::TracerProvider>(
        std::shared_ptr<api_trace::TracerProvider>(providers->tracers.get(), [](auto*) {}));
    providers->meter_api = nostd::shared_ptr<api_metrics::MeterProvider>(
        std::shared_ptr<api_metrics::MeterProvider>(providers->meters.get(), [](auto*) {}));
    providers->logger_api = nostd::shared_ptr<api_logs::LoggerProvider>(
        std::shared_ptr<api_logs::LoggerProvider>(providers->loggers.get(), [](auto*) {}));

    return std::unique_ptr<Runtime>(new Runtime(std::move(providers), version));
}

Runtime::Runtime(std::unique_ptr<Providers> providers, const std::string& version)
    : providers_(std::move(providers)),
      observer_(make_observer(providers_->tracer_api, providers_->meter_api, version)),
      version_(version) {}

Runtime::~Runtime() {
    shutdown();
}

std::unique_ptr<telemetry::Logger> Runtime::log_sink(telemetry::LogLevel minimum) {
    return make_log_sink(providers_->logger_api, version_, minimum);
}

void Runtime::shutdown() noexcept {
    if (shut_down_) {
        return;
    }
    shut_down_ = true;
    // Order matters: flush data before the span that logs refer to is gone, and drop the observer
    // (which holds instruments) before the meter provider.
    providers_->loggers->ForceFlush();
    providers_->tracers->ForceFlush();
    providers_->meters->ForceFlush();
    providers_->loggers->Shutdown();
    providers_->tracers->Shutdown();
    observer_.reset();
    // The meter provider shuts down in its destructor; calling Shutdown() here as well makes the
    // SDK warn about a second shutdown on stderr.
    providers_.reset();
}

}  // namespace fileops::cli::otel
