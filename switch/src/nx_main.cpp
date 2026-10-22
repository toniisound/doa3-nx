// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch entry point. runner.cpp is compiled with its main renamed to
// recomp_runner_main; this file sets up the console, the log file and the
// paths on the SD card, then starts the runner as the desktop launch scripts
// do: recomp_program_runner --xbe <disc>/default.xbe.
//
// SD card layout:
//   sdmc:/switch/doa3-nx/doa3-nx.nro
//   sdmc:/switch/doa3-nx/disc/default.xbe   (and the rest of the USA disc)
//   sdmc:/switch/doa3-nx/doa3-nx.log        (runtime log, rewritten each boot)
// Saves live in disc/.recomp-storage, as on PC.
#include <switch.h>

#include <fcntl.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef DOA3_NX_VERSION
#define DOA3_NX_VERSION "0.1.0"
#endif

int recomp_runner_main(int argc, char **argv); // runner.cpp main, renamed at compile time
extern "C" void __real_exit(int code) __attribute__((noreturn));

extern "C" {
static void flushLog(void); // below, with the log devoptab
}

namespace {

const char kRoot[] = "sdmc:/switch/doa3-nx";
const char kDisc[] = "sdmc:/switch/doa3-nx/disc";
const char kXbe[] = "sdmc:/switch/doa3-nx/disc/default.xbe";
const char kLog[] = "sdmc:/switch/doa3-nx/doa3-nx.log";
// The runner gets the path without the "sdmc:" device prefix (sdmc is the
// default device): win32_compat.c resolves guest file names case-insensitively
// one "/" component at a time, which needs the path to start at "/".
const char kRunnerXbe[] = "/switch/doa3-nx/disc/default.xbe";

bool console_ready = false;

bool fileExists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void waitForPlus()
{
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("\nPulsa (+) para salir.\n");
    consoleUpdate(nullptr);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
        svcSleepThread(16'000'000);
    }
}

// The last lines of the log, so a stop is readable on the console itself.
void printLogTail(int lines)
{
    std::fflush(stderr);
    flushLog();
    FILE *file = std::fopen(kLog, "rb");
    if (file == nullptr) return;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    const long start = size > 4096 ? size - 4096 : 0;
    std::fseek(file, start, SEEK_SET);
    std::string text(static_cast<size_t>(size - start), '\0');
    text.resize(std::fread(text.data(), 1, text.size(), file));
    std::fclose(file);
    size_t pos = text.size();
    int found = 0;
    while (pos > 0 && found <= lines) {
        pos = text.rfind('\n', pos - 1);
        if (pos == std::string::npos) { pos = 0; break; }
        ++found;
    }
    std::printf("\n--- final del log ---\n%s\n", text.c_str() + (pos ? pos + 1 : 0));
}

} // namespace

extern "C" {

// stderr goes to the log file at the devoptab level, so every thread's
// stderr stream (each newlib thread has its own) lands in the same file,
// while stdout stays on the on-screen console.
static int log_fd = -1;

// The last complete log line, shown on screen while the game boots.
static char last_line[96];
static char line_build[96];
static size_t line_length;

static void trackLastLine(const char *ptr, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        const char c = ptr[i];
        if (c == '\n') {
            line_build[line_length] = '\0';
            if (line_length != 0) std::memcpy(last_line, line_build, line_length + 1);
            line_length = 0;
        } else if (line_length + 1 < sizeof line_build && c >= ' ') {
            line_build[line_length++] = c;
        }
    }
}

// The runtime logs every guest file read and write; one SD write per line
// would cost the guest real time. Lines collect in memory instead and a
// helper thread on another core writes them out four times a second (and
// whenever the buffer fills, or before exit and crash reports).
static Mutex log_mutex;
static char log_buffer[256 * 1024];
static size_t log_used;
static Thread log_thread;
static volatile bool log_thread_running;

static void flushLogLocked()
{
    if (log_fd >= 0 && log_used != 0) write(log_fd, log_buffer, log_used);
    log_used = 0;
}

static void flushLog()
{
    mutexLock(&log_mutex);
    flushLogLocked();
    mutexUnlock(&log_mutex);
}

static void logFlusher(void *arg)
{
    (void)arg;
    while (log_thread_running) {
        svcSleepThread(250'000'000);
        flushLog();
    }
}

static ssize_t logWrite(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r;
    (void)fd;
    mutexLock(&log_mutex);
    trackLastLine(ptr, len);
    if (log_fd >= 0) {
        if (log_used + len > sizeof log_buffer) flushLogLocked();
        if (len > sizeof log_buffer) {
            write(log_fd, ptr, len);
        } else {
            std::memcpy(log_buffer + log_used, ptr, len);
            log_used += len;
        }
    }
    mutexUnlock(&log_mutex);
    return (ssize_t)len;
}

static devoptab_t log_devoptab;

static void redirectStderrToLog()
{
    mutexInit(&log_mutex);
    log_fd = open(kLog, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd < 0) return;
    log_thread_running = true;
    if (R_SUCCEEDED(threadCreate(&log_thread, logFlusher, nullptr, nullptr, 0x8000, 0x2D, 2))) {
        threadStart(&log_thread);
    }
    std::memset(&log_devoptab, 0, sizeof log_devoptab);
    log_devoptab.name = "doa3log";
    log_devoptab.write_r = logWrite;
    devoptab_list[STD_ERR] = &log_devoptab;
}

void doa3_nx_log_raw(const char *text, size_t length)
{
    // Crash path: the faulting thread may hold log_mutex, so no locking.
    if (log_fd >= 0) {
        if (log_used != 0) write(log_fd, log_buffer, log_used);
        log_used = 0;
        write(log_fd, text, length);
    }
}

bool doa3_nx_console_ready(void)
{
    return console_ready;
}

// libnx calls these around main.
void userAppInit(void)
{
    // hid is already up (libnx default); configure pads before anyone reads them.
    padConfigureInput(4, HidNpadStyleSet_NpadStandard);
    mkdir(kRoot, 0777);
}

void userAppExit(void)
{
    std::fflush(stderr);
    if (log_fd >= 0) {
        flushLog();
        fsync(log_fd);
        close(log_fd);
        log_fd = -1;
    }
    if (console_ready) {
        consoleExit(nullptr);
        console_ready = false;
    }
}

// host_diagnostics.cpp gives stderr a 64 KiB buffer and flushes it from a
// helper thread. newlib streams are per thread, so that thread never flushes
// the main thread's stderr; line buffering keeps the log current instead.
int __real_setvbuf(FILE *stream, char *buffer, int mode, size_t size);
int __wrap_setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    if (stream == stderr && mode == _IOFBF) mode = _IOLBF;
    return __real_setvbuf(stream, buffer, mode, size);
}

// libnx starts every pthread (std::thread included) on the main thread's
// core. The guest runs on the main thread (core 0) and never yields, so host
// threads move off it: the render thread asks for core 1, which it then has to
// itself, and the rest (DirectSound's voice pump) join the audio mixer on core 2.
static int next_thread_core = -1;

void doa3_nx_set_next_thread_core(int core)
{
    next_thread_core = core;
}

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start)(void *), void *arg);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start)(void *), void *arg)
{
    const int rc = __real_pthread_create(thread, attr, start, arg);
    const int requested = next_thread_core;
    next_thread_core = -1;
    if (rc != 0 || thread == nullptr) return rc;
    u64 mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) return rc;
    unsigned core = requested >= 0 ? static_cast<unsigned>(requested) : 2u;
    if (!(mask & (1ull << core))) core = (mask & 2ull) ? 1u : 0u;
    // pthread_t is libnx's struct __pthread_t*, whose first member is a Thread
    // and whose first field is the kernel handle.
    const Handle handle = *reinterpret_cast<const Handle *>(*thread);
    svcSetThreadCoreMask(handle, static_cast<s32>(core), 1u << core);
    std::fprintf(stderr, "[nx] host thread on core %u\n", core);
    return rc;
}

// Until the presenter takes the screen, a heartbeat shows the boot is alive.
static Mutex console_mutex;
static bool presenter_owns_console = false;

void doa3_nx_presenter_took_console(void)
{
    mutexLock(&console_mutex);
    presenter_owns_console = true;
    mutexUnlock(&console_mutex);
}

// The OpenGL presenter needs the window the text console draws to.
void doa3_nx_release_console(void)
{
    mutexLock(&console_mutex);
    presenter_owns_console = true;
    if (console_ready) {
        consoleExit(nullptr);
        console_ready = false;
    }
    mutexUnlock(&console_mutex);
    std::fprintf(stderr, "[nx] text console released for the renderer\n");
}

static Thread heartbeat_thread;
uint64_t doa3_nx_kernel_calls(void); // boot_trace.cpp
void doa3_nx_profile_start(void);     // profile.cpp

static void heartbeat(void *arg)
{
    (void)arg;
    const u64 start = armGetSystemTick();
    for (;;) {
        svcSleepThread(500'000'000);
        mutexLock(&console_mutex);
        if (presenter_owns_console || !console_ready) {
            mutexUnlock(&console_mutex);
            return;
        }
        const double seconds = armTicksToNs(armGetSystemTick() - start) / 1e9;
        char line[sizeof last_line];
        std::memcpy(line, last_line, sizeof line);
        line[sizeof line - 1] = '\0';
        std::printf("\x1b[6;1HArrancando... %5.1f s   llamadas al kernel: %llu\x1b[K\n\x1b[K%.78s\n",
            seconds, (unsigned long long)doa3_nx_kernel_calls(), line);
        consoleUpdate(nullptr);
        mutexUnlock(&console_mutex);
    }
}

// exit() is wrapped at link time (-Wl,--wrap=exit): a runtime stop with a
// nonzero code stays on screen instead of dropping back to the menu.
void __wrap_exit(int code)
{
    if (code != 0 && console_ready) {
        std::printf("\n\x1b[31mEl juego se ha detenido (codigo %d).\x1b[0m\n", code);
        std::printf("Log completo: %s\n", kLog);
        printLogTail(12);
        waitForPlus();
    }
    __real_exit(code);
}

} // extern "C"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(nullptr);
    console_ready = true;
    redirectStderrToLog();
    std::fprintf(stderr, "DOA3 NX %s (Toniisound), based on NoRain211/doa3-re\n",
        DOA3_NX_VERSION);

    std::printf("DOA3 NX %s\n\n", DOA3_NX_VERSION);
    if (!fileExists(kXbe)) {
        std::printf("No se encuentra %s\n\n", kXbe);
        std::printf("Copia el contenido del disco USA de Dead or Alive 3\n");
        std::printf("(extraido con extract-xiso) en %s/\n", kDisc);
        waitForPlus();
        consoleExit(nullptr);
        console_ready = false;
        return 0;
    }

    // Held at boot: R runs unpaced (as fast as the CPU allows, to measure
    // headroom above 60 fps). There is no CPU boost: boost mode would drop the
    // GPU to 76.8 MHz. Clock the console with sys-clk instead.
    PadState pad;
    padInitializeDefault(&pad);
    padUpdate(&pad);
    const u64 held = padGetButtons(&pad);
    if (held & HidNpadButton_R) {
        setenv("RECOMP_UNPACED", "1", 1);
        std::fprintf(stderr, "[nx] RECOMP_UNPACED=1 (R held at boot)\n");
    }
    std::printf("Modo: %s\n", (held & HidNpadButton_R) ? "sin limite (unpaced)" : "60 Hz");
    std::printf("Arrancando...\n");
    consoleUpdate(nullptr);
    mutexInit(&console_mutex);
    if (R_SUCCEEDED(threadCreate(&heartbeat_thread, heartbeat, nullptr, nullptr,
            0x10000, 0x2D, 1))) {
        threadStart(&heartbeat_thread);
    }

    // The runner resolves D:\ against the folder holding default.xbe.
    chdir(kRoot);
    static char arg0[] = "doa3-nx";
    static char arg1[] = "--xbe";
    static char arg2[sizeof kRunnerXbe];
    std::memcpy(arg2, kRunnerXbe, sizeof kRunnerXbe);
    char *runner_argv[] = {arg0, arg1, arg2, nullptr};
    doa3_nx_profile_start();
    int code = 1;
    try {
        code = recomp_runner_main(3, runner_argv);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[nx] uncaught exception: %s\n", error.what());
        std::printf("\n\x1b[31mExcepcion: %s\x1b[0m\n", error.what());
        waitForPlus();
        return 1;
    }
    std::fprintf(stderr, "[nx] runner returned %d\n", code);
    return code;
}
