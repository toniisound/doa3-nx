#include "d3d_vblank.h"
#include "xapi_time_adapter.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <thread>

extern "C" void recomp_kernel_wait_for_vblank(uint64_t deadline_ns);
extern "C" void recomp_kernel_yield(void);

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point last_vblank;
Clock::time_point completed_vblank;
Clock::time_point presented_vblank;
#ifdef _WIN32
// The guest and presenter threads both sleep, so each gets its own timer.
thread_local std::unique_ptr<void, decltype(&CloseHandle)>
    high_resolution_timer(nullptr, &CloseHandle);
#endif
// ponytail: the supported progressive NTSC mode; other video modes need their rate.
constexpr auto interval = std::chrono::nanoseconds(1000000000 / 60);
constexpr auto spin_window = std::chrono::microseconds(500);

Clock::time_point guest_now()
{
    return Clock::time_point(std::chrono::nanoseconds(recomp_xapi_performance_counter()));
}

// Sleeps until deadline on the clock that now() reads. Guest time runs ahead
// of steady_clock after a skip, so every wait is measured against now().
template <typename Now>
void sleepUntil(Clock::time_point deadline, Now now)
{
#ifdef _WIN32
    if (high_resolution_timer == nullptr) {
        high_resolution_timer.reset(CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE));
    }
    if (high_resolution_timer == nullptr) {
        std::this_thread::sleep_for(deadline - now());
        return;
    }
    const auto timer_wait = deadline - spin_window - now();
    if (timer_wait > Clock::duration::zero()) {
        LARGE_INTEGER due{};
        due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(
            timer_wait).count() / 100;
        if (due.QuadPart < 0 &&
            SetWaitableTimer(high_resolution_timer.get(), &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(high_resolution_timer.get(), INFINITE);
        } else {
            std::this_thread::sleep_for(deadline - now());
            return;
        }
    }
    while (now() < deadline) YieldProcessor();
#else
    const auto sleep_wait = deadline - spin_window - now();
    if (sleep_wait > Clock::duration::zero()) {
        std::this_thread::sleep_for(sleep_wait);
    }
    while (now() < deadline) {
#if defined(__aarch64__)
        __asm__ __volatile__("yield");
#else
        std::this_thread::yield();
#endif
    }
#endif
}
}

void recomp_d3d_vblank_reset(void)
{
    last_vblank = guest_now();
    completed_vblank = last_vblank;
    presented_vblank = last_vblank;
}

void recomp_d3d_wait_vblank(void)
{
    // Every waiter in this refresh interval observes the same vblank.
    // Keep the deadline local across guest switches and nested waits.
    const auto now = guest_now();
#if defined(__SWITCH__)
    // RECOMP_SOFT_VBLANK=1 (the Switch default): a frame that misses the
    // refresh by less than a quarter interval starts the next one at once
    // instead of waiting a whole extra refresh. DOA3 then slows down slightly
    // in its heaviest moments (as on hardware that drops a frame) rather than
    // falling to 30 fps, where it runs its logic twice a frame and can never
    // catch up again.
    static const bool soft = [] {
        const char *value = std::getenv("RECOMP_SOFT_VBLANK");
        return value != nullptr && value[0] == '1';
    }();
    if (soft && last_vblank + interval <= now && now < last_vblank + interval + interval / 4) {
        last_vblank = now;
        if (completed_vblank < now) completed_vblank = now;
        recomp_kernel_yield();  // peers that run while the main thread waits a refresh
        return;
    }
#endif
    if (last_vblank <= now) {
        last_vblank += interval * ((now - last_vblank) / interval + 1);
    }
    const auto deadline = last_vblank;
    const auto deadline_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            deadline.time_since_epoch()).count());
    recomp_kernel_wait_for_vblank(deadline_ns);
    if (!recomp_xapi_skip_wait(deadline_ns)) {
        // Deadlines are guest time, which runs ahead of host time after a skip.
        sleepUntil(deadline, guest_now);
    }
    if (completed_vblank < deadline) completed_vblank = deadline;
}

void recomp_d3d_wait_present(void)
{
    // A guest that already waited this refresh may submit immediately.
    // Present-only loops still consume one new refresh per submission.
    if (completed_vblank <= presented_vblank) recomp_d3d_wait_vblank();
    presented_vblank = completed_vblank;
}

void recomp_d3d_sleep_until(long long steady_ns)
{
    sleepUntil(Clock::time_point(std::chrono::nanoseconds(steady_ns)), Clock::now);
}
