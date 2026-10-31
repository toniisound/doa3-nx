#include "save_adapter.h"

#ifdef RECOMP_FULL_PROGRAM
#include "doa3_build.h"
#include "fiber_adapter.h"
#include "kernel_abi.h"
#include "save_transaction.h"
#include "stop_report.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

RecompFunction recomp_lookup(uint32_t guest_address);

static void save_original(uint32_t address, uint32_t success_value)
{
    const uint32_t owner = recomp_fiber_adapter_model()->current_handle;
    const RecompFunction original = recomp_lookup(address);

    if (original == NULL) {
        recomp_stop(1, "save:missing-original:0x%08" PRIx32, address);
        return;
    }
    if (!recomp_save_begin(owner)) {
        recomp_stop(1, "save:begin:0x%08" PRIx32, address);
        return;
    }

    /* Raw dispatch preserves the original ABI and cannot reenter this hook.
       Nested region saves join this fiber's transaction even when the
       enclosing guest routine ignores their return value. */
    original();
    if (!recomp_kernel_save_handles_closed(owner)) {
        recomp_save_note_failure(owner);
    }
    const bool success = recomp_runtime.registers.eax == success_value;
    if (recomp_save_end_recovers(owner, success)) {
        recomp_kernel_release_profile_handles();
    }
    if (!recomp_save_end(owner, success)) {
        recomp_stop(1, "save:end:0x%08" PRIx32, address);
    }
}

static void save_profile(void)
{
    save_original(0x000e0410u, 1u);
}

static void save_profile_region(void)
{
    save_original(0x0001adb0u, 0u);
}

static uint32_t call_guest(uint32_t address, const uint32_t *args, unsigned count)
{
    kernel_call_guest(address, args, count);
    return recomp_runtime.registers.eax;
}

/* DOA3 auto-save (0x00021930), hand-written so the journal brackets it. It
   rewrites the T: save file named at 0x0021B50C with one write: 0x3C0C bytes from
   0x00484D78, their XOR, and padding to 0x3C10. Then 62 "NOW SAVING" frames.
   Its debug prints (0x0017EC00) are empty and omitted. */
static void doa3_auto_save(void)
{
    if (*recomp_memory(0x00484dcau, 1u) == 1u || *recomp_memory(0x0048e61cu, 1u) == 1u) {
        recomp_runtime.registers.esp += 4u;
        return;
    }
    const uint32_t owner = recomp_fiber_adapter_model()->current_handle;
    const uint32_t entry_sp = recomp_runtime.registers.esp;
    const uint32_t written = entry_sp - 4u; /* the original's pushed ECX */
    recomp_runtime.registers.esp = written;
    if (!recomp_save_begin(owner)) {
        recomp_stop(1, "save:begin:0x00021930");
        return;
    }
    *recomp_memory(0x004b8438u, 1u) = 1u;
    const uint32_t open[] = {*recomp_memory_u32(0x0021b50cu), 0x40000000u, 0u, 0u, 2u, 0x20u, 0u};
    const uint32_t file = call_guest(0x001631c0u, open, 7u); /* CreateFileA */
    if (file != UINT32_MAX) {
        const uint32_t allocate[] = {0u, 0x4000u, 0x1000u, 4u};
        const uint32_t buffer = call_guest(0x00162c40u, allocate, 4u); /* VirtualAlloc */
        /* The original copies to a failed allocation's address 0; roll back instead. */
        if (buffer == 0u) {
            recomp_save_note_failure(owner);
        } else {
            uint8_t *bytes = recomp_memory(buffer, 0x3c10u);
            uint8_t sum = 0u;
            memcpy(bytes, recomp_memory(0x00484d78u, 0x3c0cu), 0x3c0cu);
            for (uint32_t i = 0u; i < 0x3c0cu; ++i) sum ^= bytes[i];
            bytes[0x3c0c] = sum;
            const uint32_t write[] = {file, buffer, 0x3c10u, written, 0u};
            call_guest(0x00162db5u, write, 5u); /* WriteFile; the count is not checked */
        }
        for (uint32_t frame = 0u; frame < 0x3eu; ++frame) {
            if (*recomp_memory(0x00480b70u, 1u) == 0u) {
                *recomp_memory(0x00369124u, 1u) = 0u;
                call_guest(0x000cd390u, NULL, 0u);
            }
            const uint32_t one = 1u, scale = 0x3f800000u, mode = 2u, text = 0x001ed5e4u;
            const uint32_t at[] = {0x296u, 0x1acu};
            call_guest(0x0009e562u, &one, 1u); /* wait one frame */
            call_guest(0x00055780u, &scale, 1u);
            call_guest(0x00055760u, &scale, 1u);
            call_guest(0x00055770u, &mode, 1u);
            call_guest(0x00055740u, at, 2u);
            call_guest(0x00055e90u, &text, 1u);
        }
        call_guest(0x00162caau, &file, 1u); /* CloseHandle */
        if (buffer != 0u) {
            const uint32_t release[] = {buffer, 0u, 0x8000u};
            call_guest(0x00162c6eu, release, 3u); /* VirtualFree */
        }
    }
    *recomp_memory(0x004b8438u, 1u) = 0u;
    if (!recomp_kernel_save_handles_closed(owner)) recomp_save_note_failure(owner);
    if (recomp_save_end_recovers(owner, true)) recomp_kernel_release_profile_handles();
    /* A failed open or write rolls the old save back and the game carries on, as
       it does after "Save Failed."; a commit or rollback that itself failed stops. */
    if (!recomp_save_end(owner, true)) {
        if (!recomp_save_ready()) {
            recomp_stop(1, "save:end:0x00021930");
            return;
        }
        fprintf(stderr, "recomp save: 0x00021930 rolled back\n");
    }
    recomp_runtime.registers.esp = entry_sp + 4u;
}

/* Costume tiers. Each of the 19 characters has a byte in the save block
   (USA 0x00487806, Japan 0x0059FB16): how many costume tiers the player has
   earned (new saves start at 2). Each of the 25 select slots names its
   character's byte (USA 0x00309CB8, Japan 0x002FF868) and its tier count
   (USA 0x0036806C, Japan 0x0029C70C: at most 3 on USA, 5 on Japan 3.1); the
   select screen shows min(earned, count). Raising each byte to its slots'
   largest count is what awarding every tier writes. The other byte is the
   extra-characters flag. The runner calls this each frame, which also covers
   saves loaded afterwards. */
void recomp_save_unlock_all(void)
{
    uint8_t *earned = recomp_memory(DOA3(0x00487806u, 0x0059fb16u), 19u);
    const uint8_t *character = recomp_memory(DOA3(0x00309cb8u, 0x002ff868u), 25u);
    const uint8_t *count = recomp_memory(DOA3(0x0036806cu, 0x0029c70cu), 25u);
    for (unsigned slot = 0u; slot < 25u; ++slot) {
        if (character[slot] < 19u && earned[character[slot]] < count[slot]) {
            earned[character[slot]] = count[slot];
        }
    }
    *recomp_memory(DOA3(0x00488837u, 0x005a0b47u), 1u) = 1u;
}

/* Analog Button. DOA3 reads the stick only with a controller's Analog Button
   byte on, and saves made before light presses (input_host_win32.c) hold it
   off. The boot auto-load keeps its busy byte at 1 until it has copied the
   save over the settings, so the first time that byte clears, turn it on for
   all four controllers. shortcut: overrides an OFF saved on purpose at every
   boot; keep a one-time marker in .recomp-storage if anyone wants it off. */
void recomp_save_analog_button_on(void)
{
    static bool loading, done;
    if (done) return;
    if (*recomp_memory(DOA3(0x004b8438u, 0x00c5939cu), 1u) == 1u) {
        loading = true;
    } else if (loading) {
        memset(recomp_memory(DOA3(0x00488831u, 0x005a0b41u), 4u), 1, 4u);
        done = true;
    }
}
#endif

RecompFunction recomp_save_lookup_manual(uint32_t guest_address)
{
#ifdef RECOMP_FULL_PROGRAM
    switch (guest_address) {
    case 0x000e0410u:
        return save_profile;
    case 0x0001adb0u:
        return save_profile_region;
    case 0x00021930u:
        return doa3_auto_save;
    default:
        return NULL;
    }
#else
    (void)guest_address;
    return NULL;
#endif
}
