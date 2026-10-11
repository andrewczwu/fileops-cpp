#include "fileops/error.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <string>

#include <gtest/gtest.h>

#include "collecting_logger.hpp"
#include "temp_directory.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

class CombineTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    ObservedOperations ops_;
};

TEST_F(CombineTest, ConcatenatesFirstThenSecondWithoutSeparator) {
    const auto a = dir_.write_file("a", "first");
    const auto b = dir_.write_file("b", "second");

    const Result result = ops_.sut.combine(a, b, dir_.file("out"));

    EXPECT_EQ(result.bytes, 11U);
    EXPECT_EQ(read_file(dir_.file("out")), "firstsecond");
}

TEST_F(CombineTest, CanAppendInPlaceWhenOutputIsAnInputAndForced) {
    const auto a = dir_.write_file("a", "head");
    const auto b = dir_.write_file("b", "tail");

    ops_.sut.combine(a, b, a, true);

    EXPECT_EQ(read_file(a), "headtail");
    EXPECT_EQ(read_file(b), "tail");
}

TEST_F(CombineTest, CanDoubleAFileOntoItself) {
    const auto a = dir_.write_file("a", "ab");

    const Result result = ops_.sut.combine(a, a, a, true);

    EXPECT_EQ(result.bytes, 4U);
    EXPECT_EQ(read_file(a), "abab");
}

TEST_F(CombineTest, FailsWithNotFoundWhenEitherInputMissingAndWritesNothing) {
    const auto a = dir_.write_file("a", "x");

    for (const auto& [first, second] :
         {std::pair{a, dir_.file("missing")}, std::pair{dir_.file("missing"), a}}) {
        try {
            ops_.sut.combine(first, second, dir_.file("out"));
            FAIL() << "expected Error";
        } catch (const Error& e) {
            EXPECT_EQ(e.code(), ExitCode::NotFound);
        }
    }
    EXPECT_FALSE(std::filesystem::exists(dir_.file("out")));
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

TEST_F(CombineTest, RefusesToOverwriteExistingOutputByDefault) {
    const auto a = dir_.write_file("a", "1");
    const auto b = dir_.write_file("b", "2");
    const auto out = dir_.write_file("out", "keep");

    try {
        ops_.sut.combine(a, b, out);
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::AlreadyExists);
    }
    EXPECT_EQ(read_file(out), "keep");
}

TEST_F(CombineTest, LogsAllThreePathsAndByteCount) {
    const auto a = dir_.write_file("a", "12");
    const auto b = dir_.write_file("b", "345");

    ops_.sut.combine(a, b, dir_.file("out"));

    const auto& combined = ops_.logger.record(events::files_combined);
    EXPECT_EQ(field<std::string>(combined, "First"), a.string());
    EXPECT_EQ(field<std::string>(combined, "Second"), b.string());
    EXPECT_EQ(field<std::string>(combined, "Output"), dir_.file("out").string());
    EXPECT_EQ(field<std::int64_t>(combined, "Bytes"), 5);
}

TEST_F(CombineTest, PreservesBinaryContentAcrossBufferBoundaries) {
    // Each input is larger than one copy buffer and not a multiple of it.
    std::string first((1024 * 1024) + 13, '\0');
    std::string second((1024 * 1024) + 29, '\xFF');
    for (std::size_t i = 0; i < first.size(); ++i) {
        first[i] = static_cast<char>(i % 251);
    }
    const auto a = dir_.write_file("a.bin", first);
    const auto b = dir_.write_file("b.bin", second);

    const Result result = ops_.sut.combine(a, b, dir_.file("out.bin"));

    EXPECT_EQ(result.bytes, first.size() + second.size());
    EXPECT_EQ(read_file(dir_.file("out.bin")), first + second);
}

TEST_F(CombineTest, EmptyArgumentIsAUsageError) {
    try {
        ops_.sut.combine(dir_.file("a"), "", dir_.file("out"));
        FAIL() << "expected Error";
    } catch (const Error& e) {
        EXPECT_EQ(e.code(), ExitCode::UsageError);
        EXPECT_STREQ(e.what(), "second must not be empty");
    }
}

}  // namespace
}  // namespace fileops::test
