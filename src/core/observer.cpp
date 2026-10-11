#include "fileops/telemetry/observer.hpp"

#include <algorithm>
#include <cctype>

namespace fileops::telemetry {
namespace {

class NullScope final : public OperationScope {
public:
    void finish(const OperationOutcome& /*outcome*/) override {}
};

}  // namespace

std::unique_ptr<OperationScope> NullObserver::start(const OperationContext& /*context*/) {
    return std::make_unique<NullScope>();
}

Observer& null_observer() noexcept {
    static NullObserver instance;
    return instance;
}

std::string outcome_tag(ExitCode code) {
    if (code == ExitCode::Success) {
        return "success";
    }
    std::string tag{to_string(code)};
    std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return tag;
}

}  // namespace fileops::telemetry
