#pragma once

// A minimal OTLP/HTTP receiver for integration tests: accepts connections on 127.0.0.1, records
// each request's path, content type and body size, and answers 200. Enough to prove the tool
// exports spans, metrics and log records to the endpoint it was given, without a collector.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fileops::test {

struct ReceivedRequest {
    std::string path;
    std::string content_type;
    std::string body;
};

class OtlpReceiver {
public:
    OtlpReceiver();
    ~OtlpReceiver();
    OtlpReceiver(const OtlpReceiver&) = delete;
    OtlpReceiver& operator=(const OtlpReceiver&) = delete;
    OtlpReceiver(OtlpReceiver&&) = delete;
    OtlpReceiver& operator=(OtlpReceiver&&) = delete;

    /// `http://127.0.0.1:<port>`, for OTEL_EXPORTER_OTLP_ENDPOINT.
    [[nodiscard]] std::string endpoint() const { return "http://127.0.0.1:" + std::to_string(port_); }

    /// Stops accepting and returns everything received so far.
    std::vector<ReceivedRequest> stop();

    /// Requests whose path ends with `suffix` (e.g. "/v1/traces").
    [[nodiscard]] static std::vector<ReceivedRequest> with_path(const std::vector<ReceivedRequest>& all,
                                                                const std::string& suffix);

private:
    void serve();
    void handle(int client);

    int listener_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::mutex lock_;
    std::vector<ReceivedRequest> requests_;
};

}  // namespace fileops::test
