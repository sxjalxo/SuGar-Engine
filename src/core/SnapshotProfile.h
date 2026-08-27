#pragma once

#include <cstddef>
#include <streambuf>

// Snapshot capture cost instrumentation
// (DevDocs/DESIGN_SNAPSHOT_CAPTURE_COST.md, section 5).
//
// Phases (traversal, format, materialize, store) are obtained by DIFFERENTIAL
// SUBSTITUTION -- swapping one thing out per run -- not by timers inside
// writeSceneJson. That writer walks the ECS and formats tokens in ONE pass, so a timer
// per call would cost the same order as the work it measures and would manufacture the
// split it claims to report (DEV_ENVIRONMENT.md #9: a probe is code, and gets the same
// suspicion as the code it probes).
//
// The instrument's real self-validation is NOT a phase-sum check. format and
// materialize are both derived by subtraction from the same three measured quantities
// (save, nullSink, traversal): format = nullSink - traversal, materialize =
// save - nullSink. Their sum with traversal is therefore
// T + (nullSink - T) + (save - nullSink) = save, IDENTICALLY, for any T, nullSink,
// save. A phase-sum residual is 0 by construction and can only detect arithmetic
// tampering after the fact -- it can never catch a bad split (F4,
// DESIGN_SNAPSHOT_CAPTURE_COST.md 5.3). The check that CAN fail is
// `snapshot_sink_bytes_delta` (src/Benchmarks.h): it compares NullSink's byte count
// against the real serializer's output size on the same scene. Task 4's break-test
// (stop NullSink::xsputn from accumulating) makes that check fail immediately; a
// phase-sum check would stay green. See also `testSnapshotSinkBytes` in
// src/SelfTests.h, which gates this invariant in the self-test suite.

// A streambuf that accepts and discards everything. Writing the scene into this costs
// traversal + token formatting, and nothing else: no buffer growth, no final copy.
class NullSink : public std::streambuf {
public:
    std::size_t written() const { return written_; }

protected:
    // Bulk path -- what ostream uses for strings and for anything already formatted.
    std::streamsize xsputn(const char*, std::streamsize count) override {
        written_ += static_cast<std::size_t>(count);
        return count;
    }

    // Single-character path. Returning anything but eof() means "accepted".
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            ++written_;
        }
        return ch;
    }

private:
    std::size_t written_ = 0;
};
