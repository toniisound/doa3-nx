#include "audio_capture_xapo.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <xapo.h>

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#ifndef XAPO_E_FORMAT_UNSUPPORTED
#define XAPO_E_FORMAT_UNSUPPORTED MAKE_HRESULT(SEVERITY_ERROR, 0x897, 0x01)
#endif

namespace {

// MinGW has no xapobase.h, so this spells out CXAPOBase's default flags.
const XAPO_REGISTRATION_PROPERTIES properties = {
    {0xa6da3ade, 0x8211, 0x4ae4, {0x8a, 0x09, 0x11, 0x25, 0x62, 0x31, 0xd1, 0x72}},
    L"Recomp audio capture", L"", 1, 0,
    XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH |
        XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH |
        XAPO_FLAG_INPLACE_SUPPORTED | XAPO_FLAG_INPLACE_REQUIRED,
    1, 1, 1, 1
};

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, without ksmedia.h and its GUID library.
const GUID float_subtype = {WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool is_float32(const WAVEFORMATEX *format)
{
    if (!format || format->wBitsPerSample != 32 || !format->nChannels ||
        !format->nSamplesPerSec) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    return format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
        IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->SubFormat,
            float_subtype);
}

// In-place passthrough: both sides are the same float32 format.
bool formats_match(const WAVEFORMATEX *a, const WAVEFORMATEX *b)
{
    return is_float32(a) && is_float32(b) && a->nChannels == b->nChannels &&
        a->nSamplesPerSec == b->nSamplesPerSec;
}

class Capture final : public IXAPO {
public:
    std::vector<float> samples;
    size_t used = 0;
    ULONGLONG first_tick = 0;
    uint32_t rate = 0, channels = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override
    {
        if (!object) return E_POINTER;
        if (IsEqualIID(id, __uuidof(IXAPO)) || IsEqualIID(id, __uuidof(IUnknown))) {
            *object = static_cast<IXAPO *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs)); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = ULONG(InterlockedDecrement(&refs));
        if (!left) delete this;
        return left;
    }

    // XAudio2 frees the copy with XAPOFree, which is CoTaskMemFree.
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(
        XAPO_REGISTRATION_PROPERTIES **result) override
    {
        if (!result) return E_POINTER;
        auto *copy = static_cast<XAPO_REGISTRATION_PROPERTIES *>(
            CoTaskMemAlloc(sizeof properties));
        if (!copy) return E_OUTOFMEMORY;
        *copy = properties;
        *result = copy;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(const WAVEFORMATEX *output,
        const WAVEFORMATEX *requested, WAVEFORMATEX **supported) override
    {
        return format_supported(output, requested, supported);
    }

    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(const WAVEFORMATEX *input,
        const WAVEFORMATEX *requested, WAVEFORMATEX **supported) override
    {
        return format_supported(input, requested, supported);
    }

    HRESULT STDMETHODCALLTYPE Initialize(const void *, UINT32) override { return S_OK; }
    void STDMETHODCALLTYPE Reset() override {}

    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputs,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *input, UINT32 outputs,
        const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *output) override
    {
        if (locked) return E_FAIL;
        if (inputs != 1 || outputs != 1 || !input || !output) return E_INVALIDARG;
        if (!formats_match(input[0].pFormat, output[0].pFormat))
            return XAPO_E_FORMAT_UNSUPPORTED;
        rate = input[0].pFormat->nSamplesPerSec;
        channels = input[0].pFormat->nChannels;
        // ponytail: retain the first 60 seconds; use a writer thread for longer runs.
        try { samples.resize(size_t(rate) * channels * 60); }
        catch (const std::bad_alloc &) { return E_OUTOFMEMORY; }
        locked = true;
        return S_OK;
    }

    void STDMETHODCALLTYPE UnlockForProcess() override { locked = false; }

    void STDMETHODCALLTYPE Process(UINT32,
        const XAPO_PROCESS_BUFFER_PARAMETERS *input, UINT32,
        XAPO_PROCESS_BUFFER_PARAMETERS *output, BOOL enabled) override
    {
        output[0].ValidFrameCount = input[0].ValidFrameCount;
        output[0].BufferFlags = input[0].BufferFlags;
        if (!enabled) return;
        size_t count = size_t(input[0].ValidFrameCount) * channels;
        if (count && !first_tick) first_tick = GetTickCount64();
        if (count > samples.size() - used) count = samples.size() - used;
        if (input[0].BufferFlags == XAPO_BUFFER_SILENT)
            std::memset(samples.data() + used, 0, count * sizeof(float));
        else
            std::memcpy(samples.data() + used, input[0].pBuffer, count * sizeof(float));
        used += count;
    }

    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 frames) override { return frames; }
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 frames) override { return frames; }

private:
    LONG refs = 1;
    bool locked = false;

    // On a mismatch, suggest the other side's format, as CXAPOBase does.
    static HRESULT format_supported(const WAVEFORMATEX *other,
        const WAVEFORMATEX *requested, WAVEFORMATEX **supported)
    {
        if (supported) *supported = nullptr;
        if (!requested) return E_POINTER;
        if (formats_match(other, requested)) return S_OK;
        if (supported && is_float32(other)) {
            const size_t size = sizeof(WAVEFORMATEX) + other->cbSize;
            *supported = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(size));
            if (*supported) std::memcpy(*supported, other, size);
        }
        return XAPO_E_FORMAT_UNSUPPORTED;
    }
};

Capture *capture;
FILE *capture_file;

void write_u32(FILE *file, uint32_t value)
{
    std::fwrite(&value, sizeof value, 1, file);
}

} // namespace

void recomp_audio_capture_attach(IXAudio2MasteringVoice *master)
{
    const char *path = std::getenv("RECOMP_AUDIO_CAPTURE");
    if (!path || !*path || capture) return;
    // Never overwrite an earlier recording, including after device loss.
    // _O_EXCL, since msvcrt's fopen has no "x" mode.
    const int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
        _S_IREAD | _S_IWRITE);
    capture_file = fd < 0 ? nullptr : _fdopen(fd, "wb");
    if (fd >= 0 && !capture_file) _close(fd);
    if (!capture_file) {
        std::fprintf(stderr, "[audio-capture] cannot create recording\n");
        return;
    }
    capture = new (std::nothrow) Capture;
    if (!capture) { std::fclose(capture_file); capture_file = nullptr; return; }
    XAUDIO2_VOICE_DETAILS details{};
    master->GetVoiceDetails(&details);
    XAUDIO2_EFFECT_DESCRIPTOR effect{capture, TRUE, details.InputChannels};
    XAUDIO2_EFFECT_CHAIN chain{1, &effect};
    HRESULT result = master->SetEffectChain(&chain);
    if (FAILED(result)) {
        std::fprintf(stderr, "[audio-capture] attach failed=0x%08lx\n", (unsigned long)result);
        capture->Release(); capture = nullptr;
        std::fclose(capture_file); capture_file = nullptr;
        return;
    }
    std::fprintf(stderr, "[audio-capture] attached rate=%u channels=%u limit=60s\n",
        capture->rate, capture->channels);
}

void recomp_audio_capture_save(void)
{
    if (!capture) return;
    const uint32_t bytes = uint32_t(capture->used * sizeof(float));
    std::fwrite("RIFF", 4, 1, capture_file); write_u32(capture_file, bytes + 36);
    std::fwrite("WAVEfmt ", 8, 1, capture_file); write_u32(capture_file, 16);
    uint16_t format[] = {3, uint16_t(capture->channels)}; // IEEE float
    std::fwrite(format, sizeof format, 1, capture_file);
    write_u32(capture_file, capture->rate);
    write_u32(capture_file, capture->rate * capture->channels * 4);
    uint16_t align[] = {uint16_t(capture->channels * 4), 32};
    std::fwrite(align, sizeof align, 1, capture_file);
    std::fwrite("data", 4, 1, capture_file); write_u32(capture_file, bytes);
    size_t written = std::fwrite(capture->samples.data(), 1, bytes, capture_file);
    int closed = std::fclose(capture_file);
    std::fprintf(stderr, "[audio-capture] saved frames=%zu rate=%u first_tick=%llu complete=%u\n",
        capture->used / capture->channels, capture->rate, capture->first_tick,
        written == bytes && closed == 0);
    capture_file = nullptr;
    capture->Release(); capture = nullptr;
}
