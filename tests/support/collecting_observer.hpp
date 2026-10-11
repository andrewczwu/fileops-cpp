#pragma once

// In-memory Observer: records every operation's context and outcome so a test can assert on the
// span and metric facts exactly, without an OpenTelemetry SDK in the loop.

#include "fileops/telemetry/observer.hpp"

#include <deque>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fileops::test {

struct ObservedOperation {
    telemetry::OperationContext context;
    std::optional<telemetry::OperationOutcome> outcome;  // unset while running
};

class CollectingObserver final : public telemetry::Observer {
public:
    std::unique_ptr<telemetry::OperationScope> start(const telemetry::OperationContext& context) override {
        operations_.push_back({context, std::nullopt});
        return std::make_unique<Scope>(operations_.back());
    }

    [[nodiscard]] const std::deque<ObservedOperation>& operations() const noexcept { return operations_; }

    /// The single observed operation; fails if there is not exactly one or it never finished.
    [[nodiscard]] const ObservedOperation& single() const {
        if (operations_.size() != 1) {
            throw std::runtime_error("expected exactly one observed operation, got " +
                                     std::to_string(operations_.size()));
        }
        if (!operations_.front().outcome) {
            throw std::runtime_error("the observed operation never finished");
        }
        return operations_.front();
    }

private:
    class Scope final : public telemetry::OperationScope {
    public:
        explicit Scope(ObservedOperation& record) : record_(record) {}
        void finish(const telemetry::OperationOutcome& outcome) override {
            if (record_.outcome) {
                throw std::runtime_error("finish() called twice");
            }
            record_.outcome = outcome;
        }

    private:
        ObservedOperation& record_;
    };

    std::deque<ObservedOperation> operations_;  // references stay valid across start() calls
};

}  // namespace fileops::test
