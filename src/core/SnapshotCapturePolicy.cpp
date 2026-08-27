#include "core/SnapshotCapturePolicy.h"

#include "core/SnapshotBudget.h"

#include <sstream>

void SnapshotCapturePolicy::configure(double budgetMs, bool packaged) {
    const double requested = budgetMs > 0.0 ? budgetMs : 4.0;
    // A measurement run raises this deliberately; a normal run never sets the variable
    // and gets exactly the behaviour it had before.
    budgetMs_ = snapshotBudgetFromEnvironment(requested);
    packaged_ = packaged;
    reset();
}

void SnapshotCapturePolicy::reset() {
    consecutiveOverBudget_ = 0;
    if (packaged_) {
        enabled_ = false;
        reason_ = "time-travel disabled in packaged build";
    } else {
        enabled_ = true;
        reason_.clear();
    }
}

void SnapshotCapturePolicy::recordCaptureCost(double milliseconds) {
    if (!enabled_) {
        return;
    }

    if (milliseconds <= budgetMs_) {
        // One affordable capture ends the run. An isolated spike -- measured at 0.08-0.12 %
        // of captures in Release, i.e. roughly one in a thousand -- therefore cannot
        // accumulate towards the cut-off across a session.
        consecutiveOverBudget_ = 0;
        return;
    }

    ++consecutiveOverBudget_;
    if (consecutiveOverBudget_ < kConsecutiveOverBudget) {
        return;
    }

    // Sustained: every one of the last kConsecutiveOverBudget captures was over budget,
    // so this is scene cost rather than noise.
    enabled_ = false;
    std::ostringstream reason;
    reason << "time-travel paused: " << kConsecutiveOverBudget
           << " consecutive snapshots over the " << budgetMs_
           << " ms budget (latest " << milliseconds << " ms; scene too large)";
    reason_ = reason.str();
}
