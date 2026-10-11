#include "fileops/error.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <string>

#include <gtest/gtest.h>

#include <sys/stat.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

class CreateTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(CreateTest, CreatesEmptyFileWhenNoTextGiven) {
    const auto path = dir_.file("empty");

    const Result result = ops_.sut.create(path);

    EXPECT_EQ(result.bytes, 0U);
    EXPECT_EQ(read_file(path), "");
}

TEST_F(CreateTest, WritesTextExactlyAsGivenWithoutTrailingNewline) {
    const auto path = dir_.file("hello.txt");

    const Result result = ops_.sut.create(path, "héllo");

    EXPECT_EQ(result.bytes, 6U);  // é is two bytes in UTF-8
    EXPECT_EQ(read_file(path), "héllo");
    EXPECT_EQ(field<std::int64_t>(ops_.logger.record(events::file_created), "Bytes"), 6);
}

TEST_F(CreateTest, RefusesToOverwriteExistingFileByDefault) {
    const auto path = dir_.write_file("x", "keep");

    try {
        ops_.sut.create(path, "new");
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::AlreadyExists);
        EXPECT_NE(std::string{e.what()}.find("use --force"), std::string::npos);
    }
    EXPECT_EQ(read_file(path), "keep");
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(CreateTest, OverwritesWhenForced) {
    const auto path = dir_.write_file("x", "old");

    ops_.sut.create(path, "new", true);

    EXPECT_EQ(read_file(path), "new");
}

TEST_F(CreateTest, OverwriteKeepsTheExistingFilesMode) {
    const auto path = dir_.write_file("secret", "old");
    ASSERT_EQ(::chmod(path.c_str(), 0600), 0);

    ops_.sut.create(path, "new", true);

    EXPECT_EQ(read_file(path), "new");
    EXPECT_EQ(permission_bits(path), 0600U);
}

TEST_F(CreateTest, FailsWithNotFoundWhenDirectoryIsMissing) {
    const auto path = dir_.path() / "missing-dir" / "file";

    try {
        ops_.sut.create(path);
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::NotFound);
    }
    EXPECT_EQ(field<std::string>(ops_.logger.record(events::operation_failed), "ExitCode"), "NotFound");
}

TEST_F(CreateTest, MapsPermissionDeniedToIoError) {
    FILEOPS_SKIP_IF_ROOT();
    ASSERT_EQ(::chmod(dir_.path().c_str(), 0500), 0);

    try {
        ops_.sut.create(dir_.file("nope"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::IoError);
        EXPECT_NE(std::string{e.what()}.find("permission denied"), std::string::npos);
    }
}

TEST_F(CreateTest, EmptyPathIsAUsageError) {
    try {
        ops_.sut.create("");
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
        EXPECT_STREQ(e.what(), "path must not be empty");
    }
}

TEST_F(CreateTest, RefusesPathThatIsADirectory) {
    try {
        ops_.sut.create(dir_.path());
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
    }
}

TEST_F(CreateTest, AcceptsNamesCloseToTheFileSystemLimit) {
    // 240 bytes is a legal name; the temp file's decoration must not push it past 255.
    const auto path = dir_.file(std::string(240, 'n'));

    const Result result = ops_.sut.create(path, "ok");

    EXPECT_EQ(result.bytes, 2U);
    EXPECT_EQ(read_file(path), "ok");
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(CreateTest, TruncatesMultiByteNamesOnACharacterBoundary) {
    // 80 three-byte characters = 240 bytes; the temp name must cut between characters, not inside one.
    std::string name;
    for (int i = 0; i < 80; ++i) {
        name += "\xE2\x82\xAC";  // €
    }
    const auto path = dir_.file(name);

    const Result result = ops_.sut.create(path, "ok");

    EXPECT_EQ(result.bytes, 2U);
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

}  // namespace
}  // namespace fileops::test
