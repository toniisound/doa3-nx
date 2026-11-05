#include "audio_capture_xapo.cpp"

static void check(bool condition)
{
    if (!condition) std::abort();
}

int main()
{
    auto *effect = new Capture;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 32;
    format.nBlockAlign = 8;
    format.nAvgBytesPerSec = 384000;
    XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS lock{&format, 4};
    IXAPO *xapo = nullptr;
    check(SUCCEEDED(effect->QueryInterface(__uuidof(IXAPO), reinterpret_cast<void **>(&xapo))) &&
        xapo == effect && effect->Release() == 1);
    XAPO_REGISTRATION_PROPERTIES *registration = nullptr;
    check(SUCCEEDED(effect->GetRegistrationProperties(&registration)) &&
        registration->Flags == properties.Flags && registration->MaxInputBufferCount == 1);
    CoTaskMemFree(registration);
    WAVEFORMATEX pcm16 = format;
    pcm16.wFormatTag = WAVE_FORMAT_PCM;
    pcm16.wBitsPerSample = 16;
    WAVEFORMATEX *suggested = nullptr;
    check(effect->IsInputFormatSupported(&format, &pcm16, &suggested) == XAPO_E_FORMAT_UNSUPPORTED &&
        suggested && suggested->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && suggested->nChannels == 2);
    CoTaskMemFree(suggested);
    check(effect->IsOutputFormatSupported(&format, &format, &suggested) == S_OK && !suggested);
    XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS lock16{&pcm16, 4};
    check(effect->LockForProcess(1, &lock16, 1, &lock16) == XAPO_E_FORMAT_UNSUPPORTED);
    check(SUCCEEDED(effect->LockForProcess(1, &lock, 1, &lock)));
    check(FAILED(effect->LockForProcess(1, &lock, 1, &lock)));
    float pcm[] = {0.25f, -0.5f, 0.75f, -1.0f};
    XAPO_PROCESS_BUFFER_PARAMETERS input{pcm, XAPO_BUFFER_VALID, 2};
    XAPO_PROCESS_BUFFER_PARAMETERS output{pcm, XAPO_BUFFER_SILENT, 2};
    effect->Process(1, &input, 1, &output, TRUE);
    check(effect->used == 4 && output.ValidFrameCount == 2 &&
        output.BufferFlags == XAPO_BUFFER_VALID &&
        std::memcmp(effect->samples.data(), pcm, sizeof pcm) == 0 && pcm[1] == -0.5f);
    input.BufferFlags = XAPO_BUFFER_SILENT;
    effect->Process(1, &input, 1, &output, TRUE);
    check(effect->used == 8 && effect->samples[4] == 0 && effect->samples[7] == 0);
    effect->used = effect->samples.size() - 2;
    effect->Process(1, &input, 1, &output, TRUE);
    check(effect->used == effect->samples.size());
    effect->Process(1, &input, 1, &output, TRUE);
    check(effect->used == effect->samples.size());
    effect->UnlockForProcess();
    effect->Release();
    std::puts("PASS audio capture interface, formats, passthrough, silence, capacity");
}
