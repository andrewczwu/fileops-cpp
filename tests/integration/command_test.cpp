// End-to-end behaviour of each command through the real executable.

#include "fileops/exit_code.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <sys/stat.h>

#include "temp_directory.hpp"
#include "tool.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

class CommandTest : public ::testing::Test {
protected:
    TempDirectory dir_;
};

TEST_F(CommandTest, CreateWritesTextAndLogsTheByteCount) {
    const auto result = Tool::run(dir_.path(), {"create", "hello.txt", "--text", "hello"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(read_file(dir_.file("hello.txt")), "hello");
    EXPECT_EQ(result.log(events::file_created).at("State").at("Bytes"), 5);
}

TEST_F(CommandTest, CreateWithoutTextMakesEmptyFile) {
    const auto result = Tool::run(dir_.path(), {"create", "empty"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(std::filesystem::file_size(dir_.file("empty")), 0U);
}

TEST_F(CommandTest, CreateRefusesExistingFileUnlessForced) {
    dir_.write_file("x", "keep");

    const auto refused = Tool::run(dir_.path(), {"create", "x", "--text", "new"});
    const auto forced = Tool::run(dir_.path(), {"create", "x", "--text", "new", "--force"});

    EXPECT_EQ(refused.exit_code, to_int(ExitCode::AlreadyExists));
    ASSERT_EQ(refused.messages().size(), 1U);
    EXPECT_NE(refused.messages().front().find("use --force"), std::string::npos);
    EXPECT_EQ(forced.exit_code, 0);
    EXPECT_EQ(read_file(dir_.file("x")), "new");
}

TEST_F(CommandTest, CopyPreservesContentOfMultiMegabyteFile) {
    std::mt19937 rng(777);  // NOLINT(cert-msc32-c,cert-msc51-cpp) deterministic test data
    std::string content((4 * 1024 * 1024) + 3, '\0');
    for (auto& c : content) {
        c = static_cast<char>(rng() & 0xFFU);
    }
    dir_.write_file("src.bin", content);

    const auto result = Tool::run(dir_.path(), {"copy", "src.bin", "dst.bin"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(read_file(dir_.file("dst.bin")), content);
    EXPECT_EQ(result.log(events::file_copied).at("State").at("Bytes").get<std::uint64_t>(), content.size());
}

TEST_F(CommandTest, CopyMissingSourceExitsNotFound) {
    const auto result = Tool::run(dir_.path(), {"copy", "missing", "dst"});

    EXPECT_EQ(result.exit_code, to_int(ExitCode::NotFound));
    EXPECT_FALSE(std::filesystem::exists(dir_.file("dst")));
}

TEST_F(CommandTest, CopyPreservesPermissions) {
    const auto source = dir_.write_file("tool.sh", "#!/bin/sh\n");
    ASSERT_EQ(::chmod(source.c_str(), 0755), 0);

    const auto result = Tool::run(dir_.path(), {"copy", "tool.sh", "copy.sh"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(permission_bits(dir_.file("copy.sh")), 0755U);
}

TEST_F(CommandTest, CombineConcatenatesInOrder) {
    dir_.write_file("a", "alpha\n");
    dir_.write_file("b", "beta\n");

    const auto result = Tool::run(dir_.path(), {"combine", "a", "b", "out"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(read_file(dir_.file("out")), "alpha\nbeta\n");
    EXPECT_EQ(result.log(events::files_combined).at("State").at("Bytes"), 11);
}

TEST_F(CommandTest, CombineCanAppendInPlaceWithForce) {
    dir_.write_file("a", "head");
    dir_.write_file("b", "tail");

    const auto result = Tool::run(dir_.path(), {"combine", "a", "b", "a", "--force"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(read_file(dir_.file("a")), "headtail");
}

TEST_F(CommandTest, CombineRefusesToOverwriteWithoutForce) {
    dir_.write_file("a", "1");
    dir_.write_file("b", "2");
    dir_.write_file("out", "keep");

    const auto result = Tool::run(dir_.path(), {"combine", "a", "b", "out"});

    EXPECT_EQ(result.exit_code, to_int(ExitCode::AlreadyExists));
    EXPECT_EQ(read_file(dir_.file("out")), "keep");
}

TEST_F(CommandTest, DeleteRemovesFile) {
    dir_.write_file("gone", "bye");

    const auto result = Tool::run(dir_.path(), {"delete", "gone"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_FALSE(std::filesystem::exists(dir_.file("gone")));
    EXPECT_EQ(result.log(events::file_deleted).at("State").at("Bytes"), 3);
}

TEST_F(CommandTest, DeleteMissingFailsUnlessForced) {
    const auto refused = Tool::run(dir_.path(), {"delete", "missing"});
    const auto forced = Tool::run(dir_.path(), {"delete", "missing", "--force"});

    EXPECT_EQ(refused.exit_code, to_int(ExitCode::NotFound));
    EXPECT_EQ(forced.exit_code, 0);
    EXPECT_EQ(forced.log(events::delete_skipped_missing).at("LogLevel"), "Warning");
}

TEST_F(CommandTest, DeleteRefusesDirectory) {
    std::filesystem::create_directory(dir_.file("sub"));

    const auto result = Tool::run(dir_.path(), {"delete", "sub"});

    EXPECT_EQ(result.exit_code, to_int(ExitCode::UsageError));
    EXPECT_TRUE(std::filesystem::is_directory(dir_.file("sub")));
}

TEST_F(CommandTest, NoTemporaryFilesRemainAfterAnySequence) {
    Tool::run(dir_.path(), {"create", "a", "--text", "1"});
    Tool::run(dir_.path(), {"copy", "a", "b"});
    Tool::run(dir_.path(), {"combine", "a", "b", "c"});
    Tool::run(dir_.path(), {"copy", "missing", "d"});
    Tool::run(dir_.path(), {"create", "a"});  // refused: exists
    Tool::run(dir_.path(), {"delete", "c"});

    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

}  // namespace
}  // namespace fileops::test
