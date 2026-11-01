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

extern "C" int doa3_nx_native_enabled; // doa3_nx_math.c

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

// The console is kept (black, no text) until the presenter takes the screen.
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

void doa3_nx_profile_start(void);     // profile.cpp

// exit() is wrapped at link time (-Wl,--wrap=exit): a runtime stop with a
// nonzero code stays on screen instead of dropping back to the menu.
#if defined(DOA3_NX_PGO_GEN)
void __gcov_dump(void);
#endif

void __wrap_exit(int code)
{
#if defined(DOA3_NX_PGO_GEN)
    // Profile-guided build, step 1: write the profile before anything can
    // stop the process (HOME close, or waiting on the error screen).
    mkdir("sdmc:/switch/doa3-nx/pgo", 0777);
    __gcov_dump();
    std::fprintf(stderr, "[nx] profile written to sdmc:/switch/doa3-nx/pgo\n");
    std::fflush(stderr);
#endif
    if (code != 0 && console_ready) {
        std::printf("\n\x1b[31mEl juego se ha detenido (codigo %d).\x1b[0m\n", code);
        std::printf("Log completo: %s\n", kLog);
        printLogTail(12);
        waitForPlus();
    }
    __real_exit(code);
}

} // extern "C"

namespace {

const char kOptions[] = "sdmc:/switch/doa3-nx/doa3-nx.ini";

struct BootOptions {
    bool native_math = true;
    bool soft_vblank = true;
    bool texcheck_once = false;
    bool gl_vsync = false;
    bool unpaced = false;
    bool widescreen = true;
    int resolution = 720; // render height: 720, 600 or 480 (the boot menu)
};

void writeOptions(const BootOptions &options)
{
    FILE *out = std::fopen(kOptions, "w");
    if (out == nullptr) return;
    std::fprintf(out,
        "; DOA3 NX options (1 = on, 0 = off). resolution is the last choice of the\n"
        "; boot menu (720, 600 or 480). Holding a button at boot also switches one\n"
        "; for that run: X native_math off, Y texcheck_once on, ZL soft_vblank off,\n"
        "; L gl_vsync on, R unpaced on.\n"
        "resolution=%d\nwidescreen=%d\nnative_math=%d\nsoft_vblank=%d\ntexcheck_once=%d\ngl_vsync=%d\nunpaced=%d\n",
        options.resolution, options.widescreen, options.native_math, options.soft_vblank,
        options.texcheck_once, options.gl_vsync, options.unpaced);
    std::fclose(out);
}

BootOptions readOptions()
{
    BootOptions options;
    FILE *file = std::fopen(kOptions, "r");
    if (file == nullptr) {
        writeOptions(options);
        return options;
    }
    char line[128];
    while (std::fgets(line, sizeof line, file)) {
        char key[64];
        int value = 0;
        if (line[0] == ';' || line[0] == '#') continue;
        if (std::sscanf(line, " %63[a-z_] = %d", key, &value) != 2) continue;
        const bool on = value != 0;
        if (!std::strcmp(key, "native_math")) options.native_math = on;
        else if (!std::strcmp(key, "soft_vblank")) options.soft_vblank = on;
        else if (!std::strcmp(key, "texcheck_once")) options.texcheck_once = on;
        else if (!std::strcmp(key, "gl_vsync")) options.gl_vsync = on;
        else if (!std::strcmp(key, "unpaced")) options.unpaced = on;
        else if (!std::strcmp(key, "widescreen")) options.widescreen = on;
        else if (!std::strcmp(key, "resolution") && (value == 720 || value == 600 || value == 480))
            options.resolution = value;
    }
    std::fclose(file);
    return options;
}

// Boot menu: the render resolution, every launch, the last choice marked.
// Returns false when the user leaves with + or HOME.
bool chooseResolution(PadState &pad, BootOptions &options)
{
    struct Choice { int height; const char *wide; const char *narrow; };
    static const Choice choices[] = {
        {720, "1280x720", "960x720"},
        {600, "1067x600", "800x600"},
        {480, "854x480", "640x480"},
    };
    constexpr int kCount = sizeof choices / sizeof choices[0];
    int selected = 0;
    for (int i = 0; i < kCount; ++i) {
        if (choices[i].height == options.resolution) selected = i;
    }
    bool redraw = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selected = (selected + kCount - 1) % kCount;
            redraw = true;
        }
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selected = (selected + 1) % kCount;
            redraw = true;
        }
        if (down & HidNpadButton_A) {
            options.resolution = choices[selected].height;
            return true;
        }
        if (down & HidNpadButton_Plus) return false;
        if (redraw) {
            redraw = false;
            std::printf("\x1b[2J\x1b[1;1H");
            std::printf("\n  DOA3 NX %s\n\n", DOA3_NX_VERSION);
            std::printf("  Resolucion\n\n");
            for (int i = 0; i < kCount; ++i) {
                std::printf("  %s %dp  (%s)%s\n", i == selected ? ">" : " ", choices[i].height,
                    options.widescreen ? choices[i].wide : choices[i].narrow,
                    i == 0 ? "  mejor imagen" : i == kCount - 1 ? "  original de Xbox" : "");
            }
            std::printf("\n  Arriba/Abajo: elegir    A: jugar    +: salir\n");
            consoleUpdate(nullptr);
        }
        svcSleepThread(16'000'000);
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(nullptr);
    console_ready = true;
    redirectStderrToLog();
    std::fprintf(stderr, "DOA3 NX %s (Toniisound), based on NoRain211/doa3-re\n",
        DOA3_NX_VERSION);
#if defined(DOA3_NX_BUILD_INFO)
    std::fprintf(stderr, "[nx] build: %s\n", DOA3_NX_BUILD_INFO);
#endif

    // The boot stays on a black screen until the intro; the console only
    // shows text for a missing disc or a stop (see __wrap_exit).
    if (!fileExists(kXbe)) {
        std::printf("DOA3 NX %s\n\n", DOA3_NX_VERSION);
        std::printf("No se encuentra %s\n\n", kXbe);
        std::printf("Copia el contenido del disco USA de Dead or Alive 3\n");
        std::printf("(extraido con extract-xiso) en %s/\n", kDisc);
        waitForPlus();
        consoleExit(nullptr);
        console_ready = false;
        return 0;
    }

#if defined(DOA3_NX_PGO_GEN)
    // Without the exit lock, HOME > Close kills the process outright and the
    // profile is never written; with it, the close arrives as a normal exit.
    appletLockExit();
    std::fprintf(stderr, "[nx] PGO profile build: exit with HOME > Close or ZL+ZR+(-)+(+)\n");
#endif
    // Options: sdmc:/switch/doa3-nx/doa3-nx.ini (written with the defaults on
    // the first run), or the same switches held at boot.
    BootOptions options = readOptions();

    // The first samples after boot can be empty: read the pad for half a
    // second, letting applet messages through, and keep every button seen.
    PadState pad;
    padInitializeDefault(&pad);
    u64 held = 0;
    for (int i = 0; i < 30; ++i) {
        appletMainLoop();
        padUpdate(&pad);
        held |= padGetButtons(&pad);
        svcSleepThread(16'000'000);
    }
    std::fprintf(stderr, "[nx] buttons held at boot: 0x%llx\n", (unsigned long long)held);
    if (held & HidNpadButton_R) options.unpaced = true;
    if (held & HidNpadButton_ZL) options.soft_vblank = false;
    if (held & HidNpadButton_X) options.native_math = false;
    if (held & HidNpadButton_Y) options.texcheck_once = true;
    if (held & HidNpadButton_L) options.gl_vsync = true;

    // unpaced: as fast as the CPU allows, to measure headroom above 60 fps.
    // There is no CPU boost: boost mode would drop the GPU to 76.8 MHz; clock
    // the console with sys-clk instead.
    if (options.unpaced) setenv("RECOMP_UNPACED", "1", 1);
    // soft_vblank: a frame a little over 16.7 ms starts the next one at once
    // instead of waiting a whole refresh (see d3d_vblank.cpp).
    if (options.soft_vblank) setenv("RECOMP_SOFT_VBLANK", "1", 1);
    // widescreen: the Xbox's widescreen video flag; the game draws 16:9 and
    // the renderer fills the screen (0: 4:3 with side bars).
    if (options.widescreen) setenv("RECOMP_D3D_WIDESCREEN", "1", 1);
    // native_math: native versions of the hottest math helpers (doa3_nx_math.c).
    doa3_nx_native_enabled = options.native_math ? 1 : 0;
    // texcheck_once: check cached textures once a frame, not on every draw
    // (faster, but the health bars can show empty).
    if (options.texcheck_once) setenv("RECOMP_GL_TEXCHECK_ONCE", "1", 1);
    // gl_vsync: the render thread also waits for vblank (old double vsync).
    if (options.gl_vsync) setenv("RECOMP_GL_VSYNC", "1", 1);
    if (!chooseResolution(pad, options)) {
        std::fprintf(stderr, "[nx] left from the boot menu\n");
        consoleExit(nullptr);
        console_ready = false;
        return 0;
    }
    writeOptions(options); // remembers the choice for the next menu
    {
        // The game renders 640x480; the presenter scales it to this height.
        char scale[16];
        std::snprintf(scale, sizeof scale, "%.4f", options.resolution / 480.0);
        setenv("RECOMP_D3D_SCALE", scale, 1);
    }
    std::printf("\x1b[2J");
    consoleUpdate(nullptr);
    std::fprintf(stderr, "[nx] resolution: %dp\n", options.resolution);
    std::fprintf(stderr, "[nx] options: widescreen=%d native_math=%d soft_vblank=%d texcheck_once=%d gl_vsync=%d unpaced=%d\n",
        options.widescreen, options.native_math, options.soft_vblank, options.texcheck_once, options.gl_vsync, options.unpaced);
    consoleUpdate(nullptr);
    mutexInit(&console_mutex);

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
