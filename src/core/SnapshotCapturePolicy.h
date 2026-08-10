#pragma once

#include <string>

// Decides WHEN the engine records a time-travel snapshot each fixed step. The ring
// (ISnapshotStorage) and the serialization format are deliberately untouched -- this
// gates only the *call*, so:
//   - scenes whose per-step snapshot fits a time budget keep full per-step capture;
//   - once a single capture blows the budget (a large scene), capture stops paying
//     the cost for the rest of the session and the reason is surfaced to the Timeline;
//   - packaged builds (a shipped game with no Timeline UI) never capture at all.
// A future binary/delta ISnapshotStorage backend can replace JSON without changing
// this policy or the simulation (that is why the two are separate).
class SnapshotCapturePolicy {
public:
    // budgetMs: a single capture costing more than this disables capture for the rest
    // of the session. packaged: a shipped build (no editor) -> never capture.
    void configure(double budgetMs, bool packaged);

    // Re-arm at the start of a Play session (a fresh scene may fit even if the last
    // did not; a non-packaged session starts enabled again).
    void reset();

    // Feed each capture's measured wall-clock cost; may flip the policy to disabled.
    void recordCaptureCost(double milliseconds);

    bool enabled() const { return enabled_; }
    const std::string& disabledReason() const { return reason_; }

private:
    double budgetMs_ = 4.0;
    bool packaged_ = false;
    bool enabled_ = true;
    std::string reason_;
};
