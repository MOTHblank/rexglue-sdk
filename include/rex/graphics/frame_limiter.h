// Copyright 2026 Fable II Recomp contributors.
// Released under the BSD license; see LICENSE in the SDK root.
#pragma once

#include <chrono>

namespace rex::graphics {

// Worker-thread-only host swap limiter. It neither changes the guest clock nor
// advertises a different video mode. Late frames reset the schedule, not burst.
class FrameLimiter {
 public:
  FrameLimiter() = default;
  ~FrameLimiter();
  FrameLimiter(const FrameLimiter&) = delete;
  FrameLimiter& operator=(const FrameLimiter&) = delete;
  void Pace(int frames_per_second) {
    if (frames_per_second <= 0 || frames_per_second > 240) {
      rate_ = 0;
      previous_ = {};
      return;
    }

    const auto now_before_wait = Clock::now();
    if (rate_ != frames_per_second || previous_ == Clock::time_point{}) {
      previous_ = now_before_wait;
      rate_ = frames_per_second;
      return;
    }

    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / frames_per_second));
    const auto deadline = previous_ + period;
    if (now_before_wait < deadline) {
      WaitUntil(deadline);
    }

    const auto now = Clock::now();
    // Keep a stable phase across ordinary scheduler/timer overshoot instead of
    // permanently moving every later deadline by that overshoot. Only correct a
    // small error: if rendering or scheduling made the frame genuinely late,
    // reset to "now" so the limiter never emits a short catch-up frame.
    auto phase_tolerance = period / 4;
    const auto two_ms =
        std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(2));
    if (phase_tolerance > two_ms) phase_tolerance = two_ms;
    previous_ = now <= deadline + phase_tolerance ? deadline : now;
    rate_ = frames_per_second;
  }

 private:
  using Clock = std::chrono::steady_clock;
  void WaitUntil(Clock::time_point deadline);
  void* timer_ = nullptr;
  bool timer_attempted_ = false;
  Clock::time_point previous_{};
  int rate_ = 0;
};

}  // namespace rex::graphics
