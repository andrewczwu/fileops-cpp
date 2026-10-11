// End-to-end OTLP export: the real binary, a real HTTP endpoint (in-process receiver), and the
// three signals arriving as protobuf payloads. Payload contents are checked by string presence:
// protobuf encodes strings verbatim, so "fileops.copy" in the traces body is the span name.

#include "fileops/exit_code.hpp"

#include <string>

#include <gtest/gtest.h>

#include "otlp_receiver.hpp"
#include "temp_directory.hpp"
#include "tool.hpp"

namespace fileops::test {
namespace {

class OtlpExportTest : public ::testing::Test {
protected:
    TempDirectory dir_;
    OtlpReceiver receiver_;

    [[nodiscard]] Environment env(
        std::initializer_list<std::pair<const std::string, std::optional<std::string>>> extra = {}) const {
        Environment e{{"OTEL_EXPORTER_OTLP_ENDPOINT", receiver_.endpoint()},
                      {"OTEL_EXPORTER_OTLP_TIMEOUT", "5000"}};
        for (const auto& [k, v] : extra) {
            e[k] = v;
        }
        return e;
    }
};

bool contains(const std::vector<ReceivedRequest>& requests, const std::string& needle) {
    for (const auto& r : requests) {
        if (r.body.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST_F(OtlpExportTest, ExportsASpanMetricsAndLogRecordsForOneRun) {
    dir_.write_file("a", "x");

    const auto result = Tool::run(dir_.path(), env(), {"copy", "a", "b"});
    const auto received = receiver_.stop();

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stderr_text, "") << "console stays silent without --log-level";

    const auto traces = OtlpReceiver::with_path(received, "/v1/traces");
    const auto metrics = OtlpReceiver::with_path(received, "/v1/metrics");
    const auto logs = OtlpReceiver::with_path(received, "/v1/logs");
    ASSERT_FALSE(traces.empty());
    ASSERT_FALSE(metrics.empty());
    ASSERT_FALSE(logs.empty());
    for (const auto& r : received) {
        EXPECT_EQ(r.content_type, "application/x-protobuf") << r.path;
        EXPECT_FALSE(r.body.empty()) << r.path;
    }
    EXPECT_TRUE(contains(traces, "fileops.copy"));
    EXPECT_TRUE(contains(traces, "service.name"));
    EXPECT_TRUE(contains(metrics, "fileops.operations"));
    EXPECT_TRUE(contains(metrics, "fileops.operation.duration"));
    EXPECT_TRUE(contains(metrics, "fileops.bytes"));
    // Information and above by default: the FileCopied record goes out, the Debug "Starting" one does not.
    EXPECT_TRUE(contains(logs, "Copied a to b"));
    EXPECT_FALSE(contains(logs, "Starting Copy"));
}

TEST_F(OtlpExportTest, LogLevelGovernsTheExportToo) {
    dir_.write_file("a", "x");

    const auto quiet = Tool::run(dir_.path(), env({{"FILEOPS_LOG_LEVEL", "None"}}), {"copy", "a", "b"});
    const auto received = receiver_.stop();

    EXPECT_EQ(quiet.exit_code, 0);
    EXPECT_FALSE(OtlpReceiver::with_path(received, "/v1/traces").empty()) << "spans are not logs";
    EXPECT_TRUE(OtlpReceiver::with_path(received, "/v1/logs").empty())
        << "--log-level None silences every sink";
}

TEST_F(OtlpExportTest, FailureIsExportedAsAnErrorSpanAndTheSummaryLineComesLast) {
    const auto result = Tool::run(dir_.path(),
                                  env({{"FILEOPS_LOG_LEVEL", "Debug"}, {"FILEOPS_LOG_FORMAT", "json"}}),
                                  {"delete", "missing"});
    const auto received = receiver_.stop();

    EXPECT_EQ(result.exit_code, to_int(ExitCode::NotFound));
    const auto traces = OtlpReceiver::with_path(received, "/v1/traces");
    EXPECT_TRUE(contains(traces, "fileops.delete"));
    EXPECT_TRUE(contains(traces, "file not found"));
    EXPECT_TRUE(contains(traces, "fileops::Error"));
    const auto lines = result.stderr_lines();
    ASSERT_FALSE(lines.empty());
    EXPECT_TRUE(lines.back().starts_with("fileops: file not found"));
}

}  // namespace
}  // namespace fileops::test
