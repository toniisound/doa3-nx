#include "dsound_api_adapter.h"
#include "dsound_service_model.h"
#include "dsound_hrtf.h"
#include "audio_output.h"
#include "doa3_build.h"
#include "xbox_adpcm.h"
#include "kernel_abi.h"
#include "stop_report.h"
#include "xbox_memory_layout.h"
#include "xapi_time_adapter.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

/* DOA3 (DSOUND 3936) wrappers game code calls directly; each forwards to the
   CDirectSound/CDirectSoundBuffer method named. See docs/bring-up.md. */
enum {
    DIRECT_SOUND_CREATE = 0x001c7fd9u,        /* (guid, ppDS, punk) */
    DIRECT_SOUND_DO_WORK = 0x001c760du,       /* () */
    DS_USE_FULL_HRTF = 0x001c6b92u,         /* () */
    DS_SET_COOPERATIVE_LEVEL = 0x001c6b72u, /* retail no-op, 3 arguments */
    BUF_UNLOCK = 0x001c6b77u,              /* retail no-op, 5 arguments */
    DS_GET_SPEAKER_CONFIG = 0x001c7392u,      /* (ds, pdw) */
    DS_DOWNLOAD_EFFECTS_IMAGE = 0x001c73aeu,  /* (ds, pv, size, loc, ppDesc) */
    DS_SET_POSITION = 0x001c73d5u,            /* (ds, x, y, z, apply) */
    DS_SET_VELOCITY = 0x001c740au,            /* (ds, x, y, z, apply) */
    DS_COMMIT_DEFERRED_SETTINGS = 0x001c743fu, /* (ds) */
    DS_SET_I3DL2_LISTENER = 0x001c7457u,      /* (ds, p, apply) */
    DS_CREATE_SOUND_BUFFER = 0x001c7ea9u,     /* (ds, desc, ppBuffer, punk) */
    BUF_RELEASE = 0x001c6b7cu,                /* (b) */
    BUF_SET_VOLUME = 0x001c7477u,             /* (b, volume) */
    BUF_SET_MIX_BIN_VOLUMES = 0x001c7493u,    /* (b, mask, volumes) */
    BUF_SET_DISTANCE = 0x001c74b3u,           /* (b, distance, apply) */
    BUF_SET_POSITION = 0x001c74d7u,           /* (b, x, y, z, apply) */
    BUF_SET_VELOCITY = 0x001c750cu,           /* (b, x, y, z, apply) */
    BUF_STOP_EX = 0x001c7541u,                /* (b, time lo, time hi, flags) */
    BUF_SET_LOOP_REGION = 0x001c7565u,        /* (b, start, length) */
    BUF_GET_STATUS = 0x001c7585u,             /* (b, pdw) */
    BUF_GET_CURRENT_POSITION = 0x001c75a1u,   /* (b, pPlay, pWrite) */
    BUF_SET_CURRENT_POSITION = 0x001c75c1u,   /* (b, offset) */
    BUF_LOCK = 0x001c75ddu,  /* (b, off, bytes, pp1, pn1, pp2, pn2, flags) */
    BUF_SET_FREQUENCY = 0x001c7afbu,          /* (b, hz) */
    BUF_SET_MIX_BINS = 0x001c7b17u,           /* (b, mix bins) */
    BUF_PLAY = 0x001c7b33u,                   /* (b, r1, r2, flags) */
    BUF_STOP = 0x001c7b57u,                   /* (b) */
    BUF_SET_BUFFER_DATA = 0x001c7b6fu,        /* (b, pv, bytes) */
#ifdef RECOMP_DOA3_JP_BINDINGS
    /* DSOUND 4134 only; CRI mwSndOpenPort calls it after CreateSoundBuffer.
       Keyed by its Japan address. */
    BUF_SET_HEADROOM = 0x001f1da5u,           /* (b, headroom) */
#endif

    /* Handles live in unmapped guest space, so any direct guest access to a
       DirectSound object stops loudly at a recognisable address. */
    DIRECT_SOUND_HANDLE = 0x0eff0000u,
    BUFFER_HANDLE_BASE = 0x0f000000u,
    BUFFER_HANDLE_STRIDE = 0x40u,
    BUFFER_COUNT = 256u,

    DSBLOCK_FROMWRITECURSOR = 1u,
    DSBLOCK_ENTIREBUFFER = 2u,
    DSBSTATUS_PLAYING = 1u,
    DSBSTATUS_LOOPING = 4u,

    /* DSOUND full HRTF response bank and [61][31] index map; Japan 4134
       carries the same bytes. */
    HRTF_BANK = DOA3(0x001cee88u, 0x001f9660u),
    HRTF_BANK_SIZE = 0x115c0u,
    HRTF_MAP = DOA3(0x001e0448u, 0x0020ac20u),
    HRTF_MAP_SIZE = 0xec6u,
};

typedef struct Buffer {
    int used;
    uint32_t data, source_size, format, owned_data;
    uint32_t channels, bits;
    int32_t volume, headroom;
    const uint8_t *pcm;
    uint32_t samples_per_second;
    uint32_t block_align;
    RecompDsoundBufferModel model, output_model;
    /* CTRL3D mono voices: library 3D state, rendered through the HRTF. */
    int is3d;
    float position[3], velocity[3], min_distance, max_distance, doppler;
    int32_t attenuation; /* distance term, hundredths of dB */
    uint32_t base_hz;    /* SetFrequency before Doppler */
    RecompHrtfVoice hrtf;
} Buffer;

static Buffer buffers[BUFFER_COUNT];
static float listener_position_xyz[3], listener_velocity_xyz[3];

static int tracing(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("RECOMP_AUDIO_TRACE") != NULL;
    return enabled;
}

#ifdef _WIN32
static SRWLOCK audio_lock = SRWLOCK_INIT;
static volatile DWORD audio_owner;
#define LOCK_AUDIO() (AcquireSRWLockExclusive(&audio_lock), audio_owner = GetCurrentThreadId())
#define UNLOCK_AUDIO() (audio_owner = 0, ReleaseSRWLockExclusive(&audio_lock))
#else
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile pthread_t audio_owner;
static volatile int audio_owned;
#define LOCK_AUDIO() (pthread_mutex_lock(&audio_lock), audio_owner = pthread_self(), audio_owned = 1)
#define UNLOCK_AUDIO() (audio_owned = 0, pthread_mutex_unlock(&audio_lock))
#endif

void recomp_dsound_api_reset(void)
{
    LOCK_AUDIO();
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        if (buffers[i].used) recomp_audio_output_reset_voice(i);
        if (buffers[i].owned_data) recomp_kernel_free_pool(buffers[i].owned_data);
    }
    memset(buffers, 0, sizeof buffers);
    UNLOCK_AUDIO();
}

static uint64_t now_ms(void)
{
#ifdef RECOMP_DSOUND_TEST_CLOCK
    uint64_t recomp_test_dsound_now_ms(void);
    return recomp_test_dsound_now_ms();
#else
    return recomp_xapi_performance_counter() / 1000000u;
#endif
}

static void pump_buffer(Buffer *clock, uint64_t now)
{
    RecompDsoundBufferModel *output = &clock->output_model;
    if (!output->playing || clock->pcm == NULL ||
        now <= output->last_ms || now - output->last_ms < 10u) {
        return;
    }
    uint32_t channels = clock->channels;
    uint32_t bits = clock->bits;
    int adpcm = clock->format == 0x69u;
    uint32_t offset = 0u;
    uint32_t bytes = recomp_dsound_buffer_consume(output, now, &offset);
    if (bytes == 0u) {
        return;
    }
    /* Host output owns its copy; the decoder may refill the guest ring immediately. */
    int16_t samples[40000];
    uint8_t *pcm = (uint8_t *)samples;
    if (bytes > sizeof samples) {
        return;
    }
    if (adpcm) {
        uint32_t block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
        uint32_t decoded_bytes = XBOX_ADPCM_BLOCK_SAMPLES * channels * 2u;
        for (uint32_t written = 0u; written < bytes;) {
            uint8_t block[XBOX_ADPCM_BLOCK_BYTES * 2];
            int16_t decoded[XBOX_ADPCM_BLOCK_SAMPLES * 2];
            uint32_t within = offset % decoded_bytes;
            uint32_t count = decoded_bytes - within;
            if (count > bytes - written) count = bytes - written;
            memcpy(block, clock->pcm + offset / decoded_bytes * block_bytes,
                block_bytes);
            if (!xbox_adpcm_decode_block(block, block_bytes, channels,
                    decoded, XBOX_ADPCM_BLOCK_SAMPLES * 2u)) {
                output->playing = 0u;
                return;
            }
            memcpy(pcm + written, (uint8_t *)decoded + within, count);
            written += count;
            offset += count;
            if (offset == ((output->play_flags & RECOMP_DSOUND_PLAY_LOOPING) ? output->loop_end_bytes : output->size_bytes)) offset = output->loop_start_bytes;
        }
        bits = 16u;
    } else {
        for (uint32_t written = 0u; written < bytes;) {
            uint32_t end = (output->play_flags & RECOMP_DSOUND_PLAY_LOOPING) ? output->loop_end_bytes : output->size_bytes;
            uint32_t count = end - offset;
            if (count > bytes - written) count = bytes - written;
            memcpy(pcm + written, clock->pcm + offset, count);
            written += count;
            offset += count;
            if (offset == ((output->play_flags & RECOMP_DSOUND_PLAY_LOOPING) ? output->loop_end_bytes : output->size_bytes)) offset = output->loop_start_bytes;
        }
    }
    const uint32_t slot = (uint32_t)(clock - buffers);
    const int32_t volume = clock->volume + clock->attenuation;
    if (!clock->is3d) {
        recomp_audio_output_submit(slot, pcm, bytes, output->sample_rate, channels, bits, volume);
        return;
    }
    /* The APU filters 3D voices at 48 kHz; pieces keep each submission in bounds. */
    static float stereo[2u * 20000u];
    uint32_t limit = (uint32_t)((uint64_t)19000u * output->sample_rate / RECOMP_HRTF_RATE);
    if (limit == 0u) limit = 1u;
    for (uint32_t done = 0u, frames = bytes / 2u; done < frames;) {
        uint32_t piece = frames - done < limit ? frames - done : limit;
        uint32_t rendered = recomp_hrtf_render(&clock->hrtf, samples + done, piece,
            output->sample_rate, stereo, 20000u);
        if (rendered) recomp_audio_output_submit(slot, (const uint8_t *)stereo, rendered * 8u, RECOMP_HRTF_RATE, 2u, 32u, volume);
        done += piece;
    }
}

#if defined(RECOMP_FULL_PROGRAM)
static volatile int output_pump_stop;
#ifdef _WIN32
static HANDLE output_pump;
#else
static pthread_t output_pump;
static int output_pump_started;
#endif

/* Feeds host output while the game thread is busy, as the APU plays from
   guest rings on its own. A frame hitch then delays only the guest's refills,
   not audio it has already written. */
#ifdef _WIN32
static DWORD WINAPI output_pump_loop(void *unused)
#else
static void *output_pump_loop(void *unused)
#endif
{
    (void)unused;
#ifdef __APPLE__
    /* The Windows pump runs at time-critical priority. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    while (!output_pump_stop) {
#ifdef _WIN32
        Sleep(5u);
#else
        const struct timespec pause = {0, 5000000L};
        nanosleep(&pause, NULL);
#endif
        LOCK_AUDIO();
        uint64_t now = now_ms();
        for (uint32_t i = 0u; i < BUFFER_COUNT; ++i) {
            if (buffers[i].used) pump_buffer(&buffers[i], now);
        }
        UNLOCK_AUDIO();
    }
    return 0;
}

static void stop_output_pump(void)
{
    output_pump_stop = 1;
    /* Output teardown follows this handler, so the pump must be out of its
       pass. Exiting under the lock (recomp_stop in a locked adapter path)
       already parks it for good; joining then would deadlock. */
#ifdef _WIN32
    if (audio_owner != GetCurrentThreadId()) {
        if (output_pump) WaitForSingleObject(output_pump, INFINITE);
#else
    if (!audio_owned || !pthread_equal(audio_owner, pthread_self())) {
        if (output_pump_started) pthread_join(output_pump, NULL);
        output_pump_started = 0;
#endif
        /* The watchdog exits from another thread while guest calls continue.
           Park those callers before backend teardown; exit never releases it. */
        LOCK_AUDIO();
    }
}

static void start_output_pump(void)
{
    static int shutdown_registered;
    if (!shutdown_registered) {
        atexit(stop_output_pump);
        shutdown_registered = 1;
    }
    /* The pump would advance the guest-visible cursor at host-chosen times.
       DirectSoundDoWork and buffer calls pump on the guest thread instead. */
    if (recomp_xapi_deterministic) return;
#ifdef _WIN32
    if (output_pump != NULL || !recomp_audio_output_enabled()) return;
    output_pump = CreateThread(NULL, 0u, output_pump_loop, NULL, 0u, NULL);
    if (output_pump != NULL) {
        SetThreadPriority(output_pump, THREAD_PRIORITY_TIME_CRITICAL);
    }
#else
    if (output_pump_started || !recomp_audio_output_enabled()) return;
    output_pump_started = pthread_create(&output_pump, NULL, output_pump_loop, NULL) == 0;
#endif
}
#endif

static Buffer *buffer_at(uint32_t handle)
{
    uint32_t slot = (handle - BUFFER_HANDLE_BASE) / BUFFER_HANDLE_STRIDE;

    if (handle < BUFFER_HANDLE_BASE ||
        (handle - BUFFER_HANDLE_BASE) % BUFFER_HANDLE_STRIDE != 0u ||
        slot >= BUFFER_COUNT || !buffers[slot].used) {
        recomp_stop(2, "dsound:buffer:0x%08x", (unsigned)handle);
    }
    return &buffers[slot];
}

static void set_u32(uint32_t address, uint32_t value)
{
    if (address != 0u) {
        *recomp_memory_u32(address) = value;
    }
}

static uint32_t decoded_bytes(const Buffer *buffer, uint32_t bytes)
{
    return buffer->format == 0x69u
        ? bytes / buffer->block_align * XBOX_ADPCM_BLOCK_SAMPLES * buffer->channels * 2u
        : bytes;
}

static uint32_t source_bytes(const Buffer *buffer, uint32_t bytes)
{
    return buffer->format == 0x69u
        ? bytes / (XBOX_ADPCM_BLOCK_SAMPLES * buffer->channels * 2u) * buffer->block_align
        : bytes;
}

static void reset_output(Buffer *buffer)
{
    recomp_audio_output_reset_voice((uint32_t)(buffer - buffers));
    buffer->output_model = buffer->model;
    RecompHrtfFilter filter = buffer->hrtf.filter;
    memset(&buffer->hrtf, 0, sizeof buffer->hrtf);
    buffer->hrtf.filter = buffer->hrtf.rendered = filter;
}

/* 0x001C9B9A: total 3D pitch clamps to [-32767, 8191] at 4096 per octave from 48 kHz. */
static uint32_t doppler_hz(const Buffer *buffer)
{
    if (!buffer->is3d) return buffer->base_hz;
    double hz = buffer->base_hz * (double)buffer->doppler;
    return (uint32_t)lround(hz < 187.53 ? 187.53 : hz > 191967.5 ? 191967.5 : hz);
}

static uint32_t apply_frequency(Buffer *buffer)
{
    const uint32_t hz = doppler_hz(buffer);
    pump_buffer(buffer, now_ms());
    uint32_t result = recomp_dsound_buffer_set_frequency(&buffer->model, hz, now_ms());
    /* Retime only: advancing the output cursor would skip audio the pump has
       not sent yet (it waits 10 ms between sends). */
    if (result == RECOMP_DSOUND_OK) buffer->output_model.sample_rate = buffer->model.sample_rate;
    return result;
}

/* DSOUND 3936 per-voice 3D terms with the listener factors, orientation and
   cones DOA3 leaves at their defaults (all factors 1, +z ahead, +y up). */
static void update_3d(Buffer *buffer)
{
    static const uint8_t *bank, *map;
    float d[3], n[3] = {0.0f, 0.0f, 0.0f}, u = 0.0f, azimuth, elevation;
    if (!buffer->is3d) return;
    for (int i = 0; i < 3; ++i) d[i] = buffer->position[i] - listener_position_xyz[i];
    const float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; distance > 0.0f && i < 3; ++i) n[i] = d[i] / distance;

    /* 0x001C9F93, rolloff 1: inverse distance from min distance, held past max. */
    const float m = buffer->min_distance, cap = buffer->max_distance;
    if (distance <= m) {
        buffer->attenuation = 0;
    } else {
        float a = truncf(-2000.0f * log10f(distance < cap ? distance / m : cap / m));
        /* NaN from non-finite guest positions takes the silent branch. */
        buffer->attenuation = !(a > -10000.0f) ? -10000 : a > 0.0f ? 0 : (int32_t)a;
    }

    /* 0x001CA31A: linear Doppler, radial speed clamped below the speed of sound (342). */
    for (int i = 0; i < 3; ++i) u += (buffer->velocity[i] - listener_velocity_xyz[i]) * n[i];
    u = !isfinite(u) ? 0.0f : u < -341.0f ? -341.0f : u > 341.0f ? 341.0f : u;
    buffer->doppler = fminf(10.0f, 1.0f - u / 342.0f);
    if (doppler_hz(buffer) != buffer->output_model.sample_rate && buffer->output_model.size_bytes) {
        apply_frequency(buffer);
    }

    if (bank == NULL) {
        bank = recomp_memory(HRTF_BANK, HRTF_BANK_SIZE);
        map = recomp_memory(HRTF_MAP, HRTF_MAP_SIZE);
    }
    recomp_hrtf_angles(n[0], n[1], n[2], &azimuth, &elevation);
    buffer->hrtf.filter = recomp_hrtf_lookup(bank, map, azimuth, elevation);
}

static uint32_t set_data(Buffer *buffer, uint32_t data, uint32_t size)
{
    if (size && (!data || size % buffer->block_align ||
        (buffer->format == 0x69u &&
         (uint64_t)(size / buffer->block_align) * XBOX_ADPCM_BLOCK_SAMPLES *
            buffer->channels * 2u > UINT32_MAX))) {
        return RECOMP_DSOUND_INVALID_PARAM;
    }
    buffer->data = data;
    buffer->source_size = size;
    buffer->pcm = size ? recomp_memory(data, size) : NULL;
    memset(&buffer->model, 0, sizeof buffer->model);
    if (size) {
        recomp_dsound_buffer_configure(&buffer->model, decoded_bytes(buffer, size),
            buffer->samples_per_second, buffer->channels * (buffer->format == 0x69u ? 2u : buffer->bits / 8u), now_ms());
    }
    reset_output(buffer);
    return RECOMP_DSOUND_OK;
}

static uint32_t aligned(const Buffer *buffer, uint32_t offset)
{
    return source_bytes(buffer, offset) / buffer->block_align * buffer->block_align;
}

static void direct_sound_create(void)
{
#if defined(RECOMP_FULL_PROGRAM)
    start_output_pump();
#endif
    set_u32(kernel_arg(2u), DIRECT_SOUND_HANDLE);
    kernel_return(3u, RECOMP_DSOUND_OK);
}

static void create_sound_buffer(void)
{
    uint32_t desc = kernel_arg(2u);
    uint32_t output = kernel_arg(3u);
    uint32_t size = *recomp_memory_u32(desc + 8u);
    uint32_t format = *recomp_memory_u32(desc + 12u);
    uint32_t slot = 0u;
    /* DSOUND 3936 voice initialization (0x001C76DF): ordinary voices reserve
       6 dB; CTRL3D and the two mix-in flags reserve none. */
    int32_t headroom = (*recomp_memory_u32(desc + 4u) & 0x82010u) ? 0 : 600;
    Buffer *buffer;

    while (slot < BUFFER_COUNT && buffers[slot].used) {
        ++slot;
    }
    if (slot == BUFFER_COUNT || format == 0u || output == 0u) {
        recomp_stop(2, "dsound:create-buffer:slot=%u format=0x%08x",
                    (unsigned)slot, (unsigned)format);
    }
    buffer = &buffers[slot];
    *buffer = (Buffer){
        .used = 1,
        .format = *recomp_memory_u16(format),
        .channels = *recomp_memory_u16(format + 2u),
        .bits = *recomp_memory_u16(format + 14u),
        .samples_per_second = *recomp_memory_u32(format + 4u),
        .block_align = *recomp_memory_u16(format + 12u),
        .headroom = headroom,
        .volume = -headroom,
        .min_distance = 1.0f,
        .max_distance = 1000000000.0f,
        .doppler = 1.0f,
    };
    if (size > UINT32_MAX - 31u ||
        (buffer->format != 1u && buffer->format != 0x69u) ||
        buffer->channels < 1u || buffer->channels > 2u ||
        buffer->samples_per_second < 1000u || buffer->samples_per_second > 200000u ||
        (buffer->format == 1u && ((buffer->bits != 8u && buffer->bits != 16u) ||
            buffer->block_align != buffer->channels * buffer->bits / 8u)) ||
        (buffer->format == 0x69u && (buffer->bits != 4u ||
            buffer->block_align != XBOX_ADPCM_BLOCK_BYTES * buffer->channels))) {
        buffer->used = 0;
        kernel_return(4u, RECOMP_DSOUND_INVALID_PARAM);
        return;
    }
    buffer->base_hz = buffer->samples_per_second;
    buffer->is3d = (*recomp_memory_u32(desc + 4u) & 0x10u) && buffer->channels == 1u &&
        (buffer->format == 0x69u || buffer->bits == 16u);
    update_3d(buffer);
    buffer->hrtf.rendered = buffer->hrtf.filter;
    if (tracing()) fprintf(stderr, "recomp audio: create slot=%u tag=%x channels=%u bits=%u rate=%u bytes=%u flags=0x%x\n",
        slot, buffer->format, buffer->channels, buffer->bits, buffer->samples_per_second, size,
        *recomp_memory_u32(desc + 4u));
    if (size != 0u) {
        buffer->owned_data = recomp_kernel_allocate_pool(size + 31u);

        if (buffer->owned_data == 0u) {
            recomp_stop(2, "dsound:create-buffer:alloc=%u", (unsigned)size);
        }
        uint32_t data = (buffer->owned_data + 31u) & ~31u;
        uint32_t result = set_data(buffer, data, size);
        if (result != RECOMP_DSOUND_OK) {
            recomp_kernel_free_pool(buffer->owned_data);
            buffer->owned_data = 0u;
            buffer->used = 0;
            kernel_return(4u, result);
            return;
        }
    }
    set_u32(output, BUFFER_HANDLE_BASE + slot * BUFFER_HANDLE_STRIDE);
    kernel_return(4u, RECOMP_DSOUND_OK);
}

static void buffer_release(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    recomp_audio_output_reset_voice((uint32_t)(buffer - buffers));
    if (buffer->owned_data) recomp_kernel_free_pool(buffer->owned_data);
    buffer->owned_data = 0u;
    buffer->used = 0;
    kernel_return(1u, 0u);
}

static void buffer_set_data(void)
{
    if (tracing()) fprintf(stderr, "recomp audio: data slot=%u bytes=%u\n",
        (unsigned)(buffer_at(kernel_arg(1u)) - buffers), kernel_arg(3u));
    kernel_return(3u, set_data(buffer_at(kernel_arg(1u)), kernel_arg(2u), kernel_arg(3u)));
}

static void buffer_lock(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    uint32_t size = buffer->source_size;
    uint32_t offset = kernel_arg(2u);
    uint32_t bytes = kernel_arg(3u);
    uint32_t flags = kernel_arg(8u);
    uint32_t first;

    if (size == 0u) {
        kernel_return(8u, RECOMP_DSOUND_INVALID_PARAM);
        return;
    }
    if (flags & DSBLOCK_FROMWRITECURSOR) {
        offset = aligned(buffer,
                         recomp_dsound_buffer_cursor(&buffer->model, now_ms()));
    }
    if ((flags & DSBLOCK_ENTIREBUFFER) || bytes > size) {
        bytes = size;
    }
    offset %= size;
    first = bytes < size - offset ? bytes : size - offset;
    set_u32(kernel_arg(4u), buffer->data + offset);
    set_u32(kernel_arg(5u), first);
    set_u32(kernel_arg(6u), bytes > first ? buffer->data : 0u);
    set_u32(kernel_arg(7u), bytes - first);
    kernel_return(8u, RECOMP_DSOUND_OK);
}

static void buffer_play(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));

    uint64_t now = now_ms();
    uint32_t flags = kernel_arg(4u) & (RECOMP_DSOUND_PLAY_LOOPING | RECOMP_DSOUND_PLAY_FROMSTART);
    pump_buffer(buffer, now);
    int continuing = buffer->model.playing && buffer->output_model.playing &&
        !(flags & RECOMP_DSOUND_PLAY_FROMSTART);
    uint32_t result = recomp_dsound_buffer_play(&buffer->model, flags, now);
    if (tracing()) fprintf(stderr, "recomp audio: play tick=%llu slot=%u bytes=%u rate=%u flags=%u result=%x\n",
        (unsigned long long)now, (unsigned)(buffer - buffers), buffer->source_size,
        buffer->model.sample_rate, flags, result);
    if (result == RECOMP_DSOUND_OK && !continuing) reset_output(buffer);
    kernel_return(4u, result);
}

static void stop_buffer(Buffer *buffer, uint32_t flags)
{
    uint64_t now = now_ms();
    if (tracing()) fprintf(stderr, "recomp audio: stop tick=%llu slot=%u flags=%u\n",
        (unsigned long long)now, (unsigned)(buffer - buffers), flags);
    pump_buffer(buffer, now);
    if ((flags & 3u) == 3u && buffer->model.playing &&
        (buffer->model.play_flags & RECOMP_DSOUND_PLAY_LOOPING)) {
        recomp_dsound_buffer_cursor(&buffer->model, now);
        buffer->model.play_flags &= ~RECOMP_DSOUND_PLAY_LOOPING;
        buffer->output_model.play_flags = buffer->model.play_flags;
    } else {
        recomp_dsound_buffer_stop(&buffer->model, now);
        reset_output(buffer);
    }
}

static void buffer_stop(void)
{
    stop_buffer(buffer_at(kernel_arg(1u)), 0u);
    kernel_return(1u, RECOMP_DSOUND_OK);
}

static void buffer_stop_ex(void)
{
    /* ponytail: zero timestamps cover observed callers; schedule nonzero
       timestamps and release envelopes when a caller requires them. */
    stop_buffer(buffer_at(kernel_arg(1u)), kernel_arg(4u));
    kernel_return(4u, RECOMP_DSOUND_OK);
}

static void buffer_get_status(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    uint32_t status = 0u;

    recomp_dsound_buffer_cursor(&buffer->model, now_ms());
    if (buffer->model.playing) {
        status = DSBSTATUS_PLAYING |
            ((buffer->model.play_flags & RECOMP_DSOUND_PLAY_LOOPING)
                 ? DSBSTATUS_LOOPING : 0u);
    }
    set_u32(kernel_arg(2u), status);
    kernel_return(2u, RECOMP_DSOUND_OK);
}

static void buffer_get_current_position(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    /* Report what the pump has sent: the game refills everything behind this
       cursor, and the pump trails the clock by up to 15 ms. Pumping here keeps
       the cursor moving when no pump thread runs (muted or no host output). */
    recomp_dsound_buffer_cursor(&buffer->model, now_ms());
    pump_buffer(buffer, now_ms());
    uint32_t cursor = aligned(buffer, buffer->output_model.cursor_bytes);

    set_u32(kernel_arg(2u), cursor);
    set_u32(kernel_arg(3u), cursor);
    kernel_return(3u, RECOMP_DSOUND_OK);
}

static void buffer_set_current_position(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    uint32_t position = kernel_arg(2u);
    uint32_t result = RECOMP_DSOUND_INVALID_PARAM;
    if (position < buffer->source_size && position % buffer->block_align == 0u) {
        pump_buffer(buffer, now_ms());
        result = recomp_dsound_buffer_set_position(&buffer->model, decoded_bytes(buffer, position), now_ms());
        if (result == RECOMP_DSOUND_OK) reset_output(buffer);
    }
    kernel_return(2u, result);
}

static void buffer_set_loop_region(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    uint32_t start = kernel_arg(2u), length = kernel_arg(3u);
    if (start >= buffer->source_size || length == 0u || length > buffer->source_size - start ||
        start % buffer->block_align || length % buffer->block_align) {
        kernel_return(3u, RECOMP_DSOUND_INVALID_PARAM);
        return;
    }
    pump_buffer(buffer, now_ms());
    buffer->model.loop_start_bytes = decoded_bytes(buffer, start);
    buffer->model.loop_end_bytes = decoded_bytes(buffer, start + length);
    reset_output(buffer);
    kernel_return(3u, RECOMP_DSOUND_OK);
}

static void buffer_set_frequency(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    uint32_t hz = kernel_arg(2u);
    if (tracing()) fprintf(stderr, "recomp audio: frequency slot=%u hz=%u\n",
        (unsigned)(buffer - buffers), hz);
    if (hz == 0u) hz = buffer->samples_per_second;
    buffer->base_hz = hz;
    kernel_return(2u, apply_frequency(buffer));
}

static void buffer_set_volume(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    int32_t volume = (int32_t)kernel_arg(2u);
    if (volume < -10000 || volume > 0) {
        kernel_return(2u, RECOMP_DSOUND_INVALID_PARAM);
        return;
    }
    pump_buffer(buffer, now_ms());
    buffer->volume = volume - buffer->headroom;
    if (tracing()) fprintf(stderr, "recomp audio: volume slot=%u db100=%d\n",
        (unsigned)(buffer - buffers), buffer->volume);
    kernel_return(2u, RECOMP_DSOUND_OK);
}

static void do_work(void)
{
    uint64_t now = now_ms();
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        if (buffers[i].used) pump_buffer(&buffers[i], now);
    }
    kernel_return(0u, RECOMP_DSOUND_OK);
}

static void get_speaker_config(void)
{
    set_u32(kernel_arg(2u), 0u); /* stereo, no encoder */
    kernel_return(2u, RECOMP_DSOUND_OK);
}

static void download_effects_image(void)
{
    set_u32(kernel_arg(5u), 0u);
    kernel_return(5u, RECOMP_DSOUND_OK);
}

/* Mixer, 3D and listener state only shapes audible output. */
static void ok_0(void) { kernel_return(0u, RECOMP_DSOUND_OK); }
static void ok_1(void) { kernel_return(1u, RECOMP_DSOUND_OK); }
static void ok_2(void) { kernel_return(2u, RECOMP_DSOUND_OK); }
static void ok_3(void) { kernel_return(3u, RECOMP_DSOUND_OK); }
static void ok_5(void) { kernel_return(5u, RECOMP_DSOUND_OK); }

static float arg_float(uint32_t index)
{
    uint32_t bits = kernel_arg(index);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* Reads (target, x, y, z, apply). Deferred settings apply at once: DOA3
   commits every frame, after the same calls. RECOMP_AUDIO_TRACE logs them. */
static void read_vector(const char *name, float *xyz)
{
    if (tracing()) fprintf(stderr, "recomp audio: %s target=0x%08x xyz=%g,%g,%g apply=%u\n", name,
        kernel_arg(1u), arg_float(2u), arg_float(3u), arg_float(4u), kernel_arg(5u));
    for (int i = 0; i < 3; ++i) xyz[i] = arg_float(2u + (uint32_t)i);
}

static void update_all_3d(void)
{
    for (uint32_t i = 0u; i < BUFFER_COUNT; ++i) {
        if (buffers[i].used) update_3d(&buffers[i]);
    }
}

static void listener_position(void)
{
    read_vector("listener-position", listener_position_xyz);
    update_all_3d();
    ok_5();
}

static void listener_velocity(void)
{
    read_vector("listener-velocity", listener_velocity_xyz);
    update_all_3d();
    ok_5();
}

static void voice_position(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    read_vector("voice-position", buffer->position);
    update_3d(buffer);
    ok_5();
}

static void voice_velocity(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    read_vector("voice-velocity", buffer->velocity);
    update_3d(buffer);
    ok_5();
}

static void min_distance(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    if (tracing()) fprintf(stderr, "recomp audio: min-distance target=0x%08x distance=%g apply=%u\n",
        kernel_arg(1u), arg_float(2u), kernel_arg(3u));
    buffer->min_distance = arg_float(2u);
    update_3d(buffer);
    ok_3();
}

#ifdef RECOMP_DOA3_JP_BINDINGS
/* 4134 CDirectSoundVoice_SetHeadroom: trade the old headroom for the new one. */
static void buffer_set_headroom(void)
{
    Buffer *buffer = buffer_at(kernel_arg(1u));
    int32_t headroom = (int32_t)kernel_arg(2u);
    pump_buffer(buffer, now_ms());
    buffer->volume += buffer->headroom - headroom;
    buffer->headroom = headroom;
    kernel_return(2u, RECOMP_DSOUND_OK);
}
#endif

static void mix_bin_volumes(void)
{
#ifdef RECOMP_DOA3_JP_BINDINGS
    /* DSOUND 4134: (b, LPCDSMIXBINS), ret 8. */
    if (tracing()) fprintf(stderr, "recomp audio: mix-bin-volumes target=0x%08x bins=0x%08x\n",
        kernel_arg(1u), kernel_arg(2u));
    ok_2();
#else
    if (tracing()) {
        uint32_t mask = kernel_arg(2u), volumes = kernel_arg(3u);
        fprintf(stderr, "recomp audio: mix-bin-volumes target=0x%08x mask=0x%x", kernel_arg(1u), mask);
        for (uint32_t bit = 0u, i = 0u; bit < 32u && volumes != 0u; ++bit) {
            if (mask & (1u << bit)) fprintf(stderr, " bin%u=%d", bit, (int32_t)*recomp_memory_u32(volumes + 4u * i++));
        }
        fputc('\n', stderr);
    }
    ok_3();
#endif
}

static void mix_bins(void)
{
    if (tracing()) fprintf(stderr, "recomp audio: mix-bins target=0x%08x bins=0x%08x\n", kernel_arg(1u), kernel_arg(2u));
    ok_2();
}

/* No generated code runs under this lock. The producer only reads resolved
   sample storage and never calls the runtime's guest-memory accessors. */
#define SERIALIZED(name) static void name##_locked(void) { LOCK_AUDIO(); name(); UNLOCK_AUDIO(); }
SERIALIZED(buffer_get_current_position)
SERIALIZED(buffer_get_status)
SERIALIZED(buffer_lock)
SERIALIZED(buffer_play)
SERIALIZED(buffer_release)
SERIALIZED(buffer_set_current_position)
SERIALIZED(buffer_set_data)
SERIALIZED(buffer_set_frequency)
SERIALIZED(buffer_set_loop_region)
SERIALIZED(buffer_set_volume)
SERIALIZED(buffer_stop)
SERIALIZED(buffer_stop_ex)
SERIALIZED(create_sound_buffer)
SERIALIZED(direct_sound_create)
SERIALIZED(do_work)
SERIALIZED(download_effects_image)
SERIALIZED(get_speaker_config)
SERIALIZED(ok_0)
SERIALIZED(ok_1)
SERIALIZED(ok_2)
SERIALIZED(ok_3)
SERIALIZED(ok_5)
SERIALIZED(listener_position)
SERIALIZED(listener_velocity)
SERIALIZED(voice_velocity)
SERIALIZED(min_distance)
SERIALIZED(voice_position)
SERIALIZED(mix_bin_volumes)
SERIALIZED(mix_bins)
#ifdef RECOMP_DOA3_JP_BINDINGS
SERIALIZED(buffer_set_headroom)
#endif

RecompFunction recomp_dsound_api_lookup_manual(uint32_t guest_address)
{
    switch (guest_address) {
    case DIRECT_SOUND_CREATE: return direct_sound_create_locked;
    case DIRECT_SOUND_DO_WORK: return do_work_locked;
    case DS_USE_FULL_HRTF: return ok_0_locked;
    case DS_SET_COOPERATIVE_LEVEL: return ok_3_locked;
    case BUF_UNLOCK: return ok_5_locked;
    case DS_GET_SPEAKER_CONFIG: return get_speaker_config_locked;
    case DS_DOWNLOAD_EFFECTS_IMAGE: return download_effects_image_locked;
    case DS_SET_POSITION: return listener_position_locked;
    case DS_SET_VELOCITY: return listener_velocity_locked;
    case DS_COMMIT_DEFERRED_SETTINGS: return ok_1_locked;
    case DS_SET_I3DL2_LISTENER: return ok_3_locked;
    case DS_CREATE_SOUND_BUFFER: return create_sound_buffer_locked;
    case BUF_RELEASE: return buffer_release_locked;
    case BUF_SET_VOLUME: return buffer_set_volume_locked;
    case BUF_SET_MIX_BIN_VOLUMES: return mix_bin_volumes_locked;
    case BUF_SET_DISTANCE: return min_distance_locked;
    case BUF_SET_POSITION: return voice_position_locked;
    case BUF_SET_VELOCITY: return voice_velocity_locked;
    case BUF_STOP_EX: return buffer_stop_ex_locked;
    case BUF_SET_LOOP_REGION: return buffer_set_loop_region_locked;
    case BUF_GET_STATUS: return buffer_get_status_locked;
    case BUF_GET_CURRENT_POSITION: return buffer_get_current_position_locked;
    case BUF_SET_CURRENT_POSITION: return buffer_set_current_position_locked;
    case BUF_LOCK: return buffer_lock_locked;
    case BUF_SET_FREQUENCY: return buffer_set_frequency_locked;
    case BUF_SET_MIX_BINS: return mix_bins_locked;
    case BUF_PLAY: return buffer_play_locked;
    case BUF_STOP: return buffer_stop_locked;
    case BUF_SET_BUFFER_DATA: return buffer_set_data_locked;
#ifdef RECOMP_DOA3_JP_BINDINGS
    case BUF_SET_HEADROOM: return buffer_set_headroom_locked;
#endif
    default: return NULL;
    }
}
