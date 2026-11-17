// src/policy/policies.cpp — YOU IMPLEMENT. Spec §5.2.

#include "policy/policies.h"

#include <utility>

AllowlistPolicy::AllowlistPolicy(const std::vector<std::string>& programs)
    : programs_(programs.begin(), programs.end()) {}

Decision AllowlistPolicy::evaluate(const CommandRequest& req) {
    if (programs_.count(req.program) > 0) {
        return Decision::allow("'" + req.program + "' is on the allowlist", name());
    }
    return Decision::deny("'" + req.program + "' is not on the allowlist", name());
}

ArgumentPatternPolicy::ArgumentPatternPolicy(std::vector<std::string> patterns)
    : patterns_(std::move(patterns)) {}

Decision ArgumentPatternPolicy::evaluate(const CommandRequest& req) {
    std::string line = req.command_line();
    for (std::size_t i = 0; i < patterns_.size(); i++) {
        if (patterns_[i].empty()) {
            continue;
        }
        if (line.find(patterns_[i]) != std::string::npos) {
            return Decision::deny("command contains the blocked pattern '" + patterns_[i] + "'", name());
        }
    }
    return Decision::allow("no blocked pattern found", name());
}

ConfirmPolicy::ConfirmPolicy(const std::vector<std::string>& programs)
    : programs_(programs.begin(), programs.end()) {}

Decision ConfirmPolicy::evaluate(const CommandRequest& req) {
    if (programs_.count(req.program) > 0) {
        return Decision::ask("'" + req.program + "' needs your confirmation", name());
    }
    return Decision::allow("'" + req.program + "' does not need confirmation", name());
}

RateLimitPolicy::RateLimitPolicy(std::size_t max_commands, double window_seconds, const Clock& clock)
    : max_(max_commands), window_(window_seconds), clock_(clock) {}

Decision RateLimitPolicy::evaluate(const CommandRequest& req) {
    (void)req;
    double now = clock_.now_seconds();
    while (times_.empty() == false && now - times_.front() >= window_) {
        times_.pop();
    }
    if (times_.size() >= max_) {
        return Decision::deny("rate limit reached", name());
    }
    times_.push(now);
    return Decision::allow("within the rate limit", name());
}

std::size_t RateLimitPolicy::tracked() const noexcept { return times_.size(); }
