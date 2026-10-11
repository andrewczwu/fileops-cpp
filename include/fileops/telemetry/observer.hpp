#pragma once

// Operation-level observation: the facts a span and the metrics are made of. The core library
// reports them through this interface and knows nothing about OpenTelemetry; the CLI supplies an
// implementation backed by opentelemetry-cpp and the tests supply one that records in memory.

#include "fileops/exit_code.hpp"
#include "fileops/operation.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fileops::telemetry {

/// What an operation is about to do. Optional members are set only where they apply.
struct OperationContext {
    Operation operation = Operation::Create;
    /// The file produced or removed.
    std::filesystem::path path;
    std::optional<std::filesystem::path> source;
    std::optional<std::filesystem::path> source2;
    std::optional<std::filesystem::path> destination;
    /// Whether --force was given; unset for delete, where it has a different meaning.
    std::optional<bool> overwrite;
};

/// How an operation ended.
struct OperationOutcome {
    ExitCode exit_code = ExitCode::Success;
    /// Bytes written or removed.
    std::uint64_t bytes = 0;
    std::chrono::duration<double> duration{0};
    /// Type of the failure, e.g. "fileops::Error"; empty on success.
    std::string error_type;
    /// The user-facing message; empty on success.
    std::string error_message;

    [[nodiscard]] bool succeeded() const noexcept { return exit_code == ExitCode::Success; }
};

/// One running operation. finish() is called exactly once, on success or failure.
class OperationScope {
public:
    virtual ~OperationScope() = default;
    OperationScope() = default;
    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;
    OperationScope(OperationScope&&) = delete;
    OperationScope& operator=(OperationScope&&) = delete;

    virtual void finish(const OperationOutcome& outcome) = 0;
};

class Observer {
public:
    virtual ~Observer() = default;
    Observer() = default;
    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;
    Observer(Observer&&) = delete;
    Observer& operator=(Observer&&) = delete;

    virtual std::unique_ptr<OperationScope> start(const OperationContext& context) = 0;
};

/// Observes nothing. The default when a caller only wants logs.
class NullObserver final : public Observer {
public:
    std::unique_ptr<OperationScope> start(const OperationContext& /*context*/) override;
};

/// A process-wide NullObserver for callers that do not supply one.
Observer& null_observer() noexcept;

/// Metric tag value for an outcome: "success" or the lower-case exit code name ("notfound").
std::string outcome_tag(ExitCode code);

}  // namespace fileops::telemetry
