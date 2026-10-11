// The telemetry contract: which records an operation emits, in what order, with which fields. Tests
// here are the executable form of docs/OBSERVABILITY.md.

#include "fileops/error.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <vector>

#include <gtest/gtest.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;
using telemetry::LogLevel;

class TelemetryContractTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(TelemetryContractTest, SuccessfulOperationEmitsStartedThenEventThenCompleted) {
    const auto source = dir_.write_file("a", "xyz");

    ops_.sut.copy(source, dir_.file("b"));

    EXPECT_EQ(
        ops_.logger.event_ids(),
        (std::vector<int>{events::operation_started, events::file_copied, events::operation_completed}));

    const auto& started = ops_.logger.record(events::operation_started);
    EXPECT_EQ(started.level, LogLevel::Debug);
    EXPECT_EQ(started.event_name, "OperationStarted");
    EXPECT_EQ(started.category, "fileops.core.FileOperations");
    EXPECT_EQ(field<std::string>(started, "Operation"), "Copy");
    EXPECT_EQ(field<std::string>(started, "Path"), dir_.file("b").string());

    const auto& completed = ops_.logger.record(events::operation_completed);
    EXPECT_EQ(completed.level, LogLevel::Information);
    EXPECT_EQ(field<std::string>(completed, "Operation"), "Copy");
    EXPECT_EQ(field<std::int64_t>(completed, "Bytes"), 3);
    EXPECT_GE(field<double>(completed, "DurationMs"), 0.0);
    EXPECT_NE(completed.message.find("Copy completed on"), std::string::npos);
}

TEST_F(TelemetryContractTest, FailedOperationEmitsErrorLogWithExitCodeAndReason) {
    try {
        ops_.sut.copy(dir_.file("missing"), dir_.file("b"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        const auto& failed = ops_.logger.record(events::operation_failed);
        EXPECT_EQ(failed.level, LogLevel::Error);
        EXPECT_EQ(field<std::string>(failed, "ExitCode"), "NotFound");
        EXPECT_EQ(field<std::string>(failed, "Reason"), e.what());
        EXPECT_EQ(field<std::string>(failed, "Operation"), "Copy");
    }
    EXPECT_EQ(ops_.logger.event_ids(),
              (std::vector<int>{events::operation_started, events::operation_failed}));
}

TEST_F(TelemetryContractTest, EveryOperationHasItsOwnSuccessEvent) {
    const auto a = dir_.file("a");
    ops_.sut.create(a, "1");
    ops_.sut.copy(a, dir_.file("b"));
    ops_.sut.combine(a, dir_.file("b"), dir_.file("c"));
    ops_.sut.remove(dir_.file("c"));

    for (int id : {events::file_created, events::file_copied, events::files_combined, events::file_deleted}) {
        EXPECT_TRUE(ops_.logger.has(id)) << "missing event " << id;
        EXPECT_FALSE(events::name(id).empty());
    }
}

TEST_F(TelemetryContractTest, EmptyPathIsAUsageErrorWithFailureTelemetry) {
    EXPECT_THROW(ops_.sut.create(""), Error);

    EXPECT_EQ(field<std::string>(ops_.logger.record(events::operation_failed), "ExitCode"), "UsageError");
}

TEST_F(TelemetryContractTest, OperationsStillWorkWhenNobodyIsListening) {
    telemetry::NullLogger silent;
    FileOperations ops(silent);

    const Result result = ops.create(dir_.file("quiet"), "ok");

    EXPECT_EQ(result.bytes, 2U);
    EXPECT_EQ(read_file(dir_.file("quiet")), "ok");
}

TEST_F(TelemetryContractTest, LogLevelNamesRoundTrip) {
    for (LogLevel level : {LogLevel::Trace,
                           LogLevel::Debug,
                           LogLevel::Information,
                           LogLevel::Warning,
                           LogLevel::Error,
                           LogLevel::Critical,
                           LogLevel::None}) {
        EXPECT_EQ(telemetry::parse_log_level(telemetry::to_string(level)), level);
    }
    EXPECT_EQ(telemetry::parse_log_level("information"), LogLevel::Information);
    EXPECT_EQ(telemetry::parse_log_level("WARNING"), LogLevel::Warning);
    EXPECT_EQ(telemetry::parse_log_level("loud"), std::nullopt);
}

}  // namespace
}  // namespace fileops::test
