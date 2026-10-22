// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch audio backend: libnx audout at 48 kHz stereo, fed by a
// mixer thread on core 2. Mirrors audio_output_sdl.cpp: one queue per guest
// voice, a 50 ms silence cushion when a voice starts or starves, and a pitch
// trim of at most 1% that holds the cushion against the device's real rate.
//
// Having a device open also matters for speed: dsound_api_adapter.c then
// runs its output pump (ADPCM decode, HRTF for 3D voices) on its own thread,
// off the guest core, instead of inside the game's DirectSound calls.
#include "audio_output.h"

#include <switch.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

constexpr uint32_t kVoiceCount = 256;
constexpr uint32_t kMaxBufferBytes = 160000;
constexpr uint32_t kMinSampleRate = 1000;
constexpr uint32_t kMaxSampleRate = 200000;
constexpr uint32_t kPrerollMs = 50;
constexpr float kRateTrim = 0.01f;
constexpr uint32_t kDeviceRate = 48000;
constexpr uint32_t kPeriodFrames = 1024;   // 21.3 ms per device buffer
constexpr uint32_t kDeviceBuffers = 4;
constexpr uint32_t kQueueMs = 1000;        // per-voice capacity

struct Voice {
    bool active = false;
    uint32_t sample_rate = 0, channels = 0, bits = 0;
    float gain = 1.0f;
    float ratio = 1.0f;
    float level_ms = kPrerollMs;
    std::vector<float> ring; // interleaved stereo frames
    uint32_t capacity = 0;   // frames
    uint64_t write = 0;      // frames written
    double read = 0.0;       // frames consumed (fractional)
    uint64_t submitted = 0;

    uint32_t queued() const
    {
        const double q = static_cast<double>(write) - read;
        return q > 0.0 ? static_cast<uint32_t>(q) : 0u;
    }
};

std::mutex mutex;
Voice voices[kVoiceCount];
bool attempted = false;
bool device_open = false;
float master_gain = 1.0f;
volatile bool mixer_running = false;
Thread mixer_thread;
AudioOutBuffer out_buffers[kDeviceBuffers];
int16_t *out_memory = nullptr;
std::vector<float> mix_scratch(kPeriodFrames * 2);
unsigned long long dropped = 0, underruns = 0, submitted_buffers = 0;

void drop(uint32_t slot, const char *reason)
{
    ++dropped;
    if (dropped <= 4 || (dropped & (dropped - 1)) == 0) {
        std::fprintf(stderr, "[audio-output] dropped=%llu slot=%u reason=%s\n", dropped, slot, reason);
    }
}

inline float sampleAt(const uint8_t *pcm, uint32_t index, uint32_t bits)
{
    switch (bits) {
    case 8: return (static_cast<int>(pcm[index]) - 128) / 128.0f;
    case 16: {
        int16_t s;
        std::memcpy(&s, pcm + index * 2u, 2);
        return s / 32768.0f;
    }
    default: {
        float f;
        std::memcpy(&f, pcm + index * 4u, 4);
        return f;
    }
    }
}

// Appends frames as stereo float; returns false when the queue is full.
bool push(Voice &voice, const uint8_t *pcm, uint32_t frames, bool silence)
{
    if (voice.queued() + frames > voice.capacity) return false;
    for (uint32_t i = 0; i < frames; ++i) {
        float left = 0.0f, right = 0.0f;
        if (!silence) {
            if (voice.channels == 1) {
                left = right = sampleAt(pcm, i, voice.bits);
            } else {
                left = sampleAt(pcm, i * 2u, voice.bits);
                right = sampleAt(pcm, i * 2u + 1u, voice.bits);
            }
        }
        const uint64_t at = (voice.write + i) % voice.capacity;
        voice.ring[at * 2u] = left;
        voice.ring[at * 2u + 1u] = right;
    }
    voice.write += frames;
    return true;
}

// Mixes one device period under the lock; voices resample linearly.
void mixPeriod(int16_t *out)
{
    float *mix = mix_scratch.data();
    std::memset(mix, 0, sizeof(float) * kPeriodFrames * 2u);
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (Voice &voice : voices) {
            if (!voice.active || voice.capacity == 0) continue;
            const double step = static_cast<double>(voice.sample_rate) * voice.ratio / kDeviceRate;
            const float gain = voice.gain;
            for (uint32_t f = 0; f < kPeriodFrames; ++f) {
                if (voice.read + 1.0 >= static_cast<double>(voice.write)) break; // starved
                const uint64_t base = static_cast<uint64_t>(voice.read);
                const float frac = static_cast<float>(voice.read - static_cast<double>(base));
                const uint64_t a = base % voice.capacity, b = (base + 1u) % voice.capacity;
                const float l = voice.ring[a * 2u] + (voice.ring[b * 2u] - voice.ring[a * 2u]) * frac;
                const float r = voice.ring[a * 2u + 1u] + (voice.ring[b * 2u + 1u] - voice.ring[a * 2u + 1u]) * frac;
                mix[f * 2u] += l * gain;
                mix[f * 2u + 1u] += r * gain;
                voice.read += step;
            }
        }
    }
    for (uint32_t i = 0; i < kPeriodFrames * 2u; ++i) {
        float s = mix[i] * master_gain;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = static_cast<int16_t>(s * 32767.0f);
    }
}

void mixerLoop(void *)
{
    // Prime every device buffer, then refill each one as it is released.
    for (uint32_t i = 0; i < kDeviceBuffers; ++i) {
        mixPeriod(static_cast<int16_t *>(out_buffers[i].buffer));
        audoutAppendAudioOutBuffer(&out_buffers[i]);
    }
    while (mixer_running) {
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 100'000'000ull)) || released == nullptr) {
            continue;
        }
        mixPeriod(static_cast<int16_t *>(released->buffer));
        audoutAppendAudioOutBuffer(released);
    }
}

} // namespace

extern "C" int recomp_audio_output_enabled(void)
{
    return device_open ? 1 : 0;
}

extern "C" void recomp_audio_output_initialize(void)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (attempted) return;
    attempted = true;

    if (const char *setting = std::getenv("RECOMP_AUDIO_GAIN")) {
        const double gain = std::strtod(setting, nullptr);
        if (std::isfinite(gain) && gain >= 0.0 && gain <= 1.0) master_gain = static_cast<float>(gain);
    }
    if (master_gain == 0.0f) {
        std::fprintf(stderr, "[audio-output] muted RECOMP_AUDIO_GAIN=0\n");
        return;
    }
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        std::fprintf(stderr, "[audio-output] audoutInitialize failed 0x%x\n", rc);
        return;
    }
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        std::fprintf(stderr, "[audio-output] audoutStartAudioOut failed 0x%x\n", rc);
        audoutExit();
        return;
    }
    const size_t bytes = kPeriodFrames * 2u * sizeof(int16_t); // 4 KiB, page sized
    out_memory = static_cast<int16_t *>(aligned_alloc(0x1000, bytes * kDeviceBuffers));
    if (out_memory == nullptr) {
        audoutStopAudioOut();
        audoutExit();
        return;
    }
    std::memset(out_memory, 0, bytes * kDeviceBuffers);
    for (uint32_t i = 0; i < kDeviceBuffers; ++i) {
        out_buffers[i].next = nullptr;
        out_buffers[i].buffer = reinterpret_cast<uint8_t *>(out_memory) + i * bytes;
        out_buffers[i].buffer_size = bytes;
        out_buffers[i].data_size = bytes;
        out_buffers[i].data_offset = 0;
    }
    mixer_running = true;
    if (R_FAILED(threadCreate(&mixer_thread, mixerLoop, nullptr, nullptr, 0x10000, 0x2B, 2)) ||
        R_FAILED(threadStart(&mixer_thread))) {
        mixer_running = false;
        audoutStopAudioOut();
        audoutExit();
        std::fprintf(stderr, "[audio-output] mixer thread failed\n");
        return;
    }
    device_open = true;
    std::fprintf(stderr,
        "[audio-output] initialized backend=audout rate=%u channels=%u master_gain=%.3f\n",
        audoutGetSampleRate(), audoutGetChannelCount(), master_gain);
}

extern "C" void recomp_audio_output_shutdown(void)
{
    if (!device_open) return;
    mixer_running = false;
    threadWaitForExit(&mixer_thread);
    threadClose(&mixer_thread);
    audoutStopAudioOut();
    audoutExit();
    device_open = false;
    std::fprintf(stderr, "[audio-output] summary submitted_buffers=%llu dropped=%llu underruns=%llu\n",
        submitted_buffers, dropped, underruns);
}

extern "C" void recomp_audio_output_reset_voice(uint32_t slot)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (slot < kVoiceCount) {
        voices[slot].active = false;
        voices[slot].write = 0;
        voices[slot].read = 0.0;
    }
}

extern "C" void recomp_audio_output_submit(
    uint32_t slot, const uint8_t *pcm, uint32_t bytes,
    uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample,
    int32_t volume_hundredth_db)
{
    recomp_audio_output_initialize();
    if (!device_open || bytes == 0) return;
    if (slot >= kVoiceCount || !pcm || bytes > kMaxBufferBytes ||
        sample_rate < kMinSampleRate || sample_rate > kMaxSampleRate ||
        (channels != 1 && channels != 2) ||
        (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 32)) {
        drop(slot, "invalid-pcm");
        return;
    }
    const uint32_t block_align = channels * (bits_per_sample / 8u);
    if (bytes % block_align != 0) {
        drop(slot, "partial-frame");
        return;
    }
    const uint32_t frames = bytes / block_align;

    std::lock_guard<std::mutex> lock(mutex);
    Voice &voice = voices[slot];
    if (voice.active && (voice.sample_rate != sample_rate || voice.channels != channels ||
                         voice.bits != bits_per_sample)) {
        voice.active = false;
    }
    if (!voice.active) {
        voice.sample_rate = sample_rate;
        voice.channels = channels;
        voice.bits = bits_per_sample;
        // Room for the cushion plus the pump's bursts: kQueueMs, or twice
        // this submission if a voice delivers larger blocks.
        uint32_t capacity = sample_rate * kQueueMs / 1000u;
        if (capacity < frames * 2u + sample_rate * kPrerollMs / 1000u) {
            capacity = frames * 2u + sample_rate * kPrerollMs / 1000u;
        }
        if (voice.capacity < capacity) {
            voice.ring.assign(static_cast<size_t>(capacity) * 2u, 0.0f);
            voice.capacity = capacity;
        }
        voice.write = 0;
        voice.read = 0.0;
        voice.ratio = 1.0f;
        voice.level_ms = kPrerollMs;
        voice.submitted = 0;
        voice.active = true;
    }

    if (volume_hundredth_db < -10000) volume_hundredth_db = -10000;
    if (volume_hundredth_db > 0) volume_hundredth_db = 0;
    voice.gain = volume_hundredth_db == -10000 ? 0.0f : std::pow(10.0f, volume_hundredth_db / 2000.0f);

    const uint32_t queued = voice.queued();
    const bool starved = queued <= 1u;
    if (starved && voice.submitted > 0) ++underruns;
    float queued_ms = queued * 1000.0f / sample_rate;
    if (starved) {
        // Restart from a clean cushion: drop the fractional tail, queue silence.
        voice.read = static_cast<double>(voice.write);
        push(voice, nullptr, sample_rate * kPrerollMs / 1000u, true);
        queued_ms = static_cast<float>(kPrerollMs);
        voice.level_ms = queued_ms;
    }
    voice.level_ms += (queued_ms - voice.level_ms) / 16.0f;
    float ratio = 1.0f + (voice.level_ms - kPrerollMs) / 10000.0f;
    if (ratio < 1.0f - kRateTrim) ratio = 1.0f - kRateTrim;
    if (ratio > 1.0f + kRateTrim) ratio = 1.0f + kRateTrim;
    voice.ratio = ratio;

    if (!push(voice, pcm, frames, false)) {
        drop(slot, "queue-full");
        return;
    }
    voice.submitted += frames;
    ++submitted_buffers;
}
