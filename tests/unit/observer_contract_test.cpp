// The span and metric contract: what the core reports to its Observer for every operation and
// outcome. docs/OBSERVABILITY.md describes it; these tests enforce it.

#include "fileops/error.hpp"

#include <gtest/gtest.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

class ObserverContractTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(ObserverContractTest, SuccessfulOperationReportsOneOkOutcomeWithBytesAndDuration) {
    const auto source = dir_.write_file("a", "xyz");

    ops_.sut.copy(source, dir_.file("b"));

    const auto& op = ops_.observer.single();
    EXPECT_EQ(op.context.operation, Operation::Copy);
    EXPECT_TRUE(op.outcome.value().succeeded());
    EXPECT_EQ(op.outcome.value().bytes, 3U);
    EXPECT_GE(op.outcome.value().duration.count(), 0.0);
    EXPECT_TRUE(op.outcome.value().error_type.empty());
    EXPECT_TRUE(op.outcome.value().error_message.empty());
}

TEST_F(ObserverContractTest, CopyContextRecordsSourceDestinationAndOverwrite) {
    const auto source = dir_.write_file("a", "x");

    ops_.sut.copy(source, dir_.file("b"), true);

    const auto& ctx = ops_.observer.single().context;
    EXPECT_EQ(ctx.path, dir_.file("b"));
    EXPECT_EQ(ctx.source, source);
    EXPECT_EQ(ctx.destination, dir_.file("b"));
    EXPECT_FALSE(ctx.source2.has_value());
    EXPECT_EQ(ctx.overwrite, true);
}

TEST_F(ObserverContractTest, CombineContextRecordsBothInputsAndTheOutput) {
    const auto a = dir_.write_file("a", "1");
    const auto b = dir_.write_file("b", "2");

    ops_.sut.combine(a, b, dir_.file("out"));

    const auto& ctx = ops_.observer.single().context;
    EXPECT_EQ(ctx.operation, Operation::Combine);
    EXPECT_EQ(ctx.source, a);
    EXPECT_EQ(ctx.source2, b);
    EXPECT_EQ(ctx.destination, dir_.file("out"));
    EXPECT_EQ(ctx.overwrite, false);
}

TEST_F(ObserverContractTest, DeleteContextHasNoOverwriteFlag) {
    const auto path = dir_.write_file("gone", "123");

    ops_.sut.remove(path);

    const auto& op = ops_.observer.single();
    EXPECT_EQ(op.context.operation, Operation::Delete);
    EXPECT_FALSE(op.context.overwrite.has_value());
    EXPECT_EQ(op.outcome.value().bytes, 3U);
}

TEST_F(ObserverContractTest, FailedOperationReportsExitCodeErrorTypeAndMessage) {
    try {
        ops_.sut.copy(dir_.file("missing"), dir_.file("b"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        const auto& outcome = ops_.observer.single().outcome.value();
        EXPECT_FALSE(outcome.succeeded());
        EXPECT_EQ(outcome.exit_code, ExitCode::NotFound);
        EXPECT_EQ(outcome.error_type, "fileops::Error");
        EXPECT_EQ(outcome.error_message, e.what());
        EXPECT_EQ(outcome.bytes, 0U);
    }
}

TEST_F(ObserverContractTest, OutcomeTagsAreSuccessOrTheLowerCaseExitCodeName) {
    EXPECT_EQ(telemetry::outcome_tag(ExitCode::Success), "success");
    EXPECT_EQ(telemetry::outcome_tag(ExitCode::NotFound), "notfound");
    EXPECT_EQ(telemetry::outcome_tag(ExitCode::AlreadyExists), "alreadyexists");
    EXPECT_EQ(telemetry::outcome_tag(ExitCode::IoError), "ioerror");
}

TEST_F(ObserverContractTest, OperationsStillWorkWithoutAnObserver) {
    telemetry::NullLogger silent;
    FileOperations ops(silent);  // default observer: null_observer()

    EXPECT_EQ(ops.create(dir_.file("quiet"), "ok").bytes, 2U);
}

}  // namespace
}  // namespace fileops::test
