// SPDX-License-Identifier: GPL-3.0-or-later
// Frame-time profiler for the Switch bring-up.
//
// Every hand-written adapter the game reaches (D3D draws and state, DirectSound,
// CRI, XInput...) and every Xbox kernel function is resolved through
// recomp_lookup_manual / recomp_lookup_kernel. Those lookups are wrapped at
// link time to return a timing thunk per target, so each adapter's exclusive
// time is measured without touching the runtime. The guest thread's idle
// sleeps (nanosleep in the scheduler) and the indirect-call lookups are
// timed as well; what remains of the wall time is the lifted game code.
//
// Guest threads are fibers on one host thread. Each fiber keeps its own scope
// stack, and time spent switched out is excluded from the scopes left open.
#include <switch.h>

#include <time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>

typedef void (*RecompFunction)(void);
// Layout of runtime.h's RecompFunctionEntry.
struct RecompFunctionEntryView { uint32_t address; RecompFunction function; };
extern "C" void *recomp_native_fiber_current(void);

namespace {

constexpr int kSlots = 384;
#if defined(DOA3_NX_FUNC_PROFILE)
constexpr int kDepth = 1024;  // lifted call chains nest deeply
#else
constexpr int kDepth = 64;
#endif
constexpr int kFibers = 32;

enum Kind : uint8_t { KIND_ADAPTER, KIND_KERNEL };

struct Slot {
    uint32_t address = 0;
    Kind kind = KIND_ADAPTER;
    RecompFunction target = nullptr;
    uint64_t ticks = 0;  // exclusive, this second
    uint64_t calls = 0;  // this second
};

struct Scope {
    uint64_t start;
    uint64_t child;
    int slot; // -1 idle, -2 lookup, -3 lifted function, otherwise slot index
    uint32_t func; // lifted function index when slot == -3
};

struct FiberStack {
    void *fiber = nullptr;
    int depth = 0;
    Scope scopes[kDepth];
};

Slot slots[kSlots];
int slot_count = 0;
int16_t slot_index[2048]; // open addressing on the address, -1 empty
bool tables_ready = false;

FiberStack fiber_stacks[kFibers];
FiberStack *current_stack = &fiber_stacks[0];
Handle guest_thread = INVALID_HANDLE;

#if defined(DOA3_NX_FUNC_PROFILE)
// Diagnostic build: every lifted function (-finstrument-functions on the
// generated chunks) is a scope, so the "game" time splits per function.
constexpr uint32_t kFuncs = 32768u;
struct FuncStat { void *fn; uint64_t ticks; uint64_t calls; };
FuncStat funcs[kFuncs];
uint32_t func_index[kFuncs * 2u]; // open addressing on fn, 0 empty, else index + 1
uint32_t func_count = 0;
uint64_t func_overflow = 0;

__attribute__((no_instrument_function)) uint32_t funcFor(void *fn)
{
    uint64_t h = reinterpret_cast<uintptr_t>(fn);
    h = (h ^ (h >> 17)) * 0x9e3779b97f4a7c15ull;
    for (uint32_t i = static_cast<uint32_t>(h >> 40) & (kFuncs * 2u - 1u);; i = (i + 1u) & (kFuncs * 2u - 1u)) {
        if (func_index[i] == 0u) {
            if (func_count == kFuncs) { ++func_overflow; return 0u; }
            funcs[func_count].fn = fn;
            func_index[i] = ++func_count;
            return func_count - 1u;
        }
        if (funcs[func_index[i] - 1u].fn == fn) return func_index[i] - 1u;
    }
}
#endif

uint64_t idle_ticks = 0;
uint64_t lookup_ticks = 0;
uint64_t lookup_calls = 0;
uint64_t lost_scopes = 0;

inline uint64_t now() { return armGetSystemTick(); }

void initTables()
{
    if (tables_ready) return;
    std::memset(slot_index, 0xff, sizeof slot_index);
    tables_ready = true;
}

bool onGuestThread()
{
    return guest_thread != INVALID_HANDLE && threadGetCurHandle() == guest_thread;
}

FiberStack *stackFor(void *fiber)
{
    for (auto &stack : fiber_stacks) {
        if (stack.fiber == fiber) return &stack;
    }
    for (auto &stack : fiber_stacks) {
        if (stack.fiber == nullptr) {
            stack.fiber = fiber;
            stack.depth = 0;
            return &stack;
        }
    }
    return &fiber_stacks[kFibers - 1];
}

inline void enter(int slot)
{
    FiberStack *stack = current_stack;
    if (stack->depth >= kDepth) {
        ++lost_scopes;
        return;
    }
    stack->scopes[stack->depth++] = Scope{now(), 0, slot, 0u};
}

inline void leave()
{
    FiberStack *stack = current_stack;
    if (stack->depth == 0) {
        ++lost_scopes;
        return;
    }
    const Scope scope = stack->scopes[--stack->depth];
    const uint64_t elapsed = now() - scope.start;
    const uint64_t exclusive = elapsed > scope.child ? elapsed - scope.child : 0;
    if (scope.slot >= 0) {
        slots[scope.slot].ticks += exclusive;
        ++slots[scope.slot].calls;
    } else if (scope.slot == -1) {
        idle_ticks += exclusive;
#if defined(DOA3_NX_FUNC_PROFILE)
    } else if (scope.slot == -3) {
        funcs[scope.func].ticks += exclusive;
        ++funcs[scope.func].calls;
#endif
    } else {
        lookup_ticks += exclusive;
        ++lookup_calls;
    }
    if (stack->depth != 0) stack->scopes[stack->depth - 1].child += elapsed;
}

template <int N>
void thunk()
{
    if (!onGuestThread()) {
        slots[N].target();
        return;
    }
    enter(N);
    slots[N].target();
    leave();
}

template <int... I>
constexpr std::array<RecompFunction, sizeof...(I)> makeThunks(std::integer_sequence<int, I...>)
{
    return {{&thunk<I>...}};
}

const auto thunks = makeThunks(std::make_integer_sequence<int, kSlots>{});

RecompFunction instrument(uint32_t address, RecompFunction target, Kind kind)
{
    if (target == nullptr) return nullptr;
    initTables();
    uint32_t h = (address * 2654435761u) >> 21; // 11 bits
    for (int probe = 0; probe < 2048; ++probe, h = (h + 1) & 2047u) {
        const int index = slot_index[h];
        if (index < 0) {
            if (slot_count >= kSlots) return target; // table full: untimed
            const int slot = slot_count++;
            slots[slot].address = address;
            slots[slot].kind = kind;
            slots[slot].target = target;
            slot_index[h] = (int16_t)slot;
            return thunks[slot];
        }
        if (slots[index].address == address) {
            slots[index].target = target;
            return thunks[index];
        }
    }
    return target;
}

// Stage timers: the draw adapter (guest thread) marks the boundaries of its
// steps, the render thread adds the time of its own. Reported per frame.
constexpr int kStageSets = 2;
constexpr int kStages = 12;
const char *const stage_names[kStageSets][kStages] = {
    {"prep", "xform", "state", "tex", "light", "fog", "submit", "tail", "", "", "", ""},
    {"exec", "draw", "texture", "upload", "glcalls", "present", "swap", "clear", "", "", "", ""},
};
std::atomic<uint64_t> stage_ticks[kStageSets][kStages];
uint64_t stage_last = 0;  // guest marks only
constexpr int kCounters = 4;
const char *const counter_names[kCounters] = {"vtx_kb_full", "vtx_kb_sent", "rebased", "vtx_kb_dedup"};
std::atomic<uint64_t> counters[kCounters];

} // namespace

// CPU / GPU / memory clocks, to tell a slow frame from an unclocked console
// (sys-clk profiles apply per title and per docked/handheld state).
namespace {
uint32_t readClockMhz(PcvModule legacy, PcvModuleId module)
{
    u32 hz = 0;
    if (hosversionAtLeast(8, 0, 0)) {
        ClkrstSession session;
        if (R_SUCCEEDED(clkrstOpenSession(&session, module, 3))) {
            if (R_FAILED(clkrstGetClockRate(&session, &hz))) hz = 0;
            clkrstCloseSession(&session);
        }
    } else if (R_FAILED(pcvGetClockRate(legacy, &hz))) {
        hz = 0;
    }
    return hz / 1000000u;
}

void logClocks()
{
    static bool ready = false, tried = false;
    if (!tried) {
        tried = true;
        ready = hosversionAtLeast(8, 0, 0) ? R_SUCCEEDED(clkrstInitialize()) : R_SUCCEEDED(pcvInitialize());
    }
    if (!ready) {
        std::fprintf(stderr, "recomp clocks: unavailable\n");
        return;
    }
    std::fprintf(stderr, "recomp clocks: cpu=%uMHz gpu=%uMHz mem=%uMHz\n",
        readClockMhz(PcvModule_CpuBus, PcvModuleId_CpuBus), readClockMhz(PcvModule_GPU, PcvModuleId_GPU),
        readClockMhz(PcvModule_EMC, PcvModuleId_EMC));
}
uint32_t reports = 0;
}

extern "C" {

void doa3_nx_stage_begin(void) { stage_last = now(); }

void doa3_nx_stage_mark(int stage)
{
    const uint64_t t = now();
    stage_ticks[0][stage].fetch_add(t - stage_last, std::memory_order_relaxed);
    stage_last = t;
}

uint64_t doa3_nx_ticks(void) { return now(); }

void doa3_nx_counter_add(int counter, uint64_t value)
{
    counters[counter].fetch_add(value, std::memory_order_relaxed);
}

void doa3_nx_render_stage_add(int stage, uint64_t ticks)
{
    stage_ticks[1][stage].fetch_add(ticks, std::memory_order_relaxed);
}

RecompFunction __real_recomp_lookup_manual(uint32_t guest_address);
RecompFunction doa3_nx_native_lookup(uint32_t guest_address); // doa3_nx_math.c

RecompFunction __wrap_recomp_lookup_manual(uint32_t guest_address)
{
    RecompFunction function = __real_recomp_lookup_manual(guest_address);
#if !defined(DOA3_NX_NATIVE_OFF)
    // Indirect calls to the native math helpers (direct calls: doa3_nx_native.h).
    if (function == nullptr) function = doa3_nx_native_lookup(guest_address);
#endif
    return instrument(guest_address, function, KIND_ADAPTER);
}

RecompFunction __real_recomp_lookup_kernel(uint32_t guest_address);
RecompFunction __wrap_recomp_lookup_kernel(uint32_t guest_address)
{
    return instrument(guest_address, __real_recomp_lookup_kernel(guest_address), KIND_KERNEL);
}

// Indirect calls in the lifted code resolve their target here first.
RecompFunction __real_recomp_program_lookup(uint32_t guest_address);

// ~5000 indirect calls a frame each walked the manual, generated and kernel
// tables. Targets of a static recompilation never move, so a direct-mapped
// cache answers almost all of them. Misses (and NULL results) take the slow
// path; only the guest thread fills the cache.
namespace {
constexpr uint32_t kLookupCacheSize = 8192u;
struct LookupCacheEntry { uint32_t address; RecompFunction function; };
LookupCacheEntry lookup_cache[kLookupCacheSize];
}

RecompFunction __wrap_recomp_program_lookup(uint32_t guest_address)
{
    if (!onGuestThread()) return __real_recomp_program_lookup(guest_address);
    LookupCacheEntry &entry = lookup_cache[(guest_address ^ (guest_address >> 13)) & (kLookupCacheSize - 1u)];
    if (entry.function != nullptr && entry.address == guest_address) {
        ++lookup_calls;
        return entry.function;
    }
    enter(-2);
    RecompFunction function = __real_recomp_program_lookup(guest_address);
    leave();
    if (function != nullptr) entry = LookupCacheEntry{guest_address, function};
    return function;
}

void __real_recomp_native_fiber_switch(void *target);
void __wrap_recomp_native_fiber_switch(void *target)
{
    FiberStack *own = current_stack;
    if (own->fiber == nullptr) own->fiber = recomp_native_fiber_current();
    const uint64_t out = now();
    current_stack = stackFor(target);
    __real_recomp_native_fiber_switch(target);
    current_stack = own;
    const uint64_t away = now() - out;
    if (own->depth != 0) own->scopes[own->depth - 1].child += away;
}

int __real_nanosleep(const struct timespec *request, struct timespec *remaining);
int __wrap_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (!onGuestThread()) return __real_nanosleep(request, remaining);
    enter(-1);
    const int rc = __real_nanosleep(request, remaining);
    leave();
    return rc;
}

#if defined(DOA3_NX_FUNC_PROFILE)
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *fn, void *)
{
    if (!onGuestThread()) return;
    FiberStack *stack = current_stack;
    if (stack->depth >= kDepth) { ++lost_scopes; return; }
    stack->scopes[stack->depth++] = Scope{now(), 0, -3, funcFor(fn)};
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *, void *)
{
    if (!onGuestThread()) return;
    leave();
}
#endif

#if defined(DOA3_NX_FUNC_PROFILE)
RecompFunction recomp_lookup(uint32_t guest_address); // generated dispatch
extern char __start__[];                              // libnx: image base

// The full program has no function table, so the guest addresses come from
// asking the dispatch about every address of the XBE's code once (about a
// second, at the first report of a diagnostic build).
static uint32_t guestAddressOf(void *fn)
{
    static std::unordered_map<void *, uint32_t> *reverse = nullptr;
    if (reverse == nullptr) {
        reverse = new std::unordered_map<void *, uint32_t>();
        reverse->reserve(1u << 15);
        for (uint32_t address = 0x00010000u; address < 0x00400000u; ++address) {
            RecompFunction function = recomp_lookup(address);
            if (function != nullptr) reverse->emplace(reinterpret_cast<void *>(function), address);
        }
        std::fprintf(stderr, "recomp lifted: %zu dispatch entries mapped\n", reverse->size());
    }
    const auto it = reverse->find(fn);
    return it == reverse->end() ? 0u : it->second;
}
#endif

static const RecompFunctionEntryView *function_table = nullptr;
static size_t function_table_count = 0;

void doa3_nx_profile_set_functions(const void *entries, size_t count)
{
    function_table = static_cast<const RecompFunctionEntryView *>(entries);
    function_table_count = count;
}

// The guest runs on the thread that calls recomp_runner_main.
void doa3_nx_profile_start(void)
{
    initTables();
    guest_thread = threadGetCurHandle();
    fiber_stacks[0].fiber = nullptr;
    current_stack = &fiber_stacks[0];
}

// Called once a second by the presenter with the frames and wall time of
// that second. Prints the breakdown per frame and resets the counters.
void doa3_nx_profile_report(uint32_t frames, double wall_ms, int console_row)
{
    if (frames == 0) return;
    if (reports++ % 10u == 0u) logClocks();
    const double tick_ms = 1000.0 / armGetSystemTickFreq();
    const double per_frame = 1.0 / frames;

    int order[kSlots];
    double adapter_ms = 0.0, kernel_ms = 0.0;
    for (int i = 0; i < slot_count; ++i) {
        order[i] = i;
        const double ms = slots[i].ticks * tick_ms;
        (slots[i].kind == KIND_KERNEL ? kernel_ms : adapter_ms) += ms;
    }
    std::sort(order, order + slot_count, [](int a, int b) { return slots[a].ticks > slots[b].ticks; });
    const double idle_ms = idle_ticks * tick_ms;
    const double lookup_ms = lookup_ticks * tick_ms;
    const double game_ms = wall_ms - idle_ms - adapter_ms - kernel_ms - lookup_ms;

    char top[512];
    int used = 0;
    int shown = 0;
    for (int i = 0; i < slot_count && shown < 6; ++i) {
        const Slot &s = slots[order[i]];
        if (s.ticks == 0) break;
        used += std::snprintf(top + used, sizeof top - used, " %s%08x=%.2fms/%llu",
            s.kind == KIND_KERNEL ? "k" : "", (unsigned)s.address,
            s.ticks * tick_ms * per_frame, (unsigned long long)(s.calls / frames));
        if (used >= (int)sizeof top) break;
        ++shown;
    }

    std::fprintf(stderr,
        "recomp profile: frame=%.2fms game=%.2f adapters=%.2f kernel=%.2f lookup=%.2f(%llu) idle=%.2f lost=%llu top:%s\n",
        wall_ms * per_frame, game_ms * per_frame, adapter_ms * per_frame, kernel_ms * per_frame,
        lookup_ms * per_frame, (unsigned long long)(lookup_calls / frames), idle_ms * per_frame,
        (unsigned long long)lost_scopes, top);

    for (int set = 0; set < kStageSets; ++set) {
        char line[512];
        int at = 0;
        for (int i = 0; i < kStages && stage_names[set][i][0]; ++i) {
            const uint64_t t = stage_ticks[set][i].exchange(0, std::memory_order_relaxed);
            at += std::snprintf(line + at, sizeof line - at, " %s=%.2f", stage_names[set][i], t * tick_ms * per_frame);
            if (at >= (int)sizeof line) break;
        }
        std::fprintf(stderr, "recomp stages %s:%s\n", set == 0 ? "draw-adapter" : "render-thread", line);
    }
    {
        char line[256];
        int at = 0;
        for (int i = 0; i < kCounters; ++i) {
            const uint64_t v = counters[i].exchange(0, std::memory_order_relaxed);
            const bool kb = std::strstr(counter_names[i], "_kb") != nullptr;
            at += std::snprintf(line + at, sizeof line - at, " %s=%.0f", counter_names[i],
                kb ? v / 1024.0 * per_frame : v * per_frame);
            if (at >= (int)sizeof line) break;
        }
        std::fprintf(stderr, "recomp counters:%s\n", line);
    }
#if defined(DOA3_NX_FUNC_PROFILE)
    {
        constexpr int kTop = 20;
        uint32_t best[kTop];
        int shown = 0;
        for (uint32_t i = 0; i < func_count; ++i) {
            if (funcs[i].ticks == 0) continue;
            int at;
            if (shown < kTop) {
                at = shown++;
            } else {
                at = kTop - 1;
                if (funcs[i].ticks <= funcs[best[at]].ticks) continue;
            }
            best[at] = i;
            while (at > 0 && funcs[best[at]].ticks > funcs[best[at - 1]].ticks) {
                std::swap(best[at], best[at - 1]);
                --at;
            }
        }
        char line[1400];
        int at = 0;
        for (int i = 0; i < shown; ++i) {
            const FuncStat &f = funcs[best[i]];
            const uint32_t guest = guestAddressOf(f.fn);
            if (guest != 0u) {
                at += std::snprintf(line + at, sizeof line - at, " %08x=%.2fms/%llu", (unsigned)guest,
                    f.ticks * tick_ms * per_frame, (unsigned long long)(f.calls / frames));
            } else {
                // Not a dispatch entry: the offset in the NRO, for build/switch/doa3-nx.map.
                at += std::snprintf(line + at, sizeof line - at, " +%lx=%.2fms/%llu",
                    (unsigned long)(reinterpret_cast<uintptr_t>(f.fn) - reinterpret_cast<uintptr_t>(__start__)),
                    f.ticks * tick_ms * per_frame, (unsigned long long)(f.calls / frames));
            }
            if (at >= (int)sizeof line) break;
        }
        std::fprintf(stderr, "recomp lifted top:%s (functions=%u overflow=%llu)\n", line, func_count,
            (unsigned long long)func_overflow);
        for (uint32_t i = 0; i < func_count; ++i) { funcs[i].ticks = 0; funcs[i].calls = 0; }
    }
#endif

    if (console_row < 0) {
        for (int i = 0; i < slot_count; ++i) {
            slots[i].ticks = 0;
            slots[i].calls = 0;
        }
        idle_ticks = lookup_ticks = lookup_calls = 0;
        return;
    }
    std::printf("\x1b[%d;1H", console_row);
    std::printf("Por frame:  juego %5.2f ms   adaptadores %5.2f ms\x1b[K\n",
        game_ms * per_frame, adapter_ms * per_frame);
    std::printf("            kernel %5.2f ms  busquedas %5.2f ms (%llu)\x1b[K\n",
        kernel_ms * per_frame, lookup_ms * per_frame, (unsigned long long)(lookup_calls / frames));
    std::printf("            espera %5.2f ms\x1b[K\n", idle_ms * per_frame);
    std::printf("Mas costosos:\x1b[K\n");
    for (int i = 0; i < 6; ++i) {
        if (i < slot_count && slots[order[i]].ticks != 0) {
            const Slot &s = slots[order[i]];
            std::printf("  %s 0x%08x  %5.2f ms  %6llu llamadas\x1b[K\n",
                s.kind == KIND_KERNEL ? "kernel " : "adapter", (unsigned)s.address,
                s.ticks * tick_ms * per_frame, (unsigned long long)(s.calls / frames));
        } else {
            std::printf("\x1b[K\n");
        }
    }

    for (int i = 0; i < slot_count; ++i) {
        slots[i].ticks = 0;
        slots[i].calls = 0;
    }
    idle_ticks = lookup_ticks = lookup_calls = 0;
}

}
