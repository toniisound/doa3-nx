// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch, first bring-up stage: no audio device. The DirectSound
// adapters keep guest audio timing on their own clock when no device is
// open (see audio_output.h), so the game runs silently. A libnx audren
// backend replaces this file once the CPU cost of the game is known.
#include "audio_output.h"

#include <cstdio>

extern "C" {

void recomp_audio_output_initialize(void)
{
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "[audio] Switch build: audio output disabled in this stage\n");
    }
}

void recomp_audio_output_shutdown(void) {}

void recomp_audio_output_reset_voice(uint32_t slot)
{
    (void)slot;
}

int recomp_audio_output_enabled(void)
{
    return 0;
}

void recomp_audio_output_submit(
    uint32_t slot, const uint8_t *pcm, uint32_t bytes,
    uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample,
    int32_t volume_hundredth_db)
{
    (void)slot; (void)pcm; (void)bytes; (void)sample_rate;
    (void)channels; (void)bits_per_sample; (void)volume_hundredth_db;
}

}
