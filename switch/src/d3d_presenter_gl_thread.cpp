// SPDX-License-Identifier: GPL-3.0-or-later
// The d3d_presenter.h entry points on the Switch: the guest thread records
// each frame's commands into a D3dCapturePacket (deep copies of the vertex,
// index and texture bytes they point at) and a render thread on core 1 replays
// it through the OpenGL backend. A port of recomp-runtime/d3d_presenter_thread.cpp
// (the Windows D3D11 arrangement) to libnx: three packets, so the guest records
// frame N+1 while frame N renders, and never runs more than one frame ahead.
#include "d3d_presenter_capture.h"
#include "d3d_presenter_gl.h"

#include <switch.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>

extern "C" {
void doa3_nx_release_console(void);                                                // nx_main.cpp
void doa3_nx_profile_report(uint32_t frames, double wall_ms, int console_row);     // profile.cpp
}

namespace {

struct PresenterThread {
    Handle owner = threadGetCurHandle();
    Thread worker{};
    bool worker_started = false;
    RecompD3dPresenterConfig config{};
    std::mutex mutex;
    std::condition_variable changed;
    D3dCapturePacket packets[3];
    unsigned open = 0;
    unsigned front = 0;
    unsigned pending = 0; // includes the packet executing outside the mutex
    bool started = false;
    bool shutdown = false;
    bool immediate = false;
    bool immediate_dirty = true;
    RecompD3dPresenterError created = RECOMP_D3D_PRESENTER_OK;
    RecompD3dPresenterError status = RECOMP_D3D_PRESENTER_OK;
    RecompD3dPresenterError destroyed = RECOMP_D3D_PRESENTER_OK;
    unsigned draw_declines = 0;

    // Guest-side pacing, reported once a second like the desktop presenters.
    PadState pad{};
    uint64_t presents = 0;
    uint32_t second_presents = 0;
    double second_max_ms = 0.0;
    double queue_wait_ms = 0.0;
    std::chrono::steady_clock::time_point second_start{}, last_present{};
};

PresenterThread *active_thread = nullptr;
bool immediate_setting = false;

RecompD3dPresenterError validate(RecompD3dPresenter *presenter)
{
    if (presenter == nullptr || presenter != reinterpret_cast<RecompD3dPresenter *>(active_thread)) {
        return RECOMP_D3D_PRESENTER_NOT_INITIALIZED;
    }
    return threadGetCurHandle() == active_thread->owner
        ? RECOMP_D3D_PRESENTER_OK : RECOMP_D3D_PRESENTER_WRONG_THREAD;
}

RecompD3dPresenterError status(PresenterThread &thread)
{
    std::lock_guard<std::mutex> lock(thread.mutex);
    return thread.status;
}

void fail(PresenterThread &thread, RecompD3dPresenterError error)
{
    if (error == RECOMP_D3D_PRESENTER_OK) return;
    {
        std::lock_guard<std::mutex> lock(thread.mutex);
        if (thread.status == RECOMP_D3D_PRESENTER_OK) thread.status = error;
    }
    thread.changed.notify_all();
}

void execute(PresenterThread &thread, RecompD3dPresenter *backend, const D3dCapturePacket &packet)
{
    for (size_t i = 0; i < packet.count(); ++i) {
        if (status(thread) != RECOMP_D3D_PRESENTER_OK) break;
        const auto &record = packet.record(i);
        RecompD3dPresenterError error = RECOMP_D3D_PRESENTER_OK;
        bool draw = false;
        try {
            switch (record.kind) {
            case D3dCapturePacket::COMMAND: {
                const auto &command = packet.command(i);
                draw = command.type == RECOMP_D3D_PRESENTER_COMMAND_DRAW;
                error = gl_backend_submit(backend, &command);
                break;
            }
            case D3dCapturePacket::RELEASE:
                error = gl_backend_release_memory(backend, record.base, record.size);
                break;
            case D3dCapturePacket::REPORT:
                break;
            }
        } catch (const std::bad_alloc &) {
            error = RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
        } catch (...) {
            error = RECOMP_D3D_PRESENTER_HOST_FAILURE;
        }
        // A declined draw is skipped, as on the desktop; anything else stops.
        if (draw && error != RECOMP_D3D_PRESENTER_OK && error != RECOMP_D3D_PRESENTER_CLOSED) {
            if (thread.draw_declines++ < 8u) {
                std::fprintf(stderr, "recomp d3d presenter: draw declined error=%d fvf=0x%x\n",
                    static_cast<int>(error), packet.command(i).data.draw.fvf);
            }
            continue;
        }
        if (error != RECOMP_D3D_PRESENTER_OK) {
            std::fprintf(stderr, "recomp d3d presenter: render thread failed error=%d kind=%d command=%d\n",
                static_cast<int>(error), static_cast<int>(record.kind),
                record.kind == D3dCapturePacket::COMMAND ? static_cast<int>(packet.command(i).type) : -1);
            fail(thread, error);
        }
    }
}

void run(PresenterThread &thread, RecompD3dPresenterConfig config)
{
    std::fprintf(stderr, "[gl] render thread started on core %d\n", svcGetCurrentProcessorNumber());
    RecompD3dPresenter *backend = nullptr;
    RecompD3dPresenterError created;
    try {
        created = gl_backend_create(&config, &backend);
    } catch (const std::bad_alloc &) {
        created = RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
    } catch (...) {
        created = RECOMP_D3D_PRESENTER_HOST_FAILURE;
    }
    {
        std::lock_guard<std::mutex> lock(thread.mutex);
        thread.created = created;
        thread.status = created;
        thread.started = true;
    }
    thread.changed.notify_all();
    if (created != RECOMP_D3D_PRESENTER_OK) return;

    for (;;) {
        D3dCapturePacket *packet = nullptr;
        bool apply_immediate = false, immediate = false;
        {
            std::unique_lock<std::mutex> lock(thread.mutex);
            thread.changed.wait(lock, [&] { return thread.pending != 0 || thread.shutdown; });
            if (thread.pending == 0) break; // shutdown with nothing left
            packet = &thread.packets[thread.front];
            apply_immediate = thread.immediate_dirty;
            immediate = thread.immediate;
            thread.immediate_dirty = false;
        }
        if (apply_immediate) gl_backend_set_immediate_present(backend, immediate);
        execute(thread, backend, *packet);
        packet->clear();
        {
            std::lock_guard<std::mutex> lock(thread.mutex);
            thread.front = (thread.front + 1) % 3;
            --thread.pending;
        }
        thread.changed.notify_all();
    }
    thread.destroyed = gl_backend_destroy(&backend);
    std::fprintf(stderr, "recomp d3d presenter thread: draw declines=%u\n", thread.draw_declines);
}

// Mesa's GLSL compiler and EGL setup need far more than the 128 KiB a default
// libnx pthread gets, so the render thread is a libnx thread with an 8 MiB stack.
constexpr size_t kRenderStackBytes = 8u * 1024u * 1024u;

void renderEntry(void *argument)
{
    auto &thread = *static_cast<PresenterThread *>(argument);
    run(thread, thread.config);
}

void joinWorker(PresenterThread &thread)
{
    if (!thread.worker_started) return;
    threadWaitForExit(&thread.worker);
    threadClose(&thread.worker);
    thread.worker_started = false;
}

// Once a second: guest frame rate, worst frame, time spent waiting for the
// render thread, and the guest-thread profile.
void notePresent(PresenterThread &thread, double wait_ms)
{
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    ++thread.presents;
    thread.queue_wait_ms += wait_ms;
    if (thread.second_presents++ == 0u) {
        thread.second_start = now;
    } else {
        thread.second_max_ms = std::max(thread.second_max_ms,
            std::chrono::duration<double, std::milli>(now - thread.last_present).count());
    }
    thread.last_present = now;
    const double elapsed_ms = std::chrono::duration<double, std::milli>(now - thread.second_start).count();
    if (elapsed_ms < 1000.0) return;
    const uint32_t frames = thread.second_presents - 1u;
    std::fprintf(stderr,
        "recomp performance: present=%llu fps=%.2f frame_ms=%.3f max_ms=%.1f render_wait_ms=%.2f\n",
        static_cast<unsigned long long>(thread.presents), frames * 1000.0 / elapsed_ms,
        elapsed_ms / frames, thread.second_max_ms, thread.queue_wait_ms / frames);
    doa3_nx_profile_report(frames, elapsed_ms, -1);
    thread.second_presents = 1u;
    thread.second_start = now;
    thread.second_max_ms = 0.0;
    thread.queue_wait_ms = 0.0;
}

// HOME > close, or ZL+ZR+(-)+(+) held, ends the game like closing the window.
bool closeRequested(PresenterThread &thread)
{
    if (!appletMainLoop()) return true;
    padUpdate(&thread.pad);
    const u64 combo = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus | HidNpadButton_Plus;
    return (padGetButtons(&thread.pad) & combo) == combo;
}

RecompD3dPresenterError publish(PresenterThread &thread, bool destroying)
{
    const auto wait_start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(thread.mutex);
    thread.changed.wait(lock, [&] {
        return thread.pending < 2 || (!destroying && thread.status != RECOMP_D3D_PRESENTER_OK);
    });
    if (!destroying && thread.status != RECOMP_D3D_PRESENTER_OK) return thread.status;
    thread.packets[thread.open].seal();
    thread.open = (thread.open + 1) % 3;
    ++thread.pending;
    lock.unlock();
    thread.changed.notify_all();
    if (!destroying) {
        notePresent(thread, std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - wait_start).count());
    }
    return RECOMP_D3D_PRESENTER_OK;
}

} // namespace

extern "C" {

RecompD3dPresenterError recomp_d3d_presenter_create(
    const RecompD3dPresenterConfig *config, RecompD3dPresenter **presenter)
{
    if (config == nullptr || presenter == nullptr || config->width == 0 || config->height == 0 ||
        config->color_format != RECOMP_D3D_PRESENTER_COLOR_FORMAT_BGRA8_UNORM ||
        config->depth_format != RECOMP_D3D_PRESENTER_DEPTH_FORMAT_D24S8) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    if (*presenter != nullptr || active_thread != nullptr) return RECOMP_D3D_PRESENTER_ALREADY_INITIALIZED;

    // The text console draws to the same window EGL is about to take.
    doa3_nx_release_console();

    std::unique_ptr<PresenterThread> thread;
    try {
        thread = std::make_unique<PresenterThread>();
        thread->immediate = immediate_setting;
        padInitializeDefault(&thread->pad);
        thread->config = *config;
    } catch (const std::bad_alloc &) {
        return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
    }
    // Core 1 to itself; priority just above the default so the driver's
    // own helper threads cannot starve it.
    Result rc = threadCreate(&thread->worker, renderEntry, thread.get(), nullptr,
        kRenderStackBytes, 0x2B, 1);
    if (R_SUCCEEDED(rc)) rc = threadStart(&thread->worker);
    if (R_FAILED(rc)) {
        std::fprintf(stderr, "recomp d3d presenter: render thread create failed 0x%x\n", rc);
        return RECOMP_D3D_PRESENTER_HOST_FAILURE;
    }
    thread->worker_started = true;
    std::unique_lock<std::mutex> lock(thread->mutex);
    // A backend that hangs in EGL or the driver is reported instead of a black screen.
    if (!thread->changed.wait_for(lock, std::chrono::seconds(20), [&] { return thread->started; })) {
        lock.unlock();
        std::fprintf(stderr, "recomp d3d presenter: render thread did not start within 20 s\n");
        thread.release(); // the thread may still run; leak it rather than free under it
        return RECOMP_D3D_PRESENTER_HOST_FAILURE;
    }
    const auto error = thread->created;
    lock.unlock();
    if (error != RECOMP_D3D_PRESENTER_OK) {
        joinWorker(*thread);
        std::fprintf(stderr, "recomp d3d presenter: OpenGL backend failed error=%d\n", static_cast<int>(error));
        return error;
    }
    active_thread = thread.release();
    *presenter = reinterpret_cast<RecompD3dPresenter *>(active_thread);
    return RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError recomp_d3d_presenter_submit(
    RecompD3dPresenter *presenter, const RecompD3dPresenterCommand *command)
{
    auto error = validate(presenter);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    auto &thread = *active_thread;
    error = status(thread);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    if (command == nullptr) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    if (command->type == RECOMP_D3D_PRESENTER_COMMAND_PRESENT && closeRequested(thread)) {
        return RECOMP_D3D_PRESENTER_CLOSED;
    }
    error = thread.packets[thread.open].add(*command);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    return command->type == RECOMP_D3D_PRESENTER_COMMAND_PRESENT ? publish(thread, false)
                                                                : RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError recomp_d3d_presenter_release_memory(
    RecompD3dPresenter *presenter, uint32_t base, uint32_t size)
{
    auto error = validate(presenter);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    auto &thread = *active_thread;
    error = status(thread);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    return thread.packets[thread.open].addRelease(base, size);
}

RecompD3dPresenterError recomp_d3d_presenter_destroy(RecompD3dPresenter **presenter)
{
    if (presenter == nullptr) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    const auto error = validate(*presenter);
    if (error != RECOMP_D3D_PRESENTER_OK) return error;
    auto &thread = *active_thread;
    if (thread.packets[thread.open].count() != 0) publish(thread, true);
    {
        std::lock_guard<std::mutex> lock(thread.mutex);
        thread.shutdown = true;
    }
    thread.changed.notify_all();
    joinWorker(thread);
    const auto pending = thread.status == RECOMP_D3D_PRESENTER_CLOSED ? RECOMP_D3D_PRESENTER_OK : thread.status;
    const auto destroyed = thread.destroyed != RECOMP_D3D_PRESENTER_OK ? thread.destroyed : pending;
    delete active_thread;
    active_thread = nullptr;
    *presenter = nullptr;
    return destroyed;
}

void recomp_d3d_presenter_set_immediate_present(bool enabled)
{
    immediate_setting = enabled;
    if (active_thread == nullptr) return;
    std::lock_guard<std::mutex> lock(active_thread->mutex);
    active_thread->immediate = enabled;
    active_thread->immediate_dirty = true;
}

void recomp_d3d_presenter_report_draw_textures(void) {}

}
