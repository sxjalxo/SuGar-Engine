#pragma once

#include <cstdlib>
#include <string>

// How long a single time-travel capture may take before SnapshotCapturePolicy stops
// capturing for the session. Overridable because a *measurement* run has to capture
// scenes the budget is meant to reject -- otherwise the instrument measures the latch
// instead of the capture (DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md, section 9.3).
//
// Parsing is a free function taking the raw string rather than reading getenv itself,
// so it can be tested without mutating the process environment.
inline double snapshotBudgetFromEnv(const char* raw, double fallback) {
    if (raw == nullptr) {
        return fallback;
    }
    try {
        std::size_t consumed = 0;
        const std::string text(raw);
        const double parsed = std::stod(text, &consumed);
        // Trailing garbage ("12ms") is a typo, not a value: fall back rather than
        // silently honouring half of what was typed.
        for (std::size_t i = consumed; i < text.size(); ++i) {
            if (text[i] != ' ' && text[i] != '\t') {
                return fallback;
            }
        }
        // A zero or negative budget would disable capture instantly, which is the
        // opposite of what anyone setting this knob wants.
        return parsed > 0.0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

// Convenience for the one production caller.
inline double snapshotBudgetFromEnvironment(double fallback) {
    return snapshotBudgetFromEnv(std::getenv("SUGAR_SNAP_BUDGET"), fallback);
}
