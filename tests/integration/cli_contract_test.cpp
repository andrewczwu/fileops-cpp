// The contract scripts and CI rely on: stdout is reserved for results, stderr carries logs and the
// summary line, exit codes are stable, and the log stream is self-describing enough to diagnose a
// failure without re-running the command.

#include "fileops/exit_code.hpp"
#include "fileops/telemetry/log_events.hpp"

#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "temp_directory.hpp"
#include "tool.hpp"

namespace fileops::test {
namespace {

namespace events = telemetry::log_events;

class CliContractTest : public ::testing::Test {
protected:
    TempDirectory dir_;
};

TEST_F(CliContractTest, StdoutIsEmptyOnSuccessAndOnFailure) {
    dir_.write_file("a", "x");

    const auto success = Tool::run(dir_.path(), {"copy", "a", "b"});
    const auto failure = Tool::run(dir_.path(), {"copy", "missing", "c"});

    EXPECT_EQ(success.exit_code, 0);
    EXPECT_EQ(success.stdout_text, "");
    EXPECT_NE(failure.exit_code, 0);
    EXPECT_EQ(failure.stdout_text, "");
}

TEST_F(CliContractTest, LogsAreOneJsonObjectPerStderrLineWithStableSchema) {
    dir_.write_file("a", "x");

    const auto result = Tool::run(dir_.path(), {"copy", "a", "b"});

    const auto records = result.log_records();
    ASSERT_FALSE(records.empty());
    for (const auto& record : records) {
        for (const char* key :
             {"Timestamp", "EventId", "EventName", "LogLevel", "Category", "Message", "State"}) {
            EXPECT_TRUE(record.contains(key)) << "missing " << key << " in " << record.dump();
        }
        EXPECT_TRUE(record.at("State").is_object());
        EXPECT_EQ(record.at("Category"), "fileops.core.FileOperations");
        // ISO 8601, UTC, e.g. 2026-10-10T14:03:11.123456Z
        EXPECT_TRUE(std::regex_match(record.at("Timestamp").get<std::string>(),
                                     std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z)")));
    }
    EXPECT_EQ(
        result.event_ids(),
        (std::vector<int>{events::operation_started, events::file_copied, events::operation_completed}));
}

TEST_F(CliContractTest, NothingIsLoggedByDefault) {
    dir_.write_file("a", "x");

    const auto result = Tool::run(dir_.path(), Environment{}, {"copy", "a", "b"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stderr_text, "");
}

TEST_F(CliContractTest, VerboseFlagEnablesHumanReadableLogs) {
    dir_.write_file("a", "x");

    const auto result = Tool::run(dir_.path(), Environment{}, {"--verbose", "copy", "a", "b"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.log_records().empty());
    EXPECT_NE(result.stderr_text.find("[" + std::to_string(events::file_copied) + "]"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("Copied a to b (1 bytes)"), std::string::npos);
}

TEST_F(CliContractTest, GlobalOptionsMayFollowTheSubcommand) {
    dir_.write_file("a", "x");

    const auto result =
        Tool::run(dir_.path(), Environment{}, {"copy", "a", "b", "--log-level", "Information"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Copied a to b"), std::string::npos);
}

TEST_F(CliContractTest, CommandLineOptionsOverrideEnvironment) {
    dir_.write_file("a", "x");

    const auto result = Tool::run(dir_.path(), {"--log-level", "None", "copy", "a", "b"});

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stderr_text, "");
}

class ExitCodeContractTest : public CliContractTest,
                             public ::testing::WithParamInterface<std::vector<std::string>> {};

TEST_P(ExitCodeContractTest, ProcessExitCodeMatchesTheLoggedExitCode) {
    const auto result = Tool::run(dir_.path(), GetParam());

    EXPECT_NE(result.exit_code, 0);
    const auto failed = result.log(events::operation_failed);
    EXPECT_EQ(failed.at("LogLevel"), "Error");
    const std::string logged = failed.at("State").at("ExitCode").get<std::string>();
    int expected = -1;
    for (ExitCode code : {ExitCode::UsageError,
                          ExitCode::NotFound,
                          ExitCode::AlreadyExists,
                          ExitCode::IoError,
                          ExitCode::InternalError}) {
        if (to_string(code) == logged) {
            expected = to_int(code);
        }
    }
    EXPECT_EQ(result.exit_code, expected) << "logged " << logged;
    const auto messages = result.messages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages.front(), "fileops: " + failed.at("State").at("Reason").get<std::string>());
    EXPECT_FALSE(result.has_log(events::operation_completed));
}

INSTANTIATE_TEST_SUITE_P(Failures,
                         ExitCodeContractTest,
                         ::testing::Values(std::vector<std::string>{"copy", "missing", "dst"},
                                           std::vector<std::string>{"combine", "missing", "missing", "dst"},
                                           std::vector<std::string>{"delete", "missing"},
                                           std::vector<std::string>{"delete", "."}));

TEST_F(CliContractTest, SummaryLineComesAfterAllLogRecords) {
    const auto result = Tool::run(dir_.path(), {"delete", "missing"});

    const auto lines = result.stderr_lines();
    ASSERT_GE(lines.size(), 2U);
    EXPECT_TRUE(lines.back().starts_with("fileops:"));
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        EXPECT_TRUE(lines[i].starts_with('{')) << lines[i];
    }
}

class EmptyPathTest : public CliContractTest,
                      public ::testing::WithParamInterface<std::vector<std::string>> {};

TEST_P(EmptyPathTest, EmptyPathArgumentIsAUsageErrorWithFailureTelemetry) {
    const auto result = Tool::run(dir_.path(), GetParam());

    EXPECT_EQ(result.exit_code, to_int(ExitCode::UsageError));
    EXPECT_EQ(result.log(events::operation_failed).at("State").at("ExitCode"), "UsageError");
    const auto messages = result.messages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_NE(messages.front().find("must not be empty"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Arguments,
                         EmptyPathTest,
                         ::testing::Values(std::vector<std::string>{"create", ""},
                                           std::vector<std::string>{"copy", "", "dst"},
                                           std::vector<std::string>{"delete", ""}));

TEST_F(CliContractTest, LongFileNamesWorkEndToEnd) {
    const std::string name(240, 'n');

    const auto created = Tool::run(dir_.path(), {"create", name, "--text", "x"});
    const auto copied = Tool::run(dir_.path(), {"copy", name, name.substr(0, 239) + "2"});

    EXPECT_EQ(created.exit_code, 0);
    EXPECT_EQ(copied.exit_code, 0);
    EXPECT_TRUE(dir_.leftover_temp_files().empty());
}

class InvalidUsageTest : public CliContractTest,
                         public ::testing::WithParamInterface<std::vector<std::string>> {};

TEST_P(InvalidUsageTest, InvalidUsageExits1WithMessagesOnStderrAndNothingOnStdout) {
    const auto result = Tool::run(dir_.path(), GetParam());

    EXPECT_EQ(result.exit_code, to_int(ExitCode::UsageError));
    EXPECT_EQ(result.stdout_text, "");
    EXPECT_FALSE(result.messages().empty());
    for (const auto& line : result.stderr_lines()) {
        EXPECT_TRUE(line.starts_with("fileops: ")) << line;
    }
    EXPECT_NE(result.stderr_text.find("run 'fileops --help' for usage"), std::string::npos);
    EXPECT_TRUE(result.log_records().empty());  // Nothing ran, so no telemetry was produced.
}

INSTANTIATE_TEST_SUITE_P(Arguments,
                         InvalidUsageTest,
                         ::testing::Values(std::vector<std::string>{},
                                           std::vector<std::string>{"create"},
                                           std::vector<std::string>{"copy", "only-one"},
                                           std::vector<std::string>{"bogus"},
                                           std::vector<std::string>{"create", "x", "--nope"},
                                           std::vector<std::string>{"create", "a", "b", "c"},
                                           std::vector<std::string>{"--log-format", "xml", "create", "x"}));

TEST_F(CliContractTest, HelpListsEveryCommand) {
    const auto result = Tool::run(dir_.path(), {"--help"});

    EXPECT_EQ(result.exit_code, 0);
    for (const char* command : {"create", "copy", "combine", "delete"}) {
        EXPECT_NE(result.stdout_text.find(command), std::string::npos) << command;
    }
    EXPECT_EQ(result.stderr_text, "");
}

TEST_F(CliContractTest, VersionIsSemver) {
    const auto result = Tool::run(dir_.path(), {"--version"});

    EXPECT_EQ(result.exit_code, 0);
    std::string version = result.stdout_text;
    while (!version.empty() && (version.back() == '\n' || version.back() == '\r')) {
        version.pop_back();
    }
    EXPECT_TRUE(
        std::regex_match(version, std::regex(R"(^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$)")))
        << version;
}

}  // namespace
}  // namespace fileops::test
