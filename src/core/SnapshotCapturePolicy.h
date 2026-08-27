#pragma once

#include <string>

// Decides WHEN the engine records a time-travel snapshot each fixed step. The ring
// (ISnapshotStorage) and the serialization format are deliberately untouched -- this
// gates only the *call*, so:
//   - scenes whose per-step snapshot fits a time budget keep full per-step capture;
//   - a scene that is genuinely too large stops paying the cost for the rest of the
//     session and the reason is surfaced to the Timeline;
//   - packaged builds (a shipped game with no Timeline UI) never capture at all.
// A future binary/delta ISnapshotStorage backend can replace JSON without changing
// this policy or the simulation (that is why the two are separate).
//
// SUSTAINED OVERRUN, NOT ONE STRIKE (candidate D, fixed 2026-08-27).
// This policy originally disabled capture on the FIRST capture over budget. Measurement
// (DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md) showed why that was wrong: in Release every
// L3 game clears the 4 ms budget comfortably on the median -- 0.554 ms at 91 entities,
// 2.302 ms at 348, 2.328 ms for Minecraft, 1.323 ms for the arena -- while still
// producing a 0.08-0.12 % tail of individual captures above it. Under a one-strike rule
// a single such spike, roughly one capture in a thousand, permanently killed time travel
// for the whole session. The engine's headline debugging feature was being switched off
// by noise, not by cost.
//
// The distinction that matters is **isolated spike vs sustained overrun**. A scene that
// is genuinely too expensive to snapshot blows the budget on essentially *every* capture,
// not one in a thousand. So capture is disabled only after kConsecutiveOverBudget
// captures in a row exceed the budget; any single capture under budget resets the count.
// At 60 Hz that is a fifth of a second of continuous overrun before the feature gives up
// -- fast enough to protect the frame rate, far outside the reach of an isolated spike.
//
// Deliberately NOT included: re-arming mid-session. Once capture is disabled the engine
// stops calling recordCaptureCost at all, so there are no fresh cost samples to re-arm
// from; re-arming would need a periodic probe capture, which nothing has asked for. A new
// Play session still re-arms through reset().
class SnapshotCapturePolicy {
public:
    // How many consecutive over-budget captures disable capture for the session. One is
    // what this class used to do, and it is what candidate D was: see the header note.
    static constexpr int kConsecutiveOverBudget = 8;

    // budgetMs: a capture costing more than this counts as over budget. packaged: a
    // shipped build (no editor) -> never capture.
    void configure(double budgetMs, bool packaged);

    // Re-arm at the start of a Play session (a fresh scene may fit even if the last
    // did not; a non-packaged session starts enabled again).
    void reset();

    // Feed each capture's measured wall-clock cost; may flip the policy to disabled.
    void recordCaptureCost(double milliseconds);

    bool enabled() const { return enabled_; }
    const std::string& disabledReason() const { return reason_; }

    // How many consecutive over-budget captures have been seen so far. Diagnostic: it
    // lets a test -- and the Timeline, if it ever wants to warn before the cut-off --
    // see the run building rather than only its result.
    int consecutiveOverBudget() const { return consecutiveOverBudget_; }

private:
    double budgetMs_ = 4.0;
    bool packaged_ = false;
    bool enabled_ = true;
    int consecutiveOverBudget_ = 0;
    std::string reason_;
};
