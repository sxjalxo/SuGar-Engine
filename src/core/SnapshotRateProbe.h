#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

// SNAPSHOT SEMANTICS MEASUREMENT INSTRUMENT
//
// Records whether consecutive captures are byte-identical and tracks the
// number of distinct states retained by the snapshot ring.
//
// This instrument exists to guard the finding that fixed-step captures
// currently produce distinct serialized states even during apparent idle
// periods. It is not a capture-rate policy.
//
// THE FINDING IT GUARDS (candidate B, measured 2026-08-27, Release, ~10 700 captures across
// the crawler at machine speed, the crawler fully idle, Minecraft and the combat arena):
// **zero** consecutive captures were byte-identical in any run, run_max 1, and all 600 ring
// frames held distinct states. ROADMAP.md's turn-based-probe entry had recorded, on inference
// rather than observation, that a turn-based game "would record 20-115+ identical snapshots
// per turn". The steps-per-turn half is right; *identical* is false. Two values move every
// fixed step in a scene nobody is touching: a game-side counter in GameData, and the engine's
// own AnimationComponent.time under a looping idle clip. One idle clip is enough, so
// capture-on-change would not have paid anywhere -- candidate B is closed as falsified.
//
// WHY IT IS KEPT rather than deleted with the investigation. The finding is a property of the
// serialized state model, not a fact about three games, and nothing else watches it. If a
// later change makes captures start repeating, that is a semantic change -- something stopped
// being serialized, or stopped advancing -- and firstDifference() names the field that stopped
// moving. Without it a green run only says "capture-on-change might pay now" and cannot say
// why.
//
// WHERE THIS MAY BE CALLED FROM. Outside the region captureSnapshotBudgeted times, always.
// record() costs a string compare plus a copy; inside the timed region it would land in the
// numbers DESIGN_SNAPSHOT_CAPTURE_COST.md reports. The SUGAR_SNAP_CORPUS knob made exactly
// that mistake once (SuGarApp.cpp, F14 note) -- it is not a hypothetical.
class SnapshotRateProbe {
public:
    // Frames retained by the live ring; the window whose distinct-state count is reported.
    static constexpr std::size_t kWindow = 600;

    void reset() { *this = SnapshotRateProbe{}; }

    // Feed each pushed snapshot, newest last, in capture order.
    void record(const std::string& snapshot) {
        const bool distinct = (captures_ == 0) || (snapshot != previous_);

        ++captures_;
        bytesTotal_ += snapshot.size();
        if (distinct) {
            closeRun();
            currentRun_ = 1;
            ++distinctTotal_;
            bytesDistinct_ += snapshot.size();
            captureFirstDifference(snapshot);
            previous_ = snapshot;
        } else {
            ++currentRun_;
            ++duplicates_;
        }

        // Rolling count of distinct captures among the last kWindow -- i.e. how many
        // genuinely different states the ring is holding right now.
        const std::size_t slot = captures_ % kWindow;
        if (captures_ > kWindow && windowDistinct_[slot] != 0) {
            --distinctInWindow_;
        }
        windowDistinct_[slot] = distinct ? 1 : 0;
        if (distinct) {
            ++distinctInWindow_;
        }
    }

    std::string summary() const {
        // Runs closed so far plus the one in flight, so the numbers are readable mid-run
        // (these sessions are stopped by taskkill from outside and often never reach stop()).
        const std::uint64_t runs = runsClosed_ + (currentRun_ > 0 ? 1 : 0);
        const double meanRun = runs > 0 ? static_cast<double>(captures_) / static_cast<double>(runs) : 0.0;
        const double dupPct =
            captures_ > 0 ? 100.0 * static_cast<double>(duplicates_) / static_cast<double>(captures_) : 0.0;

        std::ostringstream out;
        out << "[snaprate] captures=" << captures_ << " duplicates=" << duplicates_ << " dup_pct=" << dupPct
            << " distinct=" << distinctTotal_ << " runs=" << runs << " run_mean=" << meanRun
            << " run_max=" << runMax_ << " window=" << kWindow << " distinct_in_window=" << distinctInWindow_
            << " bytes_total=" << bytesTotal_ << " bytes_distinct=" << bytesDistinct_ << " runlen[1]=" << histogram_[0]
            << " runlen[2-4]=" << histogram_[1] << " runlen[5-16]=" << histogram_[2]
            << " runlen[17-64]=" << histogram_[3] << " runlen[65-256]=" << histogram_[4]
            << " runlen[257+]=" << histogram_[5];
        return out.str();
    }

    std::uint64_t captures() const { return captures_; }
    std::uint64_t duplicates() const { return duplicates_; }
    std::uint64_t distinctTotal() const { return distinctTotal_; }
    // Longest run of consecutive byte-identical captures CLOSED so far; a run still in
    // flight is not counted until a different snapshot ends it.
    std::uint64_t runMax() const { return runMax_; }
    std::size_t distinctInWindow() const { return distinctInWindow_; }

    // Where the newest capture first stopped matching the one before it, with the text
    // around it from both. "0 duplicates" is only half an answer: if an idle scene still
    // serializes differently every step, this says which field is churning.
    std::string firstDifference() const {
        if (captures_ < 2 || firstDiffOffset_ == kNoDifference) {
            return "[snapdiff] none";
        }
        std::ostringstream out;
        out << "[snapdiff] offset=" << firstDiffOffset_ << " last_offset=" << lastDiffOffset_
            << " differing_bytes=" << diffByteCount_ << " size_delta=" << sizeDelta_ << " prev=\"" << diffPrevExcerpt_
            << "\" next=\"" << diffNextExcerpt_ << "\" last_prev=\"" << diffLastPrevExcerpt_ << "\" last_next=\""
            << diffLastNextExcerpt_ << "\"";
        return out.str();
    }

private:
    void closeRun() {
        if (currentRun_ == 0) {
            return;
        }
        ++runsClosed_;
        if (currentRun_ > runMax_) {
            runMax_ = currentRun_;
        }
        histogram_[bucketOf(currentRun_)]++;
        currentRun_ = 0;
    }

    // Excerpt half-width around the first differing byte. Wide enough to show the JSON
    // key that owns the value, narrow enough to stay one readable line.
    static constexpr std::size_t kExcerpt = 70;
    static constexpr std::size_t kNoDifference = static_cast<std::size_t>(-1);

    void captureFirstDifference(const std::string& next) {
        if (captures_ < 2) {
            return; // nothing to compare the first capture against
        }
        const std::size_t limit = previous_.size() < next.size() ? previous_.size() : next.size();
        std::size_t i = 0;
        while (i < limit && previous_[i] == next[i]) {
            ++i;
        }
        firstDiffOffset_ = i;

        // How WIDE the change is, not only where it starts. One churning counter and a
        // genuinely different world state look identical to a first-difference offset;
        // they do not look identical to a differing-byte count.
        //
        // READ THIS ONLY WHEN size_delta IS 0. The comparison is positional, so an insertion
        // or deletion earlier in the document misaligns everything after it and every later
        // byte scores as differing. Measured: the crawler (size_delta 0) reports 13 differing
        // bytes of 40 947, a real 0.03 %; Minecraft (size_delta 47) reports ~80 %, which is an
        // artifact of misalignment and not a delta size. A structural diff is a different
        // instrument; nothing has asked for one.
        std::size_t differing = 0;
        std::size_t last = i;
        for (std::size_t j = i; j < limit; ++j) {
            if (previous_[j] != next[j]) {
                ++differing;
                last = j;
            }
        }
        diffByteCount_ = differing;
        lastDiffOffset_ = last;
        sizeDelta_ = static_cast<long long>(next.size()) - static_cast<long long>(previous_.size());

        const std::size_t from = i > kExcerpt ? i - kExcerpt : 0;
        diffPrevExcerpt_ = previous_.substr(from, kExcerpt * 2);
        diffNextExcerpt_ = next.substr(from, kExcerpt * 2);

        // The change is not necessarily one field: report the far end of the differing
        // span too, or a second churning value stays invisible behind the first.
        const std::size_t lastFrom = last > kExcerpt ? last - kExcerpt : 0;
        diffLastPrevExcerpt_ = previous_.substr(lastFrom, kExcerpt * 2);
        diffLastNextExcerpt_ = next.substr(lastFrom, kExcerpt * 2);
    }

    static std::size_t bucketOf(std::uint64_t runLength) {
        if (runLength <= 1) return 0;
        if (runLength <= 4) return 1;
        if (runLength <= 16) return 2;
        if (runLength <= 64) return 3;
        if (runLength <= 256) return 4;
        return 5;
    }

    std::string previous_;
    std::uint64_t captures_ = 0;
    std::uint64_t duplicates_ = 0;
    std::uint64_t distinctTotal_ = 0;
    std::uint64_t currentRun_ = 0;
    std::uint64_t runsClosed_ = 0;
    std::uint64_t runMax_ = 0;
    std::uint64_t bytesTotal_ = 0;
    std::uint64_t bytesDistinct_ = 0;
    std::array<std::uint64_t, 6> histogram_{};
    std::array<std::uint8_t, kWindow> windowDistinct_{};
    std::size_t distinctInWindow_ = 0;
    std::size_t firstDiffOffset_ = kNoDifference;
    std::size_t lastDiffOffset_ = 0;
    std::size_t diffByteCount_ = 0;
    long long sizeDelta_ = 0;
    std::string diffPrevExcerpt_;
    std::string diffNextExcerpt_;
    std::string diffLastPrevExcerpt_;
    std::string diffLastNextExcerpt_;
};
