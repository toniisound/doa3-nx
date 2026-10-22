// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch, first bring-up stage: a presenter that accepts every
// command but draws nothing. It answers the question that decides whether
// the port is worth a GPU backend: how fast does the lifted game run on the
// console's CPU? Once a second it prints the guest frame rate, draws per
// frame and the time the guest spent between presents on the libnx console.
//
// The real OpenGL presenter replaces this file; the interface is the same
// d3d_presenter.h seam the D3D11 and SDL_GPU backends implement.
#include "d3d_presenter.h"

#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

extern "C" void doa3_nx_presenter_took_console(void); // nx_main.cpp
extern "C" void doa3_nx_profile_report(uint32_t frames, double wall_ms, int console_row); // profile.cpp

struct RecompD3dPresenter {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t present_count = 0;
    uint64_t clear_count = 0;
    uint64_t draw_count = 0;
    uint64_t textured_draw_count = 0;
    uint64_t program_draw_count = 0;
    uint32_t second_presents = 0;
    uint64_t second_draws = 0;
    double second_max_ms = 0.0;
    std::chrono::steady_clock::time_point second_start{};
    std::chrono::steady_clock::time_point last_present{};
    std::chrono::steady_clock::time_point boot{};
    PadState pad{};
    bool close_requested = false;
};

namespace {

RecompD3dPresenter *active_presenter = nullptr;
bool immediate_present_setting = false;

void printHeader(const RecompD3dPresenter *p)
{
    std::printf("\x1b[2J\x1b[1;1H");
    std::printf("DOA3 NX - fase 1: prueba de rendimiento de CPU\n");
    std::printf("==============================================\n\n");
    std::printf("El juego corre sin graficos ni sonido: solo se mide\n");
    std::printf("a cuantos fotogramas por segundo avanza (objetivo: 60).\n\n");
    std::printf("Resolucion del juego: %ux%u\n", p->width, p->height);
    std::printf("Salir: HOME > cerrar, o ZL+ZR+(-)+(+) a la vez\n\n");
    consoleUpdate(nullptr);
}

void pollHost(RecompD3dPresenter *p)
{
    if (!appletMainLoop()) {
        p->close_requested = true;
        return;
    }
    padUpdate(&p->pad);
    const u64 held = padGetButtons(&p->pad);
    const u64 combo = HidNpadButton_ZL | HidNpadButton_ZR |
        HidNpadButton_Minus | HidNpadButton_Plus;
    if ((held & combo) == combo) p->close_requested = true;
}

void reportPerformance(RecompD3dPresenter *p)
{
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    if (p->second_presents++ == 0u) {
        p->second_start = now;
    } else {
        p->second_max_ms = std::max(p->second_max_ms,
            std::chrono::duration<double, std::milli>(now - p->last_present).count());
    }
    p->last_present = now;
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(now - p->second_start).count();
    if (elapsed_ms < 1000.0) return;

    const uint32_t frames = p->second_presents - 1u;
    const double fps = frames * 1000.0 / elapsed_ms;
    const double uptime =
        std::chrono::duration<double>(now - p->boot).count();
    const uint64_t draws = frames ? p->second_draws / frames : 0u;
    std::fprintf(stderr,
        "recomp performance: present=%llu fps=%.2f frame_ms=%.3f max_ms=%.1f draws=%llu\n",
        (unsigned long long)p->present_count, fps, elapsed_ms / frames,
        p->second_max_ms, (unsigned long long)draws);
    std::printf("\x1b[11;1H");
    std::printf("Tiempo: %7.1f s   Fotogramas: %-10llu\x1b[K\n",
        uptime, (unsigned long long)p->present_count);
    std::printf("FPS: %6.2f   (%.1f ms/frame, peor %.1f ms)\x1b[K\n",
        fps, elapsed_ms / frames, p->second_max_ms);
    std::printf("Dibujos por frame: %llu\x1b[K\n", (unsigned long long)draws);
    std::printf("Total dibujos: %llu (texturizados %llu, vertex program %llu)\x1b[K\n",
        (unsigned long long)p->draw_count,
        (unsigned long long)p->textured_draw_count,
        (unsigned long long)p->program_draw_count);
    doa3_nx_profile_report(frames, elapsed_ms, 16);
    consoleUpdate(nullptr);

    p->second_presents = 1u;
    p->second_start = now;
    p->second_max_ms = 0.0;
    p->second_draws = 0u;
}

} // namespace

extern "C" {

RecompD3dPresenterError recomp_d3d_presenter_create(
    const RecompD3dPresenterConfig *config,
    RecompD3dPresenter **presenter)
{
    if (config == nullptr || presenter == nullptr) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    if (config->width == 0u || config->height == 0u) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    if (*presenter != nullptr || active_presenter != nullptr) {
        return RECOMP_D3D_PRESENTER_ALREADY_INITIALIZED;
    }
    auto *p = new (std::nothrow) RecompD3dPresenter;
    if (p == nullptr) return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
    p->width = config->width;
    p->height = config->height;
    p->boot = std::chrono::steady_clock::now();
    padInitializeDefault(&p->pad);
    doa3_nx_presenter_took_console();
    printHeader(p);
    std::fprintf(stderr, "[presenter] Switch probe presenter %ux%u (no rendering)\n",
        p->width, p->height);
    active_presenter = p;
    *presenter = p;
    return RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError recomp_d3d_presenter_submit(
    RecompD3dPresenter *presenter,
    const RecompD3dPresenterCommand *command)
{
    if (presenter == nullptr || command == nullptr) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    if (presenter != active_presenter) return RECOMP_D3D_PRESENTER_NOT_INITIALIZED;
    if (presenter->close_requested) return RECOMP_D3D_PRESENTER_CLOSED;

    switch (command->type) {
    case RECOMP_D3D_PRESENTER_COMMAND_CLEAR:
        ++presenter->clear_count;
        return RECOMP_D3D_PRESENTER_OK;
    case RECOMP_D3D_PRESENTER_COMMAND_DRAW: {
        const RecompD3dPresenterDrawCommand &draw = command->data.draw;
        ++presenter->draw_count;
        ++presenter->second_draws;
        if (draw.has_texture) ++presenter->textured_draw_count;
        if (draw.program_count != 0u) ++presenter->program_draw_count;
        return RECOMP_D3D_PRESENTER_OK;
    }
    case RECOMP_D3D_PRESENTER_COMMAND_GAMMA:
        return RECOMP_D3D_PRESENTER_OK;
    case RECOMP_D3D_PRESENTER_COMMAND_PRESENT:
        ++presenter->present_count;
        pollHost(presenter);
        reportPerformance(presenter);
        return presenter->close_requested ? RECOMP_D3D_PRESENTER_CLOSED
                                          : RECOMP_D3D_PRESENTER_OK;
    }
    return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
}

RecompD3dPresenterError recomp_d3d_presenter_release_memory(
    RecompD3dPresenter *presenter, uint32_t base, uint32_t size)
{
    (void)base;
    (void)size;
    if (presenter == nullptr) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    return RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError recomp_d3d_presenter_destroy(RecompD3dPresenter **presenter)
{
    if (presenter == nullptr || *presenter == nullptr) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    if (*presenter == active_presenter) active_presenter = nullptr;
    delete *presenter;
    *presenter = nullptr;
    return RECOMP_D3D_PRESENTER_OK;
}

void recomp_d3d_presenter_set_immediate_present(bool enabled)
{
    immediate_present_setting = enabled;
}

void recomp_d3d_presenter_report_draw_textures(void) {}

}
