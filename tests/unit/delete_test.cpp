#include "fileops/error.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <gtest/gtest.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

class DeleteTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(DeleteTest, DeletesFileAndReportsItsSize) {
    const auto path = dir_.write_file("gone", "12345");

    const Result result = ops_.sut.remove(path);

    EXPECT_EQ(result.bytes, 5U);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_EQ(field<std::int64_t>(ops_.logger.record(events::file_deleted), "Bytes"), 5);
}

TEST_F(DeleteTest, FailsWithNotFoundWhenFileMissing) {
    try {
        ops_.sut.remove(dir_.file("missing"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::NotFound);
    }
    EXPECT_FALSE(ops_.logger.has(events::delete_skipped_missing));
}

TEST_F(DeleteTest, SucceedsWithWarningWhenMissingAndIgnoreMissing) {
    const Result result = ops_.sut.remove(dir_.file("missing"), true);

    EXPECT_EQ(result.bytes, 0U);
    const auto& skipped = ops_.logger.record(events::delete_skipped_missing);
    EXPECT_EQ(skipped.level, telemetry::LogLevel::Warning);
    EXPECT_FALSE(ops_.logger.has(events::file_deleted));
    EXPECT_TRUE(ops_.logger.has(events::operation_completed));
}

TEST_F(DeleteTest, RefusesToDeleteADirectory) {
    try {
        ops_.sut.remove(dir_.path());
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
    }
    EXPECT_TRUE(std::filesystem::exists(dir_.path()));
}

TEST_F(DeleteTest, DeletesADanglingSymlink) {
    const auto link = dir_.file("dead");
    std::filesystem::create_symlink(dir_.file("never-existed"), link);

    ops_.sut.remove(link);

    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(link)));
    EXPECT_TRUE(ops_.logger.has(events::file_deleted));
}

TEST_F(DeleteTest, DeletesASymlinkToADirectoryButNotTheDirectory) {
    std::filesystem::create_directory(dir_.file("sub"));
    const auto link = dir_.file("sub-link");
    std::filesystem::create_directory_symlink(dir_.file("sub"), link);

    ops_.sut.remove(link);

    EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(link)));
    EXPECT_TRUE(std::filesystem::is_directory(dir_.file("sub")));
}

TEST_F(DeleteTest, EmptyPathIsAUsageError) {
    try {
        ops_.sut.remove("");
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
    }
}

}  // namespace
}  // namespace fileops::test
