#ifndef DOAXBV_RECOMP_D3D_VERTEX_SHADER_MODEL_H
#define DOAXBV_RECOMP_D3D_VERTEX_SHADER_MODEL_H

#include <stdbool.h>
#include <stdint.h>

enum {
#ifdef RECOMP_DOA3_JP_BINDINGS
    RECOMP_D3D_DEFAULT_VERTEX_DECLARATION = 0x001eb278u, /* D3D8 4134 */
#else
    RECOMP_D3D_DEFAULT_VERTEX_DECLARATION = 0x001f2ff8u,
#endif
    RECOMP_D3D_VERTEX_DECLARATION_SIZE = 0x128u,
    RECOMP_D3D_VERTEX_DECLARATION_WORDS =
        RECOMP_D3D_VERTEX_DECLARATION_SIZE / 4u,
};

typedef struct RecompD3dVertexShaderModel {
    uint32_t handle;
    uint32_t declaration_address;
    uint32_t update_count;
} RecompD3dVertexShaderModel;

void recomp_d3d_vertex_shader_reset(RecompD3dVertexShaderModel *model);

/* Mutates an existing declaration image exactly as the fixed-function
   declaration builder does. Unwritten words remain unchanged. */
bool recomp_d3d_build_fixed_function_declaration(
    uint32_t fvf,
    uint32_t declaration[RECOMP_D3D_VERTEX_DECLARATION_WORDS]);

bool recomp_d3d_bind_vertex_shader(
    RecompD3dVertexShaderModel *model,
    uint32_t handle,
    uint32_t declaration_address);

/* Unpack bounded vertex-program upload blocks; reject other command types. */
bool recomp_d3d_unpack_vertex_program(const uint32_t *packed, uint32_t words,
    uint32_t instructions, uint32_t program[136][4]);

#endif
