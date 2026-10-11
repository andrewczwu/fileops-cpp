#include "fileops/atomic_file.hpp"

#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "temp_directory.hpp"

namespace fileops::test {
namespace {

class AtomicFileTest : public ::testing::Test {
protected:
    TempDirectory dir_;
};

TEST_F(AtomicFileTest, LeavesNoTempFileAndNoDestinationWhenWriterThrows) {
    const auto destination = dir_.file("out");

    EXPECT_THROW(atomic_file::write(destination, false, [](int) { throw std::logic_error("boom"); }),
                 std::logic_error);

    EXPECT_FALSE(std::filesystem::exists(destination));
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(AtomicFileTest, KeepsOriginalContentWhenWriterThrowsDuringOverwrite) {
    const auto destination = dir_.write_file("out", "original");

    EXPECT_THROW(atomic_file::write(destination,
                                    true,
                                    [](int fd) {
                                        atomic_file::write_all(fd, "partial", 7);
                                        throw std::logic_error("boom");
                                    }),
                 std::logic_error);

    EXPECT_EQ(read_file(destination), "original");
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(AtomicFileTest, ReturnsBytesWritten) {
    const std::vector<char> payload(1234, 'z');

    const auto bytes = atomic_file::write(
        dir_.file("out"), false, [&](int fd) { atomic_file::write_all(fd, payload.data(), payload.size()); });

    EXPECT_EQ(bytes, 1234U);
}

TEST_F(AtomicFileTest, TempFileIsCreatedInDestinationDirectory) {
    // Rename is only atomic within one file system, so the temp file must live beside the destination.
    std::vector<std::filesystem::path> observed;

    atomic_file::write(dir_.file("out"), false, [&](int) { observed = dir_.leftover_temp_files(); });

    ASSERT_EQ(observed.size(), 1U);
    EXPECT_EQ(observed.front().parent_path(), dir_.path());
    EXPECT_TRUE(observed.front().filename().string().starts_with(".out."));
}

TEST_F(AtomicFileTest, DoesNotReplaceAFileThatAppearsBetweenCheckAndCommit) {
    const auto destination = dir_.file("out");

    EXPECT_ANY_THROW(atomic_file::write(destination, false, [&](int) {
        dir_.write_file("out", "someone else");  // races the writer
    }));

    EXPECT_EQ(read_file(destination), "someone else");
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

}  // namespace
}  // namespace fileops::test
