// SPDX-License-Identifier: GPL-3.0-or-later
// Boot tracing for the Switch bring-up. The runner's startup calls below are
// wrapped at link time (-Wl,--wrap=...), so each step lands in the log and
// on the boot screen without touching runner.cpp. The guest kernel-call
// counter shows whether the lifted game is still making progress.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

struct RecompMemoryRegion;
struct RecompMemoryAccess;
struct RecompFunctionEntry;

namespace {
std::atomic<uint64_t> kernel_calls{0};
}

extern "C" {

uint64_t doa3_nx_kernel_calls(void)
{
    return kernel_calls.load(std::memory_order_relaxed);
}

void __real_recomp_install_host_diagnostics(void);
void __wrap_recomp_install_host_diagnostics(void)
{
    std::fprintf(stderr, "[boot] host diagnostics\n");
    __real_recomp_install_host_diagnostics();
    std::fprintf(stderr, "[boot] host diagnostics ok\n");
}

bool __real_recomp_save_initialize(const char *disc_root);
bool __wrap_recomp_save_initialize(const char *disc_root)
{
    std::fprintf(stderr, "[boot] save init: %s\n", disc_root ? disc_root : "(null)");
    const bool ok = __real_recomp_save_initialize(disc_root);
    std::fprintf(stderr, "[boot] save init %s\n", ok ? "ok" : "FAILED");
    return ok;
}

void __real_recomp_runtime_init(const RecompMemoryRegion *regions, size_t region_count,
    RecompMemoryAccess *accesses, size_t access_capacity,
    const RecompFunctionEntry *functions, size_t function_count);
void __wrap_recomp_runtime_init(const RecompMemoryRegion *regions, size_t region_count,
    RecompMemoryAccess *accesses, size_t access_capacity,
    const RecompFunctionEntry *functions, size_t function_count)
{
    std::fprintf(stderr, "[boot] xbe mapped; runtime init\n");
    __real_recomp_runtime_init(regions, region_count, accesses, access_capacity,
        functions, function_count);
}

void __real_recomp_music_initialize(const char *folder);
void __wrap_recomp_music_initialize(const char *folder)
{
    std::fprintf(stderr, "[boot] music init: %s\n", folder ? folder : "(null)");
    __real_recomp_music_initialize(folder);
    std::fprintf(stderr, "[boot] music init ok\n");
}

bool __real_recomp_dispatch(uint32_t guest_address);
bool __wrap_recomp_dispatch(uint32_t guest_address)
{
    static bool first = true;
    if (first) {
        first = false;
        std::fprintf(stderr, "[boot] guest entry 0x%08x\n", (unsigned)guest_address);
    }
    return __real_recomp_dispatch(guest_address);
}

void __real_recomp_stop_note_kernel_call(void);
void __wrap_recomp_stop_note_kernel_call(void)
{
    kernel_calls.fetch_add(1, std::memory_order_relaxed);
    __real_recomp_stop_note_kernel_call();
}

}
