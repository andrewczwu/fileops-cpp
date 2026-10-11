#include "fileops/atomic_file.hpp"
#include "fileops/error.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <sys/stat.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

std::vector<unsigned char> random_bytes(std::size_t count) {
    std::mt19937 rng(12345);  // NOLINT(cert-msc32-c,cert-msc51-cpp) deterministic test data
    std::vector<unsigned char> bytes(count);
    for (auto& b : bytes) {
        b = static_cast<unsigned char>(rng() & 0xFFU);
    }
    return bytes;
}

class CopyTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(CopyTest, CopiesContentByteForByte) {
    const auto source = dir_.write_file("a", "line1\nline2\r\n\ttab");
    const auto destination = dir_.file("b");

    const Result result = ops_.sut.copy(source, destination);

    EXPECT_EQ(result.bytes, 17U);
    EXPECT_EQ(read_file(destination), read_file(source));
    EXPECT_EQ(read_file(source), "line1\nline2\r\n\ttab");
}

TEST_F(CopyTest, CopiesEmptyFile) {
    const auto source = dir_.write_file("empty", "");

    const Result result = ops_.sut.copy(source, dir_.file("copy"));

    EXPECT_EQ(result.bytes, 0U);
    EXPECT_EQ(read_file(dir_.file("copy")), "");
}

TEST_F(CopyTest, HandlesBinaryContent) {
    // Spans several copy buffers plus a tail, with every byte value present.
    const auto content = random_bytes((2 * 1024 * 1024) + 7);
    const auto source = dir_.write_file("src.bin", content);

    const Result result = ops_.sut.copy(source, dir_.file("dst.bin"));

    EXPECT_EQ(result.bytes, content.size());
    const std::string copied = read_file(dir_.file("dst.bin"));
    ASSERT_EQ(copied.size(), content.size());
    EXPECT_TRUE(std::equal(copied.begin(), copied.end(), content.begin(), [](char c, unsigned char u) {
        return static_cast<unsigned char>(c) == u;
    }));
}

TEST_F(CopyTest, PreservesUnixPermissions) {
    const auto source = dir_.write_file("script", "#!/bin/sh\n");
    ASSERT_EQ(::chmod(source.c_str(), 0750), 0);

    ops_.sut.copy(source, dir_.file("copy"));

    EXPECT_EQ(permission_bits(dir_.file("copy")), 0750U);
}

TEST_F(CopyTest, TempFileCarriesTheSourceModeBeforeAnyContentIsWritten) {
    // A private source must not be world-readable even for the instant between write and rename.
    mode_t observed = 0;
    atomic_file::write(
        dir_.file("copy"),
        false,
        [&](int fd) {
            struct stat st {};
            ASSERT_EQ(::fstat(fd, &st), 0);
            observed = st.st_mode & 0777U;
        },
        0600);

    EXPECT_EQ(observed, 0600U);
    EXPECT_EQ(permission_bits(dir_.file("copy")), 0600U);
}

TEST_F(CopyTest, RefusesToOverwriteExistingFileByDefault) {
    const auto source = dir_.write_file("a", "new");
    const auto destination = dir_.write_file("b", "keep");

    try {
        ops_.sut.copy(source, destination);
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::AlreadyExists);
    }
    EXPECT_EQ(read_file(destination), "keep");
}

TEST_F(CopyTest, OverwritesWhenForced) {
    const auto source = dir_.write_file("a", "new");
    const auto destination = dir_.write_file("b", "old");

    ops_.sut.copy(source, destination, true);

    EXPECT_EQ(read_file(destination), "new");
}

TEST_F(CopyTest, FailsWithNotFoundWhenSourceMissing) {
    try {
        ops_.sut.copy(dir_.file("missing"), dir_.file("b"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::NotFound);
    }
    EXPECT_FALSE(std::filesystem::exists(dir_.file("b")));
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(CopyTest, RejectsDirectoryAsSource) {
    try {
        ops_.sut.copy(dir_.path(), dir_.file("b"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
    }
}

TEST_F(CopyTest, CopyOntoItselfIsAUsageError) {
    const auto source = dir_.write_file("a", "x");

    try {
        ops_.sut.copy(source, source, true);
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
        EXPECT_NE(std::string{e.what()}.find("same file"), std::string::npos);
    }
    EXPECT_EQ(read_file(source), "x");
}

TEST_F(CopyTest, EmptySourceOrDestinationIsAUsageError) {
    const auto source = dir_.write_file("a", "x");

    for (const auto& [src, dst, expected] :
         {std::tuple{std::filesystem::path{}, dir_.file("b"), "source must not be empty"},
          std::tuple{source, std::filesystem::path{"  "}, "destination must not be empty"}}) {
        try {
            ops_.sut.copy(src, dst);
            FAIL() << "expected Error";
        } catch (const Error& e) {
            EXPECT_EQ(e.code(), ExitCode::UsageError);
            EXPECT_STREQ(e.what(), expected);
        }
    }
}

TEST_F(CopyTest, LogsSourceDestinationAndByteCount) {
    const auto source = dir_.write_file("a", "abc");

    ops_.sut.copy(source, dir_.file("b"));

    const auto& copied = ops_.logger.record(events::file_copied);
    EXPECT_EQ(field<std::string>(copied, "Source"), source.string());
    EXPECT_EQ(field<std::string>(copied, "Destination"), dir_.file("b").string());
    EXPECT_EQ(field<std::int64_t>(copied, "Bytes"), 3);
}

}  // namespace
}  // namespace fileops::test
