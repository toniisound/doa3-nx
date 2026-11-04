#include "d3d_vertex_shader_adapter.h"
#include "stop_report.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    D3D_DEVICE_SET_VERTEX_SHADER_ADDRESS = 0x001e7170u,
#ifdef RECOMP_DOA3_JP_BINDINGS
    /* D3D8 4134 SetVertexShader (0x001E0310) runs the 4928 algorithm below
       with a narrower declaration dirty set. */
    D3D_DEVICE_GLOBAL = 0x001ede80u,
    D3D_STATE_DIRTY_MASK = 0x001ead68u,
    D3D_VERTEX_DECLARATION_DIRTY_BITS = 0x00001200u,
#else
    D3D_DEVICE_GLOBAL = 0x001f2978u,
    D3D_STATE_DIRTY_MASK = 0x001f2984u,
    D3D_VERTEX_DECLARATION_DIRTY_BITS = 0x00001600u,
#endif
    D3D_VERTEX_DECLARATION_OFFSET = 0x0380u,
    D3D_VERTEX_SHADER_HANDLE_OFFSET = 0x0384u,
    D3D_VERTEX_PROGRAM_OFFSET = 0x0388u,
    D3D_VERTEX_BASE_DIRTY_BITS = 0x00000070u,
    D3D_VERTEX_PROGRAM_DIRTY_BIT = 0x00000400u,
};

static RecompD3dVertexShaderModel d3d_vertex_shader_model;

void recomp_d3d_vertex_shader_adapter_reset(void)
{
    recomp_d3d_vertex_shader_reset(&d3d_vertex_shader_model);
}

const RecompD3dVertexShaderModel *recomp_d3d_vertex_shader_adapter_model(void)
{
    return &d3d_vertex_shader_model;
}

static uint32_t stack_argument(uint32_t entry_esp, uint32_t index)
{
    return *recomp_memory_u32(entry_esp + 4u + index * 4u);
}

static void fail_vertex_shader(uint32_t handle)
{
    fprintf(
        stderr,
        "recomp d3d: SetVertexShader rejected handle 0x%08" PRIx32 "\n",
        handle);
    recomp_stop(2, "d3d-vertex-shader:0x%08" PRIx32, handle);
}

#if defined(RECOMP_DOAXBV_BINDINGS) || defined(RECOMP_DOA3_JP_BINDINGS)
void recomp_d3d_set_vertex_shader_adapter(void)
{
    uint32_t entry_esp = recomp_runtime.registers.esp;
    uint32_t handle = stack_argument(entry_esp, 0u);
    uint32_t device = *recomp_memory_u32(D3D_DEVICE_GLOBAL);
    uint32_t old_declaration;
    uint32_t old_flags;
    uint32_t old_program_mask;
    uint32_t declaration;
    uint32_t new_flags;
    uint32_t new_program_mask;
    uint32_t dirty;

    if (device == 0u) {
        fail_vertex_shader(handle);
    }

    old_declaration =
        *recomp_memory_u32(device + D3D_VERTEX_DECLARATION_OFFSET);
    old_flags = *recomp_memory_u32(old_declaration + 4u);
    old_program_mask = *recomp_memory_u32(old_declaration + 0x10u);

    if ((handle & 1u) == 0u) {
        uint32_t words[RECOMP_D3D_VERTEX_DECLARATION_WORDS];

        declaration = RECOMP_D3D_DEFAULT_VERTEX_DECLARATION;
        for (uint32_t i = 0u;
             i < RECOMP_D3D_VERTEX_DECLARATION_WORDS;
             ++i) {
            words[i] = *recomp_memory_u32(declaration + i * 4u);
        }
        if (!recomp_d3d_build_fixed_function_declaration(handle, words)) {
            fail_vertex_shader(handle);
        }
        for (uint32_t i = 0u;
             i < RECOMP_D3D_VERTEX_DECLARATION_WORDS;
             ++i) {
            *recomp_memory_u32(declaration + i * 4u) = words[i];
        }
    } else {
        declaration = handle - 1u;
    }

    new_flags = *recomp_memory_u32(declaration + 4u);
    new_program_mask = *recomp_memory_u32(declaration + 0x10u);
    dirty = *recomp_memory_u32(D3D_STATE_DIRTY_MASK) |
        D3D_VERTEX_BASE_DIRTY_BITS;
    if (old_program_mask != new_program_mask) {
        dirty |= D3D_VERTEX_PROGRAM_DIRTY_BIT;
    }
    if (old_flags != new_flags) {
        dirty |= D3D_VERTEX_DECLARATION_DIRTY_BITS;
    }
    /* SelectVertexShader invalidates constants even when two programs have
       identical declaration flags and texture-coordinate masks. */
    if ((new_flags & 0x10u) != 0u) {
        dirty |= 0x1070u;
    }

    if (!recomp_d3d_bind_vertex_shader(
            &d3d_vertex_shader_model, handle, declaration)) {
        fail_vertex_shader(handle);
    }
    *recomp_memory_u32(D3D_STATE_DIRTY_MASK) = dirty;
    *recomp_memory_u32(device + D3D_VERTEX_DECLARATION_OFFSET) = declaration;
    *recomp_memory_u32(device + D3D_VERTEX_SHADER_HANDLE_OFFSET) = handle;
    *recomp_memory_u32(device + D3D_VERTEX_PROGRAM_OFFSET) = 0u;
    recomp_runtime.registers.esp = entry_esp + 8u;
}

#else
void recomp_d3d_set_vertex_shader_adapter(void)
{
    uint32_t esp = recomp_runtime.registers.esp;
    uint32_t handle = stack_argument(esp, 0u);
    uint32_t device = *recomp_memory_u32(0x001c3390u);
    if (!device) fail_vertex_shader(handle);
    uint32_t old = *recomp_memory_u32(device + 0x470u);
    uint32_t flags = *recomp_memory_u32(old + 4u);
    uint32_t declaration = handle - 1u;
    if ((handle & 1u) == 0u) {
        uint32_t words[RECOMP_D3D_VERTEX_DECLARATION_WORDS] = {0};
        uint32_t count = (handle >> 8u) & 15u;
        if (count > 4u || !recomp_d3d_build_fixed_function_declaration(handle, words))
            fail_vertex_shader(handle);
        declaration = 0x001c0688u;
        uint32_t *out = recomp_memory_u32(declaration);
        memset(recomp_memory(declaration, 0x16cu), 0, 0x16cu);
        out[1] = words[1];
        out[3] = count;
        for (uint32_t i = 0u; i < 16u; ++i) out[(0x30u + i * 16u) / 4u] = 2u;
        for (uint32_t i = 0u; i < 5u; ++i)
            memcpy(out + (0x2cu + i * 16u) / 4u, words + (0x18u + i * 16u) / 4u, 8u);
        for (uint32_t i = 0u; i < count; ++i) {
            memcpy(out + (0x12cu + i * 16u) / 4u, words + (0xa8u + i * 16u) / 4u, 8u);
            out[6u + i] = (words[(0xacu + i * 16u) / 4u] >> 4u);
        }
    }
    *recomp_memory_u32(device + 8u) |= 0x3a4u |
        (flags != *recomp_memory_u32(declaration + 4u) ? 2u : 0u);
    *recomp_memory_u32(device + 0x470u) = declaration;
    *recomp_memory_u32(device + 0x474u) = handle;
    d3d_vertex_shader_model.handle = handle;
    d3d_vertex_shader_model.declaration_address = declaration;
    ++d3d_vertex_shader_model.update_count;
    recomp_runtime.registers.esp = esp + 8u;
}
#endif

RecompFunction recomp_d3d_vertex_shader_lookup_manual(uint32_t guest_address)
{
    return guest_address ==
#ifdef RECOMP_DOAXBV_BINDINGS
        D3D_DEVICE_SET_VERTEX_SHADER_ADDRESS
#else
        0x001b45f0u
#endif
        ? recomp_d3d_set_vertex_shader_adapter
        : NULL;
}
