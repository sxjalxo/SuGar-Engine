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
    if (milliseconds > budgetMs_) {
        enabled_ = false;
        std::ostringstream reason;
        reason << "time-travel paused: snapshot cost " << milliseconds
               << " ms exceeds the " << budgetMs_ << " ms budget (scene too large)";
        reason_ = reason.str();
    }
}
