// SPDX-License-Identifier: GPL-3.0-or-later
// Crash reporting for the Switch bring-up. A CPU exception (bad memory
// access, undefined instruction) or an uncaught C++ exception would close
// the app silently; instead the cause goes to the log and the screen.
//
// PC offsets are relative to the start of the .nro, the same addresses as
// build/switch/doa3-nx.map, so a crash can be located with:
//   aarch64-none-elf-addr2line -f -C -e build/switch/doa3-nx.elf 0x<pc offset>
#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

extern "C" {
extern char __start__; // libnx crt0: first byte of the module
void doa3_nx_log_raw(const char *text, size_t length); // nx_main.cpp
bool doa3_nx_console_ready(void);                     // nx_main.cpp

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

namespace {

void report(const char *text)
{
    doa3_nx_log_raw(text, std::strlen(text));
    if (doa3_nx_console_ready()) {
        std::printf("%s", text);
        consoleUpdate(nullptr);
    }
}

const char *describe(u32 desc)
{
    switch (desc) {
    case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
    case ThreadExceptionDesc_MisalignedPC: return "misaligned PC";
    case ThreadExceptionDesc_MisalignedSP: return "misaligned SP";
    case ThreadExceptionDesc_SError: return "SError";
    case ThreadExceptionDesc_BadSVC: return "bad SVC";
    case ThreadExceptionDesc_Trap: return "trap / undefined instruction";
    default: return "data abort / other";
    }
}

[[noreturn]] void holdAndExit()
{
    // Leave the report readable, then end the process.
    for (int i = 0; i < 60 * 10; ++i) svcSleepThread(100'000'000);
    svcExitProcess();
    __builtin_unreachable();
}

void onTerminate()
{
    char text[512];
    const char *what = "(no exception)";
    const char *type = "";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception &error) {
            what = error.what();
            type = typeid(error).name();
        } catch (...) {
            what = "(non-standard exception)";
        }
    }
    std::snprintf(text, sizeof text,
        "\n\x1b[31m[crash] uncaught C++ exception %s: %s\x1b[0m\n"
        "Se cerrara en 60 s. Pasa doa3-nx.log.\n", type, what);
    report(text);
    holdAndExit();
}

struct InstallTerminate {
    InstallTerminate() { std::set_terminate(onTerminate); }
} install_terminate;

} // namespace

// abort() (failed assert, std::terminate's default) ends the process without
// exit(); wrapped at link time so it reports first.
extern "C" [[noreturn]] void __wrap_abort(void)
{
    report("\n\x1b[31m[crash] abort()\x1b[0m\nSe cerrara en 60 s. Pasa doa3-nx.log.\n");
    holdAndExit();
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    const u64 base = reinterpret_cast<u64>(&__start__);
    char text[768];
    std::snprintf(text, sizeof text,
        "\n\x1b[31m[crash] %s (desc 0x%x)\x1b[0m\n"
        "  pc  = %016llx  (nro+0x%llx)\n"
        "  lr  = %016llx  (nro+0x%llx)\n"
        "  far = %016llx  esr = %08x\n"
        "  sp  = %016llx\n"
        "  x0=%llx x1=%llx x2=%llx x3=%llx\n"
        "Se cerrara en 60 s. Pasa doa3-nx.log.\n",
        describe(ctx->error_desc), ctx->error_desc,
        (unsigned long long)ctx->pc.x, (unsigned long long)(ctx->pc.x - base),
        (unsigned long long)ctx->lr.x, (unsigned long long)(ctx->lr.x - base),
        (unsigned long long)ctx->far.x, ctx->esr,
        (unsigned long long)ctx->sp.x,
        (unsigned long long)ctx->cpu_gprs[0].x, (unsigned long long)ctx->cpu_gprs[1].x,
        (unsigned long long)ctx->cpu_gprs[2].x, (unsigned long long)ctx->cpu_gprs[3].x);
    report(text);
    holdAndExit();
}
