#pragma once

// In-memory log sink: captures every record so a test can assert on the telemetry an operation
// produced, not just on the file system. This is the "logs as evidence" contract in test form.

#include "fileops/operations.hpp"
#include "fileops/telemetry/log.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "collecting_observer.hpp"

namespace fileops::test {

class CollectingLogger final : public telemetry::Logger {
public:
    [[nodiscard]] bool enabled(telemetry::LogLevel /*level*/) const noexcept override { return true; }
    void log(const telemetry::LogRecord& record) override { records_.push_back(record); }

    [[nodiscard]] const std::vector<telemetry::LogRecord>& records() const noexcept { return records_; }

    /// The single record with the given event id; fails the test if there is not exactly one.
    [[nodiscard]] const telemetry::LogRecord& record(int event_id) const {
        const telemetry::LogRecord* found = nullptr;
        for (const auto& r : records_) {
            if (r.event_id == event_id) {
                if (found != nullptr) {
                    throw std::runtime_error("more than one record with event id " +
                                             std::to_string(event_id));
                }
                found = &r;
            }
        }
        if (found == nullptr) {
            throw std::runtime_error("no record with event id " + std::to_string(event_id));
        }
        return *found;
    }

    [[nodiscard]] bool has(int event_id) const noexcept {
        return std::any_of(
            records_.begin(), records_.end(), [&](const auto& r) { return r.event_id == event_id; });
    }

    [[nodiscard]] std::vector<int> event_ids() const {
        std::vector<int> ids;
        ids.reserve(records_.size());
        for (const auto& r : records_) {
            ids.push_back(r.event_id);
        }
        return ids;
    }

private:
    std::vector<telemetry::LogRecord> records_;
};

/// Value of the named field in a record, converted to T; fails if absent or of another type.
template <typename T>
T field(const telemetry::LogRecord& record, std::string_view name) {
    for (const auto& f : record.state) {
        if (f.name == name) {
            if (const auto* value = std::get_if<T>(&f.value)) {
                return *value;
            }
            throw std::runtime_error("field " + std::string{name} + " has another type");
        }
    }
    throw std::runtime_error("no field named " + std::string{name});
}

/// A FileOperations instance wired to a CollectingLogger and a CollectingObserver. One per test, so
/// assertions are exact.
struct ObservedOperations {
    CollectingLogger logger;
    CollectingObserver observer;
    FileOperations sut{logger, observer};
};

}  // namespace fileops::test
