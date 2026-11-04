#include "d3d_texture_adapter.h"
#include "doa3_build.h"
#include "stop_report.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef RECOMP_DOAXBV_BINDINGS
enum {
    D3D_DEVICE_SET_TEXTURE_ADDRESS = 0x001e43f0u,
    D3D_TEXTURE_LOCK_RECT_ADDRESS = 0x001e8090u,
    D3D_DEVICE_GLOBAL = 0x001f2978u,
    D3D_STATE_DIRTY_MASK = 0x001f2984u,
    D3D_TEXTURE_SLOT_OFFSET = 0x00000b38u,
    D3D_TEXTURE_DATA_OFFSET = 0x00000004u,
    D3D_TEXTURE_FORMAT_OFFSET = 0x0000000cu,
    D3D_TEXTURE_SIZE_OFFSET = 0x00000010u,
    /* Per-format descriptor table the guest's own D3D8 indexes by format
       byte: bits 2-5 bits-per-pixel, bit 7 render target, bit 6 depth. */
    D3D_FORMAT_DESCRIPTOR_TABLE = 0x001f16b8u,
    D3D_TEXTURE_REFERENCE_STEP = 0x00080000u,
    D3D_TEXTURE_DISABLE_STATE = 0x80000000u,
    D3D_TEXTURE_DIRTY = 0x00004800u,
    D3D_TEXTURE_FORMAT_DIRTY = 0x4000u,
};
#else
/* Japan values are D3D8 4134 (0x001DD670, 0x001DFF50, 0x001DD880). */
enum {
    D3D_DEVICE_SET_TEXTURE_ADDRESS = 0x001b1cc0u,
    D3D_DEVICE_GLOBAL = DOA3(0x001c3390u, 0x001ede80u),
    D3D_STATE_DIRTY_MASK = DOA3(0x001c0808u, 0x001ead68u),
    D3D_TEXTURE_SLOT_OFFSET = DOA3(0x00000ba0u, 0x00000a78u),
    D3D_TEXTURE_DATA_OFFSET = 0x00000004u,
    D3D_TEXTURE_FORMAT_OFFSET = 0x0000000cu,
    D3D_TEXTURE_SIZE_OFFSET = 0x00000010u,
    /* Per-format descriptor table the guest's own D3D8 indexes by format
       byte: bits 2-5 bits-per-pixel, bit 7 render target, bit 6 depth. */
    D3D_FORMAT_DESCRIPTOR_TABLE = DOA3(0x001bef20u, 0x001e9ba8u),
    D3D_TEXTURE_REFERENCE_STEP = 0x00080000u,
    /* SetTexture(NULL) at 0x001B1CC0 stores -1 so the next bind reloads its format. */
    D3D_TEXTURE_DISABLE_STATE = 0xffffffffu,
    D3D_TEXTURE_DIRTY = DOA3(0x00000408u, 0x00004800u),
    D3D_TEXTURE_FORMAT_DIRTY = DOA3(0x400u, 0x4000u),
    /* Fence word a released resource records; 4134 dirty bits leave device + 8. */
    D3D_DEVICE_FENCE = DOA3(0x1cu, 0x30u),
    D3D_STREAM_TABLE = DOA3(0x001c05c8u, 0x001eb1b8u),
    D3D_INDEX_DATA = DOA3(0x001c017cu, 0x001ead64u),
    D3D_DEVICE_BASE_VERTEX = DOA3(0x478u, 0x1cu),
    D3D_DEVICE_INDEX_BUFFER = DOA3(0x47cu, 0x38cu),
};
#endif

static RecompD3dTextureModel texture_model;
static RecompD3dTextureDesc stage_descs[RECOMP_D3D_TEXTURE_STAGE_COUNT];
static bool stage_desc_valid[RECOMP_D3D_TEXTURE_STAGE_COUNT];

void sub_001E8090(void);

void recomp_d3d_texture_adapter_reset(void)
{
    recomp_d3d_texture_reset(&texture_model);
    memset(stage_descs, 0, sizeof stage_descs);
    memset(stage_desc_valid, 0, sizeof stage_desc_valid);
}

const RecompD3dTextureModel *recomp_d3d_texture_adapter_model(void)
{
    return &texture_model;
}

const RecompD3dTextureDesc *recomp_d3d_texture_adapter_stage(uint32_t stage)
{
    if (stage >= RECOMP_D3D_TEXTURE_STAGE_COUNT || !stage_desc_valid[stage]) {
        return NULL;
    }
    return &stage_descs[stage];
}

void recomp_d3d_texture_adapter_report(void)
{
    const RecompD3dTextureCensus *census = &texture_model.census;
    uint32_t i;

    for (i = 0u; i < RECOMP_D3D_TEXTURE_CENSUS_SLOTS; ++i) {
        const RecompD3dTextureCensusEntry *entry = &census->entries[i];

        if (!entry->used) {
            continue;
        }
        fprintf(
            stderr,
            "recomp d3d texture: stage=%" PRIu32 " fmt=0x%02" PRIx32
            " %s %" PRIu32 "x%" PRIu32
            " bpp=%" PRIu32 " pitch=%" PRIu32 " rt=%d depth=%d binds=%"
            PRIu32 "\n",
            entry->stage,
            entry->desc.format_byte,
            entry->desc.linear ? "linear" : "swizzled",
            entry->desc.width,
            entry->desc.height,
            entry->desc.bits_per_pixel,
            entry->desc.pitch,
            entry->desc.render_target ? 1 : 0,
            entry->desc.depth ? 1 : 0,
            entry->bind_count);
    }
    if (census->overflow_count != 0u) {
        fprintf(
            stderr,
            "recomp d3d texture: census overflow=%" PRIu32 "\n",
            census->overflow_count);
    }
}

static uint32_t stack_argument(uint32_t entry_esp, uint32_t index)
{
    return *recomp_memory_u32(entry_esp + 4u + index * 4u);
}

static uint32_t texture_format_shadow(uint32_t texture)
{
    uint32_t source = *recomp_memory_u32(
        texture + D3D_TEXTURE_FORMAT_OFFSET);
#ifndef RECOMP_DOAXBV_BINDINGS
    return source & 0xf4u;
#else
    uint32_t shadow = source & 0x000020f4u;
    uint32_t format;

    if ((shadow & 0x00002000u) == 0u) {
        return shadow;
    }
    format = source & 0x0000ff00u;
    shadow &= ~0x00002000u;
    if (format >= 0x00002a00u && format <= 0x00003100u) {
        shadow |= 0x40000000u;
    }
    return shadow;
#endif
}

static void release_texture_resource(uint32_t texture)
{
    uint32_t saved_esp = recomp_runtime.registers.esp;

    recomp_runtime.registers.esp -= 4u;
    *recomp_memory_u32(recomp_runtime.registers.esp) = texture;
    recomp_runtime.registers.esp -= 4u;
    *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
    recomp_dispatch_indirect_site(
#ifdef RECOMP_DOAXBV_BINDINGS
        0x001e7c90u, saved_esp, __FILE__, __LINE__);
#else
        /* Temporary seam: resource destruction still uses the verified
           3925 library helper while generated resource creation remains. */
        DOA3(0x001b4880u, 0x001e0750u), saved_esp, __FILE__, __LINE__);
#endif
}

bool recomp_d3d_texture_adapter_describe(
    uint32_t resource, RecompD3dTextureDesc *out)
{
    if (resource == 0u || out == NULL) {
        return false;
    }
    uint32_t format_dword =
        *recomp_memory_u32(resource + D3D_TEXTURE_FORMAT_OFFSET);
    uint32_t size_dword =
        *recomp_memory_u32(resource + D3D_TEXTURE_SIZE_OFFSET);
    uint32_t data = *recomp_memory_u32(resource + D3D_TEXTURE_DATA_OFFSET);
    uint32_t format_byte = (format_dword >> 8u) & 0xffu;
    uint32_t descriptor = *(const uint8_t *)(const void *)
        recomp_memory_i8(D3D_FORMAT_DESCRIPTOR_TABLE + format_byte);
    return recomp_d3d_texture_describe(
        format_dword, size_dword, data, descriptor, out);
}

static void record_texture_census(uint32_t stage, uint32_t texture)
{
    RecompD3dTextureDesc desc;

    if (recomp_d3d_texture_adapter_describe(texture, &desc)) {
        recomp_d3d_texture_census_record(&texture_model.census, &desc, stage);
        if (stage < RECOMP_D3D_TEXTURE_STAGE_COUNT) {
            stage_descs[stage] = desc;
            stage_desc_valid[stage] = true;
        }
    }
}

#ifndef RECOMP_DOAXBV_BINDINGS
static void retain_resource(uint32_t resource);
#endif

static void recomp_d3d_set_texture_adapter(void)
{
    uint32_t entry_esp = recomp_runtime.registers.esp;
    uint32_t stage = stack_argument(entry_esp, 0u);
    uint32_t texture = stack_argument(entry_esp, 1u);
    uint32_t device = *recomp_memory_u32(D3D_DEVICE_GLOBAL);
    uint32_t previous_texture;
    uint32_t slot_address;
#ifdef RECOMP_DOAXBV_BINDINGS
    const uint32_t texture_format_offset = 0x0cu;
#else
    const uint32_t texture_format_offset = DOA3(0x4dcu, 0x0cu);
#endif

    if (device == 0u || !recomp_d3d_set_texture(
            &texture_model, stage, texture)) {
        fprintf(
            stderr,
            "recomp d3d: SetTexture rejected stage 0x%08" PRIx32
            " with device 0x%08" PRIx32 "\n",
            stage,
            device);
        recomp_stop(2, "d3d-set-texture:0x%08" PRIx32, stage);
    }

    if (texture != 0u) {
        record_texture_census(stage, texture);
    } else if (stage < RECOMP_D3D_TEXTURE_STAGE_COUNT) {
        stage_desc_valid[stage] = false;
    }

    slot_address = device + D3D_TEXTURE_SLOT_OFFSET + stage * 4u;
    previous_texture = *recomp_memory_u32(slot_address);
    if (previous_texture != 0u) {
        uint32_t references = *recomp_memory_u32(previous_texture);
        uint32_t remaining = references - D3D_TEXTURE_REFERENCE_STEP;

        *recomp_memory_u32(previous_texture) = remaining;
#ifdef RECOMP_DOAXBV_BINDINGS
        *recomp_memory_u32(previous_texture + 8u) = device + 0x30u;
#else
        *recomp_memory_u32(previous_texture + 8u) =
            *recomp_memory_u32(device + D3D_DEVICE_FENCE);
#endif
        if ((remaining & 0x0078ffffu) == 0u) {
            release_texture_resource(previous_texture);
        }
    }

    *recomp_memory_u32(slot_address) = texture;
    if (texture == 0u) {
        *recomp_memory_u32(device + texture_format_offset + stage * 4u) =
            D3D_TEXTURE_DISABLE_STATE;
        *recomp_memory_u32(D3D_STATE_DIRTY_MASK) |= D3D_TEXTURE_DIRTY;
    } else {
        uint32_t format_address = device + texture_format_offset + stage * 4u;
        uint32_t previous_format = *recomp_memory_u32(format_address);
        uint32_t format = texture_format_shadow(texture);

#ifdef RECOMP_DOAXBV_BINDINGS
        *recomp_memory_u32(texture) += D3D_TEXTURE_REFERENCE_STEP;
#else
        retain_resource(texture);
#endif
        if (previous_format != format) {
            *recomp_memory_u32(format_address) = format;
            *recomp_memory_u32(D3D_STATE_DIRTY_MASK) |= D3D_TEXTURE_FORMAT_DIRTY;
            if (previous_texture == 0u) {
                *recomp_memory_u32(D3D_STATE_DIRTY_MASK) |= D3D_TEXTURE_DIRTY;
            }
        }
    }
    recomp_runtime.registers.esp = entry_esp + 12u;
}

#ifndef RECOMP_DOAXBV_BINDINGS
static void retain_resource(uint32_t resource)
{
    if (resource == 0u) return;
#ifndef RECOMP_DOA3_JP_BINDINGS
    /* 3925 AddRef (0x001B4960) also retains a surface's parent; 4134 inlines a plain add. */
    uint32_t common = *recomp_memory_u32(resource);
    if ((common & 0x70000u) == 0x50000u && (common & 0x780000u) == 0u) {
        uint32_t parent = *recomp_memory_u32(resource + 0x14u);
        if (parent != 0u) *recomp_memory_u32(parent) += D3D_TEXTURE_REFERENCE_STEP;
    }
#endif
    *recomp_memory_u32(resource) += D3D_TEXTURE_REFERENCE_STEP;
}

static void drop_resource(uint32_t resource, uint32_t device, bool fence)
{
    if (resource == 0u) return;
    uint32_t remaining = *recomp_memory_u32(resource) - D3D_TEXTURE_REFERENCE_STEP;
    *recomp_memory_u32(resource) = remaining;
    if (fence) *recomp_memory_u32(resource + 8u) = *recomp_memory_u32(device + D3D_DEVICE_FENCE);
    if ((remaining & 0x78ffffu) == 0u) release_texture_resource(resource);
}

static void set_stream_source(void)
{
    uint32_t esp = recomp_runtime.registers.esp;
    uint32_t stream = stack_argument(esp, 0u), buffer = stack_argument(esp, 1u);
    uint32_t stride = stack_argument(esp, 2u);
    uint32_t device = *recomp_memory_u32(D3D_DEVICE_GLOBAL);
    if (!device || stream >= 16u) recomp_stop(2, "d3d-stream:index");
    uint32_t slot = D3D_STREAM_TABLE + stream * 12u;
    retain_resource(buffer);
    drop_resource(*recomp_memory_u32(slot + 8u), device, true);
    *recomp_memory_u32(DOA3(device + 8u, D3D_STATE_DIRTY_MASK)) |=
        *recomp_memory_u32(slot) == stride ? DOA3(0x200u, 0x40u) : DOA3(0x280u, 0x70u);
    *recomp_memory_u32(slot) = stride;
    *recomp_memory_u32(slot + 8u) = buffer;
    recomp_runtime.registers.esp = esp + 16u;
}

static void set_indices(void)
{
    uint32_t esp = recomp_runtime.registers.esp;
    uint32_t buffer = stack_argument(esp, 0u), base = stack_argument(esp, 1u);
    uint32_t device = *recomp_memory_u32(D3D_DEVICE_GLOBAL);
    if (!device) recomp_stop(2, "d3d-indices:device");
    retain_resource(buffer);
    *recomp_memory_u32(D3D_INDEX_DATA) = buffer ? *recomp_memory_u32(buffer + 4u) : 0u;
    drop_resource(*recomp_memory_u32(device + D3D_DEVICE_INDEX_BUFFER), device, false);
    *recomp_memory_u32(device + D3D_DEVICE_INDEX_BUFFER) = buffer;
    *recomp_memory_u32(device + D3D_DEVICE_BASE_VERTEX) = base;
    recomp_runtime.registers.esp = esp + 12u;
}
#endif

static void recomp_d3d_texture_lock_rect_adapter(void)
{
    uint32_t entry_esp = recomp_runtime.registers.esp;
    uint32_t locked_rect = stack_argument(entry_esp, 2u);
    uint32_t locked_address;
    uint32_t cpu_address;

    sub_001E8090();
    if (locked_rect == 0u) {
        return;
    }
    locked_address = *recomp_memory_u32(locked_rect + 4u);
    if (!recomp_d3d_texture_resolve_cpu_address(
            locked_address, &cpu_address)) {
        fprintf(
            stderr,
            "recomp d3d: Texture_LockRect rejected address 0x%08" PRIx32
            "\n",
            locked_address);
        recomp_stop(2, "d3d-texture-lock:0x%08" PRIx32, locked_address);
    }
    *recomp_memory_u32(locked_rect + 4u) = cpu_address;
}

RecompFunction recomp_d3d_texture_lookup_manual(uint32_t guest_address)
{
    switch (guest_address) {
#ifndef RECOMP_DOAXBV_BINDINGS
    case 0x001b4230u: return set_stream_source;
    case 0x001b1ea0u: return set_indices;
#endif
    case D3D_DEVICE_SET_TEXTURE_ADDRESS:
        return recomp_d3d_set_texture_adapter;
#ifdef RECOMP_DOAXBV_BINDINGS
    case D3D_TEXTURE_LOCK_RECT_ADDRESS:
        return recomp_d3d_texture_lock_rect_adapter;
#endif
    default:
        return NULL;
    }
}
