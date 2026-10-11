// The OpenTelemetry mapping, driven with in-memory exporters: span names, attributes and status,
// and the three instruments with their tags. This is the executable form of the "Traces" and
// "Metrics" tables in docs/OBSERVABILITY.md.

#include "fileops/error.hpp"

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h>
#include <opentelemetry/exporters/memory/in_memory_span_exporter_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>

#include "collecting_logger.hpp"
#include "otel.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace sdk = opentelemetry::sdk;
namespace memory = opentelemetry::exporter::memory;
namespace nostd = opentelemetry::nostd;

class OtelMappingTest : public ::testing::Test {
public:
    OtelMappingTest(const OtelMappingTest&) = delete;
    OtelMappingTest& operator=(const OtelMappingTest&) = delete;
    OtelMappingTest(OtelMappingTest&&) = delete;
    OtelMappingTest& operator=(OtelMappingTest&&) = delete;

    ~OtelMappingTest() override {
        observer_.reset();
        meters_->Shutdown();
        tracers_->Shutdown();
    }

protected:
    OtelMappingTest() {
        spans_ = std::make_shared<memory::InMemorySpanData>(100);
        tracers_ = sdk::trace::TracerProviderFactory::Create(sdk::trace::SimpleSpanProcessorFactory::Create(
            memory::InMemorySpanExporterFactory::Create(spans_)));
        metrics_ = std::make_shared<memory::SimpleAggregateInMemoryMetricData>();
        sdk::metrics::PeriodicExportingMetricReaderOptions options;
        options.export_interval_millis = std::chrono::milliseconds(3600'000);
        options.export_timeout_millis = std::chrono::milliseconds(1000);
        meters_ = sdk::metrics::MeterProviderFactory::Create();
        meters_->AddMetricReader(sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
            memory::InMemoryMetricExporterFactory::Create(metrics_), options));

        observer_ = cli::otel::make_observer(
            nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
                std::shared_ptr<opentelemetry::trace::TracerProvider>(tracers_.get(), [](auto*) {})),
            nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(
                std::shared_ptr<opentelemetry::metrics::MeterProvider>(meters_.get(), [](auto*) {})),
            "1.2.3");
    }

    void flush_metrics() { meters_->ForceFlush(); }

    TempDirectory dir_;
    telemetry::NullLogger logger_;
    std::shared_ptr<memory::InMemorySpanData> spans_;
    std::shared_ptr<memory::SimpleAggregateInMemoryMetricData> metrics_;
    std::unique_ptr<sdk::trace::TracerProvider> tracers_;
    std::unique_ptr<sdk::metrics::MeterProvider> meters_;
    std::unique_ptr<telemetry::Observer> observer_;
};

std::string attribute(const sdk::trace::SpanData& span, const std::string& key) {
    const auto& attributes = span.GetAttributes();
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return "<missing>";
    }
    if (const auto* s = opentelemetry::nostd::get_if<std::string>(&it->second)) {
        return *s;
    }
    if (const auto* i = opentelemetry::nostd::get_if<std::int64_t>(&it->second)) {
        return std::to_string(*i);
    }
    if (const auto* b = opentelemetry::nostd::get_if<bool>(&it->second)) {
        return *b ? "true" : "false";
    }
    return "<other type>";
}

TEST_F(OtelMappingTest, SuccessfulCopyProducesAnOkSpanWithTheDocumentedTags) {
    FileOperations ops(logger_, *observer_);
    const auto source = dir_.write_file("a", "xyz");

    ops.copy(source, dir_.file("b"));

    const auto spans = spans_->GetSpans();
    ASSERT_EQ(spans.size(), 1U);
    const auto& span = *spans.front();
    EXPECT_EQ(span.GetName(), "fileops.copy");
    EXPECT_EQ(span.GetStatus(), opentelemetry::trace::StatusCode::kOk);
    EXPECT_EQ(attribute(span, "fileops.operation"), "copy");
    EXPECT_EQ(attribute(span, "file.source"), source.string());
    EXPECT_EQ(attribute(span, "file.destination"), dir_.file("b").string());
    EXPECT_EQ(attribute(span, "file.path"), dir_.file("b").string());
    EXPECT_EQ(attribute(span, "file.size"), "3");
    EXPECT_EQ(attribute(span, "fileops.overwrite"), "false");
    EXPECT_EQ(attribute(span, "fileops.exit_code"), "0");
    EXPECT_EQ(attribute(span, "error.type"), "<missing>");
    EXPECT_EQ(span.GetInstrumentationScope().GetName(), "fileops");
    EXPECT_EQ(span.GetInstrumentationScope().GetVersion(), "1.2.3");
}

TEST_F(OtelMappingTest, FailedOperationProducesAnErrorSpanWithTheMessage) {
    FileOperations ops(logger_, *observer_);

    EXPECT_THROW(ops.remove(dir_.file("missing")), Error);

    const auto spans = spans_->GetSpans();
    ASSERT_EQ(spans.size(), 1U);
    const auto& span = *spans.front();
    EXPECT_EQ(span.GetName(), "fileops.delete");
    EXPECT_EQ(span.GetStatus(), opentelemetry::trace::StatusCode::kError);
    EXPECT_NE(std::string{span.GetDescription()}.find("file not found"), std::string::npos);
    EXPECT_EQ(attribute(span, "fileops.exit_code"), "2");
    EXPECT_EQ(attribute(span, "error.type"), "fileops::Error");
}

TEST_F(OtelMappingTest, RecordsTheThreeInstrumentsTaggedByOperationAndOutcome) {
    FileOperations ops(logger_, *observer_);
    ops.create(dir_.file("a"), "12345");
    EXPECT_THROW(ops.create(dir_.file("a")), Error);  // already exists
    flush_metrics();

    // instrument -> set of "k=v;" tag strings seen
    const auto tags_of = [&](const std::string& instrument) {
        std::set<std::string> seen;
        for (const auto& [attributes, point] : metrics_->Get("fileops", instrument)) {
            std::string tags;
            for (const auto& [k, v] : attributes) {
                tags += k + "=" + opentelemetry::nostd::get<std::string>(v) + ";";
            }
            seen.insert(tags);
        }
        return seen;
    };
    EXPECT_TRUE(tags_of("fileops.operations").contains("operation=create;outcome=success;"));
    EXPECT_TRUE(tags_of("fileops.operations").contains("operation=create;outcome=alreadyexists;"));
    EXPECT_TRUE(tags_of("fileops.operation.duration").contains("operation=create;outcome=success;"));
    EXPECT_TRUE(tags_of("fileops.operation.duration").contains("operation=create;outcome=alreadyexists;"));
    EXPECT_TRUE(tags_of("fileops.bytes").contains("operation=create;outcome=success;"));
    EXPECT_FALSE(
        tags_of("fileops.bytes").contains("operation=create;outcome=alreadyexists;"));  // no bytes on failure
}

}  // namespace
}  // namespace fileops::test
