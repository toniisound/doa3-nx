// SPDX-License-Identifier: GPL-3.0-or-later
// Nintendo Switch OpenGL backend for the d3d_presenter.h seam.
//
// A port of recomp-runtime/d3d_presenter_sdl_gpu.cpp to OpenGL 4.3 core on
// devkitPro's mesa (nouveau): the same texture cache, render targets, draw
// segments, uniform layout and generated shaders (the Vulkan GLSL, minus its
// descriptor sets). It runs on its own thread behind d3d_presenter_gl_thread
// .cpp, which owns the d3d_presenter.h entry points.
//
// Coordinates: clip control is UPPER_LEFT / ZERO_TO_ONE, so D3D clip space
// and texture rows map as on the Xbox (row 0 = top of a target, depth 0..1).
// Targets are textures; the main target is shown with a final pass that
// letterboxes, flips to the window's lower-left origin and applies gamma.
#include "d3d_presenter_gl.h"

#include "d3d_draw_model.h"
#include "d3d_texture_model.h"
#include "d3d_vertex_program.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GL_LOWER_LEFT
#define GL_LOWER_LEFT 0x8CA1
#endif
#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x8CA2
#endif
#ifndef GL_NEGATIVE_ONE_TO_ONE
#define GL_NEGATIVE_ONE_TO_ONE 0x935E
#endif
#ifndef GL_ZERO_TO_ONE
#define GL_ZERO_TO_ONE 0x935F
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

extern "C" uint64_t doa3_nx_ticks(void);
extern "C" void doa3_nx_counter_add(int counter, uint64_t value);
extern "C" void doa3_nx_render_stage_add(int stage, uint64_t ticks);

namespace {

constexpr uint32_t kTextureSlots = 4096u;
constexpr uint32_t kPaletteBytes = 256u * 4u;
constexpr uint64_t kTargetByteLimit = 64u * 1024u * 1024u;
constexpr uint32_t kVertexRingBytes = 32u * 1024u * 1024u;
constexpr uint32_t kIndexRingBytes = 8u * 1024u * 1024u;
constexpr uint32_t kUniformRingBytes = 16u * 1024u * 1024u;
constexpr uint32_t kHeaderUniformFloats = 156u;
constexpr uint32_t kLightingUniformFloats = kHeaderUniformFloats;
constexpr uint32_t kTransformUniformFloats = 488u;
constexpr uint32_t kProgramConstantFloats = 192u * 4u;
constexpr uint32_t kNoProgramConstants = UINT32_MAX;

typedef void (*ClipControlFn)(GLenum origin, GLenum depth);
ClipControlFn glClipControlPtr = nullptr;
typedef void (*BufferStorageFn)(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
BufferStorageFn glBufferStoragePtr = nullptr;
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif

// A streaming buffer. With ARB_buffer_storage it stays mapped for good and
// segments are memcpy'd straight in (glBufferSubData cost 4-6 ms a frame on
// hardware). The ring is split in quarters; leaving a quarter fences it, and
// re-entering it a lap later waits for that fence, so the GPU never reads
// bytes being overwritten. A segment never exceeds a quarter.
struct StreamRing {
    GLuint buffer = 0;
    uint8_t *map = nullptr;   // null: glBufferSubData with orphaning
    uint32_t capacity = 0;
    uint32_t used = 0;
    uint32_t quarter = 0;     // current quarter
    GLsync fences[4]{};
    uint64_t waits = 0;
};

struct TextureEntry {
    bool used = false;
    uint32_t data = 0;
    uint32_t format_byte = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 0;
    uint8_t palette[kPaletteBytes]{};
    uint64_t fingerprint = 0;
    uint32_t checked_present = UINT32_MAX;  // frame whose draws already matched it
    GLuint texture = 0;
};

struct RenderTargetEntry {
    RecompD3dTextureDesc desc{};
    GLuint texture = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct DepthTargetEntry {
    RecompD3dTextureDesc desc{};
    GLuint renderbuffer = 0;
    uint64_t bytes = 0;
    bool host_main = false;
};

struct Program {
    GLuint program = 0;
    bool failed = false;
};

// Everything a draw needs besides its buffers and uniforms; compared
// field-by-field so redundant GL state changes are skipped.
struct DrawState {
    GLuint program = 0;
    GLuint vao = 0;
    GLenum primitive = GL_TRIANGLES;
    RecompD3dCullMode cull = RECOMP_D3D_CULL_NONE;
    bool depth_test = false;
    bool depth_write = false;
    GLenum depth_func = GL_LEQUAL;
    bool stencil = false;
    GLenum stencil_func = GL_ALWAYS;
    uint8_t stencil_read = 0xff, stencil_write = 0xff;
    GLenum stencil_fail = GL_KEEP, stencil_zfail = GL_KEEP, stencil_pass = GL_KEEP;
    bool blend = false;
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO, src_a = GL_ONE, dst_a = GL_ZERO;
    GLenum blend_op = GL_FUNC_ADD;
    uint8_t color_mask = 0x0f;
};

struct QueuedDraw {
    DrawState state;
    uint32_t vertex_offset = 0;
    uint32_t vertex_stride = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    GLuint texture = 0;
    GLuint mask_texture = 0;
    GLuint sampler0 = 0;
    GLuint sampler1 = 0;
    uint32_t constant_color = 0;
    bool constant_alpha = false;
    uint8_t stencil_ref = 0;
    float viewport[6]{}; // x, y, w, h, min z, max z; zero width keeps the target
    uint32_t uniform_offset = 0;  // bytes into the segment's uniform data
    uint32_t uniform_size = 0;    // bytes
    uint32_t program_offset = kNoProgramConstants;
};

struct DrawSegment {
    GLuint target_color = 0;
    GLuint target_depth = 0; // renderbuffer
    float target_w = 0.0f;
    float target_h = 0.0f;
    std::vector<uint8_t> vertex_data;
    std::vector<uint8_t> index_data;
    std::vector<uint8_t> uniform_data;
    std::vector<QueuedDraw> draws;
    // Vertex spans already in vertex_data: captured payload pointers are
    // stable within a packet, so the same pointer and size is the same data.
    std::unordered_map<const void *, std::pair<uint32_t, uint32_t>> vertex_seen; // size, offset
};

GLenum glBlendFactor(RecompD3dBlendFactor factor, bool alpha_channel)
{
    switch (factor) {
    case RECOMP_D3D_BLEND_ZERO: return GL_ZERO;
    case RECOMP_D3D_BLEND_ONE: return GL_ONE;
    case RECOMP_D3D_BLEND_SRC_COLOR: return alpha_channel ? GL_SRC_ALPHA : GL_SRC_COLOR;
    case RECOMP_D3D_BLEND_INV_SRC_COLOR: return alpha_channel ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR;
    case RECOMP_D3D_BLEND_SRC_ALPHA: return GL_SRC_ALPHA;
    case RECOMP_D3D_BLEND_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case RECOMP_D3D_BLEND_DST_ALPHA: return GL_DST_ALPHA;
    case RECOMP_D3D_BLEND_INV_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case RECOMP_D3D_BLEND_DST_COLOR: return alpha_channel ? GL_DST_ALPHA : GL_DST_COLOR;
    case RECOMP_D3D_BLEND_INV_DST_COLOR: return alpha_channel ? GL_ONE_MINUS_DST_ALPHA : GL_ONE_MINUS_DST_COLOR;
    case RECOMP_D3D_BLEND_SRC_ALPHA_SATURATE: return alpha_channel ? GL_ONE : GL_SRC_ALPHA_SATURATE;
    case RECOMP_D3D_BLEND_CONSTANT_COLOR:
    case RECOMP_D3D_BLEND_CONSTANT_ALPHA: return GL_CONSTANT_COLOR;
    case RECOMP_D3D_BLEND_INV_CONSTANT_COLOR:
    case RECOMP_D3D_BLEND_INV_CONSTANT_ALPHA: return GL_ONE_MINUS_CONSTANT_COLOR;
    default: return GL_ONE;
    }
}

GLenum glBlendOpFor(RecompD3dBlendOp op)
{
    switch (op) {
    case RECOMP_D3D_BLEND_OP_MIN: return GL_MIN;
    case RECOMP_D3D_BLEND_OP_MAX: return GL_MAX;
    case RECOMP_D3D_BLEND_OP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case RECOMP_D3D_BLEND_OP_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    default: return GL_FUNC_ADD;
    }
}

GLenum glCompare(RecompD3dCompareFunc func)
{
    switch (func) {
    case RECOMP_D3D_COMPARE_NEVER: return GL_NEVER;
    case RECOMP_D3D_COMPARE_LESS: return GL_LESS;
    case RECOMP_D3D_COMPARE_EQUAL: return GL_EQUAL;
    case RECOMP_D3D_COMPARE_LESS_EQUAL: return GL_LEQUAL;
    case RECOMP_D3D_COMPARE_GREATER: return GL_GREATER;
    case RECOMP_D3D_COMPARE_NOT_EQUAL: return GL_NOTEQUAL;
    case RECOMP_D3D_COMPARE_GREATER_EQUAL: return GL_GEQUAL;
    case RECOMP_D3D_COMPARE_ALWAYS: return GL_ALWAYS;
    default: return GL_LEQUAL;
    }
}

GLenum glStencilOpFor(RecompD3dStencilOp op)
{
    switch (op) {
    case RECOMP_D3D_STENCIL_ZERO: return GL_ZERO;
    case RECOMP_D3D_STENCIL_REPLACE: return GL_REPLACE;
    case RECOMP_D3D_STENCIL_INCRSAT: return GL_INCR;
    case RECOMP_D3D_STENCIL_DECRSAT: return GL_DECR;
    case RECOMP_D3D_STENCIL_INVERT: return GL_INVERT;
    case RECOMP_D3D_STENCIL_INCRWRAP: return GL_INCR_WRAP;
    case RECOMP_D3D_STENCIL_DECRWRAP: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

GLint glAddressMode(uint32_t mode)
{
    switch (mode) {
    case 1: return GL_REPEAT;
    case 2: return GL_MIRRORED_REPEAT;
    case 3: case 4: case 5: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

// Render-thread stage timers (profile.cpp), reported once a second.
struct StageTimer {
    int stage;
    uint64_t start;
    explicit StageTimer(int s) : stage(s), start(doa3_nx_ticks()) {}
    ~StageTimer() { doa3_nx_render_stage_add(stage, doa3_nx_ticks() - start); }
};

uint64_t textureFingerprint(const void *bytes, uint32_t count)
{
    const auto *data = static_cast<const uint8_t *>(bytes);
    uint64_t hash = 0xcbf29ce484222325ull ^ count;
    if (count < 8u) {
        for (uint32_t i = 0u; i < count; ++i) hash = (hash ^ data[i]) * 0x100000001b3ull;
        return hash;
    }
    constexpr uint32_t kSamples = 128u;
    for (uint32_t i = 0u; i < kSamples; ++i) {
        uint64_t word;
        std::memcpy(&word, data + static_cast<uint64_t>(count - 8u) * i / (kSamples - 1u), 8u);
        hash = (hash ^ word) * 0x100000001b3ull;
    }
    return hash;
}

uint64_t programHash(const uint32_t (*tokens)[4], uint32_t count)
{
    const auto *data = reinterpret_cast<const uint8_t *>(tokens);
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t i = 0u; i < count * 16u; ++i) hash = (hash ^ data[i]) * 0x100000001b3ull;
    return hash;
}

// --- DXT software decode (used only when the driver lacks S3TC) -----------

void decodeColorBlock(const uint8_t *block, uint8_t out[16][4], bool dxt1)
{
    uint16_t c0, c1;
    std::memcpy(&c0, block, 2);
    std::memcpy(&c1, block + 2, 2);
    uint8_t colors[4][4];
    auto expand = [](uint16_t c, uint8_t *rgba) {
        rgba[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
        rgba[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
        rgba[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
        rgba[3] = 255;
    };
    expand(c0, colors[0]);
    expand(c1, colors[1]);
    for (int i = 0; i < 3; ++i) {
        if (!dxt1 || c0 > c1) {
            colors[2][i] = static_cast<uint8_t>((2 * colors[0][i] + colors[1][i]) / 3);
            colors[3][i] = static_cast<uint8_t>((colors[0][i] + 2 * colors[1][i]) / 3);
        } else {
            colors[2][i] = static_cast<uint8_t>((colors[0][i] + colors[1][i]) / 2);
            colors[3][i] = 0;
        }
    }
    colors[2][3] = 255;
    colors[3][3] = (!dxt1 || c0 > c1) ? 255 : 0;
    uint32_t bits;
    std::memcpy(&bits, block + 4, 4);
    for (int i = 0; i < 16; ++i) std::memcpy(out[i], colors[(bits >> (2 * i)) & 3], 4);
}

// Decodes one mip level of DXT1/3/5 into RGBA8.
void decodeDxt(const uint8_t *src, uint32_t width, uint32_t height, uint32_t format, uint8_t *dst)
{
    const uint32_t block_bytes = format == RECOMP_D3D_TEXTURE_FORMAT_DXT1 ? 8u : 16u;
    const uint32_t bw = (width + 3u) / 4u, bh = (height + 3u) / 4u;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t *block = src + (by * bw + bx) * block_bytes;
            uint8_t texels[16][4];
            if (format == RECOMP_D3D_TEXTURE_FORMAT_DXT1) {
                decodeColorBlock(block, texels, true);
            } else {
                decodeColorBlock(block + 8, texels, false);
                if (format == RECOMP_D3D_TEXTURE_FORMAT_DXT3) {
                    for (int i = 0; i < 16; ++i) {
                        const uint8_t a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                        texels[i][3] = static_cast<uint8_t>(a * 17);
                    }
                } else {
                    const uint8_t a0 = block[0], a1 = block[1];
                    uint8_t alphas[8] = {a0, a1};
                    if (a0 > a1) {
                        for (int i = 1; i < 7; ++i) alphas[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                    } else {
                        for (int i = 1; i < 5; ++i) alphas[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                        alphas[6] = 0;
                        alphas[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i) texels[i][3] = alphas[(bits >> (3 * i)) & 7];
                }
            }
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t px = bx * 4u + x, py = by * 4u + y;
                    if (px < width && py < height) {
                        std::memcpy(dst + (static_cast<size_t>(py) * width + px) * 4u, texels[y * 4 + x], 4);
                    }
                }
            }
        }
    }
}

// --- Shaders ---------------------------------------------------------------

const char kUniformBlock[] = R"(
layout(std140, binding = 0) uniform TransformUniforms {
    mat4 wvp[4];
    vec4 draw_flags;
    vec4 blend_flags;
    vec4 texture_factor;
    vec4 lighting_flags;
    vec4 texture_flags;
    mat4 reflection_world_view;
    mat4 reflection_normal;
    mat4 reflection_transform;
    vec4 reflection_diffuse;
    vec4 reflection_flags;
    vec4 directional_flags;
    vec4 light_stage;
    vec4 fog_params;
    vec4 fog_color;
    mat4 directional_normals[4];
    vec4 directional_base;
    vec4 directional_material;
    vec4 directional_directions[8];
    vec4 directional_colors[8];
    mat4 light_world[4];
    vec4 light_positions[8];
    vec4 light_attenuation[8];
    vec4 light_ambient[8];
    vec4 light_specular[8];
    vec4 light_view;
} u;
)";

std::string buildVertexShader(const RecompD3dVertexLayout &layout, uint32_t program_count,
                              const uint32_t (*program_tokens)[4])
{
    const bool has_normal = layout.normal_offset != RECOMP_D3D_FVF_ABSENT;
    const bool has_diffuse = layout.diffuse_offset != RECOMP_D3D_FVF_ABSENT;
    const bool has_texcoord = layout.texcoord_offset != RECOMP_D3D_FVF_ABSENT;
    const bool four_coords = layout.texcoord_count == 4u;
    const bool two_coords = layout.texcoord_count == 2u;

    std::string s;
    s.reserve(6144);
    s += "#version 430 core\n";
    s += kUniformBlock;
    if (program_count) s += "layout(std140, binding = 1) uniform ProgramConstants { vec4 vc[192]; } pc;\n";
    s += layout.pretransformed ? "layout(location = 0) in vec4 in_position;\n"
                               : "layout(location = 0) in vec3 in_position;\n";
    if (layout.blend_weight_count == 1u) s += "layout(location = 1) in float in_weights;\n";
    else if (layout.blend_weight_count == 2u) s += "layout(location = 1) in vec2 in_weights;\n";
    else if (layout.blend_weight_count == 3u) s += "layout(location = 1) in vec3 in_weights;\n";
    if (has_normal) s += "layout(location = 2) in vec3 in_normal;\n";
    if (has_diffuse) s += "layout(location = 3) in vec4 in_diffuse;\n";
    if (has_texcoord) s += "layout(location = 4) in vec2 in_texcoord;\n";
    if (four_coords) {
        s += "layout(location = 5) in vec2 in_texcoord1;\n";
        s += "layout(location = 6) in vec2 in_texcoord2;\n";
        s += "layout(location = 7) in vec2 in_texcoord3;\n";
    } else if (two_coords) {
        s += "layout(location = 5) in vec2 in_texcoord1;\n";
    }
    s += "layout(location = 0) out vec4 v_color;\n";
    s += "layout(location = 1) out vec2 v_texcoord;\n";
    s += "layout(location = 2) out vec2 v_reflection_coord;\n";
    if (four_coords) {
        s += "layout(location = 3) out vec2 v_texcoord1;\n";
        s += "layout(location = 4) out vec2 v_texcoord2;\n";
        s += "layout(location = 5) out vec2 v_texcoord3;\n";
    } else if (two_coords) {
        s += "layout(location = 3) out vec2 v_texcoord1;\n";
    }
    if (program_count) s += "layout(location = 6) out vec2 v_program_q;\n";
    s += "layout(location = 7) out vec3 v_lit_specular;\n";
    s += "layout(location = 8) out float v_fog;\n";
    s += "void main() {\n";
    s += "    v_lit_specular = vec3(0.0);\n";
    s += "    v_fog = 1.0;\n";

    if (program_count) {
        std::string body;
        if (!recomp_d3d_vertex_program_glsl_source(program_tokens, program_count, body)) return {};
        s += body;
        s += "}\n";
        return s;
    }

    const std::string bw = std::to_string(layout.blend_weight_count);
    if (layout.pretransformed) {
        s += "    gl_Position = (u.wvp[0] * vec4(in_position.xyz, 1.0)) / in_position.w;\n";
    } else if (layout.blend_weight_count != 0u) {
        s += "    if (u.blend_flags.x > 0.5) {\n";
        s += "        vec4 p = vec4(0.0);\n";
        s += "        float remainder = 1.0;\n";
        if (layout.blend_weight_count == 1u) {
            s += "        p += in_weights * (u.wvp[0] * vec4(in_position, 1.0));\n";
            s += "        remainder -= in_weights;\n";
        } else {
            for (uint32_t i = 0u; i < layout.blend_weight_count; ++i) {
                const std::string n = std::to_string(i);
                s += "        p += in_weights[" + n + "] * (u.wvp[" + n + "] * vec4(in_position, 1.0));\n";
                s += "        remainder -= in_weights[" + n + "];\n";
            }
        }
        s += "        p += remainder * (u.wvp[" + bw + "] * vec4(in_position, 1.0));\n";
        s += "        gl_Position = p;\n";
        s += "    } else {\n";
        s += "        gl_Position = u.wvp[0] * vec4(in_position, 1.0);\n";
        s += "    }\n";
    } else {
        s += "    gl_Position = u.wvp[0] * vec4(in_position, 1.0);\n";
    }

    if (has_diffuse) {
        s += "    v_color = in_diffuse.zyxw;\n";
    } else if (has_normal && !layout.pretransformed) {
        s += "    if (u.directional_flags.x > 0.5) {\n";
        s += "        vec3 n = (u.directional_normals[0] * vec4(in_normal, 0.0)).xyz;\n";
        s += "        vec3 world_pos = (u.light_world[0] * vec4(in_position, 1.0)).xyz;\n";
        if (layout.blend_weight_count != 0u) {
            s += "        if (u.blend_flags.x > 0.5) {\n";
            s += "            n = vec3(0.0);\n";
            s += "            world_pos = vec3(0.0);\n";
            s += "            float remainder = 1.0;\n";
            if (layout.blend_weight_count == 1u) {
                s += "            n += in_weights * (u.directional_normals[0] * vec4(in_normal, 0.0)).xyz;\n";
                s += "            world_pos += in_weights * (u.light_world[0] * vec4(in_position, 1.0)).xyz;\n";
                s += "            remainder -= in_weights;\n";
            } else {
                for (uint32_t i = 0u; i < layout.blend_weight_count; ++i) {
                    const std::string n = std::to_string(i);
                    s += "            n += in_weights[" + n + "] * (u.directional_normals[" + n + "] * vec4(in_normal, 0.0)).xyz;\n";
                    s += "            world_pos += in_weights[" + n + "] * (u.light_world[" + n + "] * vec4(in_position, 1.0)).xyz;\n";
                    s += "            remainder -= in_weights[" + n + "];\n";
                }
            }
            s += "            n += remainder * (u.directional_normals[" + bw + "] * vec4(in_normal, 0.0)).xyz;\n";
            s += "            world_pos += remainder * (u.light_world[" + bw + "] * vec4(in_position, 1.0)).xyz;\n";
            s += "        }\n";
        }
        s += "        if (u.directional_flags.y > 0.5 && dot(n, n) > 0.0) n = normalize(n);\n";
        s += "        vec3 rgb = u.directional_base.rgb;\n";
        s += "        vec3 spec = vec3(0.0);\n";
        s += "        int light_cnt = int(u.directional_flags.z);\n";
        s += "        for (int i = 0; i < light_cnt; ++i) {\n";
        s += "            vec3 l = u.directional_directions[i].xyz;\n";
        s += "            float atten = 1.0;\n";
        s += "            if (u.light_positions[i].w > 0.5) {\n";
        s += "                l = u.light_positions[i].xyz - world_pos;\n";
        s += "                float d = length(l);\n";
        s += "                float denominator = dot(u.light_attenuation[i].xyz, vec3(1.0, d, d * d));\n";
        s += "                atten = (d <= u.light_attenuation[i].w && denominator > 0.0) ? 1.0 / denominator : 0.0;\n";
        s += "                l = d > 0.0 ? l / d : vec3(0.0);\n";
        s += "            }\n";
        s += "            float ndotl = dot(n, l);\n";
        s += "            rgb += (u.directional_material.rgb * u.directional_colors[i].rgb * max(0.0, ndotl) + u.light_ambient[i].rgb) * atten;\n";
        s += "            if (u.light_view.w > 0.0 && ndotl > 0.0)\n";
        s += "                spec += atten * u.light_specular[i].rgb * pow(max(dot(n, normalize(l + u.light_view.xyz)), 1e-6), u.light_view.w);\n";
        s += "        }\n";
        s += "        v_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);\n";
        s += "        if (u.directional_flags.w > 0.5) v_color.a = u.directional_material.a;\n";
        s += "        v_lit_specular = clamp(spec, 0.0, 1.0);\n";
        s += "    } else {\n";
        s += "        v_color = vec4(abs(normalize(in_normal)), 1.0);\n";
        s += "    }\n";
    } else {
        s += "    v_color = vec4(0.75, 0.75, 0.78, 1.0);\n";
    }

    s += has_texcoord ? "    v_texcoord = in_texcoord;\n" : "    v_texcoord = vec2(0.0);\n";
    if (four_coords) {
        s += "    v_texcoord1 = in_texcoord1;\n";
        s += "    v_texcoord2 = in_texcoord2;\n";
        s += "    v_texcoord3 = in_texcoord3;\n";
    } else if (two_coords) {
        s += "    v_texcoord1 = in_texcoord1;\n";
    }
    s += "    v_reflection_coord = vec2(0.0);\n";
    if (has_texcoord && !layout.pretransformed) {
        s += "    if (u.reflection_flags.x > 1.5) {\n";
        s += "        v_reflection_coord = in_texcoord;\n";
        s += "    } else if (u.reflection_flags.x > 0.5) {\n";
        s += "        vec3 eye = (u.reflection_world_view * vec4(in_position, 1.0)).xyz;\n";
        if (has_normal) {
            s += "        vec3 n = (u.reflection_normal * vec4(in_normal, 0.0)).xyz;\n";
            s += "        if (u.reflection_flags.y > 0.5) n = normalize(n);\n";
            s += "        vec3 r = reflect(normalize(eye), n);\n";
        } else {
            s += "        vec3 r = normalize(eye);\n";
        }
        s += "        v_reflection_coord = (u.reflection_transform * vec4(r, 1.0)).xy;\n";
        s += "    }\n";
    }
    s += "    if (u.fog_params.x > 0.5) {\n";
    s += "        float d = max(gl_Position.w, 0.0);\n";
    s += "        float f = u.fog_params.x < 1.5 ? (u.fog_params.y - d) * u.fog_params.z\n";
    s += "            : u.fog_params.x < 2.5 ? exp(-d * u.fog_params.y)\n";
    s += "            : exp(-d * d * u.fog_params.y * u.fog_params.y);\n";
    s += "        v_fog = clamp(f, 0.0, 1.0);\n";
    s += "    }\n";
    s += "}\n";
    return s;
}

std::string buildFragmentShader(const RecompD3dVertexLayout &layout, uint32_t program_count)
{
    const bool has_diffuse = layout.diffuse_offset != RECOMP_D3D_FVF_ABSENT;
    const bool four_coords = layout.texcoord_count == 4u;
    const bool two_coords = layout.texcoord_count == 2u;

    std::string s;
    s.reserve(4096);
    s += "#version 430 core\n";
    s += kUniformBlock;
    s += "layout(binding = 0) uniform sampler2D guest_texture;\n";
    s += "layout(binding = 1) uniform sampler2D alpha_mask;\n";
    s += "layout(location = 0) in vec4 v_color;\n";
    s += "layout(location = 1) in vec2 v_texcoord;\n";
    s += "layout(location = 2) in vec2 v_reflection_coord;\n";
    if (four_coords) {
        s += "layout(location = 3) in vec2 v_texcoord1;\n";
        s += "layout(location = 4) in vec2 v_texcoord2;\n";
        s += "layout(location = 5) in vec2 v_texcoord3;\n";
    } else if (two_coords) {
        s += "layout(location = 3) in vec2 v_texcoord1;\n";
    }
    if (program_count) s += "layout(location = 6) in vec2 v_program_q;\n";
    s += "layout(location = 7) in vec3 v_lit_specular;\n";
    s += "layout(location = 8) in float v_fog;\n";
    s += "layout(location = 0) out vec4 out_color;\n";
    s += "void main() {\n";
    s += "    vec2 tc = v_texcoord;\n";
    s += "    vec2 refl_tc = v_reflection_coord;\n";
    if (program_count) {
        s += "    tc /= v_program_q.x;\n";
        s += "    if (u.reflection_flags.z > 0.5) refl_tc /= v_program_q.y;\n";
    }
    s += "    vec4 shaded = v_color;\n";
    s += "    if (u.reflection_flags.x > 0.5) {\n";
    s += "        vec4 base = texture(guest_texture, tc);\n";
    s += "        base.a *= u.reflection_diffuse.a;\n";
    s += "        vec4 env = texture(alpha_mask, refl_tc);\n";
    s += "        vec4 diffuse = u.reflection_diffuse;\n";
    s += "        if (u.directional_flags.x > 0.5) diffuse.rgb = v_color.rgb;\n";
    s += "        shaded = mix(base, env, env.a) * diffuse;\n";
    s += "    } else\n";
    if (four_coords) {
        s += "    if (u.texture_flags.z > 0.5) {\n";
        s += "        vec4 t0 = texture(guest_texture, tc * u.texture_flags.xy);\n";
        s += "        vec4 t1 = texture(guest_texture, v_texcoord1 * u.texture_flags.xy);\n";
        s += "        vec4 t2 = texture(guest_texture, v_texcoord2 * u.texture_flags.xy);\n";
        s += "        vec4 t3 = texture(guest_texture, v_texcoord3 * u.texture_flags.xy);\n";
        s += "        shaded = 0.5 * (clamp((128.0 / 255.0) * (t0 + t1), 0.0, 1.0) + clamp((128.0 / 255.0) * (t2 + t3), 0.0, 1.0));\n";
        s += "    } else\n";
    } else if (two_coords) {
        s += "    if (u.texture_flags.w > 0.5) {\n";
        s += "        vec3 rgb = texture(guest_texture, v_texcoord1 * u.texture_flags.xy).rgb;\n";
        s += "        float alpha = texture(alpha_mask, tc * u.lighting_flags.zw).a;\n";
        s += "        shaded = v_color * vec4(rgb, alpha);\n";
        s += "    } else\n";
    }
    s += "    if (u.blend_flags.y == 1.0) {\n";
    s += "        shaded = u.texture_factor;\n";
    s += "    } else if (u.draw_flags.x > 0.5) {\n";
    s += "        vec4 sampled = texture(guest_texture, tc * u.texture_flags.xy);\n";
    s += "        if (u.lighting_flags.y > 0.5) sampled.rgb = vec3(1.0);\n";
    s += has_diffuse ? "        shaded *= sampled;\n" : "        shaded = sampled;\n";
    s += "        if (u.directional_flags.w > 0.5) {\n";
    s += "            shaded.rgb = (u.light_stage.x > 0.5 ? sampled.rgb : vec3(1.0)) *\n";
    s += "                (u.light_stage.y > 0.5 ? v_color.rgb : vec3(1.0));\n";
    s += "            shaded.a = (u.light_stage.z > 0.5 ? sampled.a : 1.0) *\n";
    s += "                (u.light_stage.w > 0.5 ? v_color.a : 1.0);\n";
    s += "        } else if (u.directional_flags.x > 0.5) shaded.rgb *= v_color.rgb;\n";
    s += "        if (u.lighting_flags.x > 0.5) shaded.rgb = vec3(0.0);\n";
    s += "        shaded.a = (u.blend_flags.w > 0.5) ? u.blend_flags.z : (shaded.a * u.blend_flags.z);\n";
    s += "        if (u.blend_flags.y > 1.5) shaded *= u.texture_factor;\n";
    s += "    }\n";
    s += "    if (u.directional_flags.w > 0.5) shaded.rgb += v_lit_specular;\n";
    s += "    if (u.fog_params.x > 0.5) shaded.rgb = mix(u.fog_color.rgb, shaded.rgb, v_fog);\n";
    s += "    if (u.reflection_flags.z > 0.5) {\n";
    s += "        shaded.a *= texture(alpha_mask, refl_tc, u.reflection_flags.w).a;\n";
    s += "    }\n";
    s += "    if (u.draw_flags.y > 0.5) {\n";
    s += "        float alpha = round(clamp(shaded.a, 0.0, 1.0) * 255.0);\n";
    s += "        float ref = u.draw_flags.w;\n";
    s += "        int func = int(u.draw_flags.z);\n";
    s += "        bool alpha_pass = (func == 7) ||\n";
    s += "            (func == 1 && alpha < ref) ||\n";
    s += "            (func == 2 && alpha == ref) ||\n";
    s += "            (func == 3 && alpha <= ref) ||\n";
    s += "            (func == 4 && alpha > ref) ||\n";
    s += "            (func == 5 && alpha != ref) ||\n";
    s += "            (func == 6 && alpha >= ref);\n";
    s += "        if (!alpha_pass) discard;\n";
    s += "    }\n";
    s += "    out_color = shaded;\n";
    s += "}\n";
    return s;
}

const char kPresentVertex[] = R"(#version 430 core
layout(location = 0) out vec2 v_uv;
uniform vec4 u_rect; // destination x, y, w, h in NDC units (lower-left origin)
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)); // strip corners 0..1
    v_uv = vec2(p.x, 1.0 - p.y);  // texture row 0 is the top of the frame
    gl_Position = vec4(u_rect.xy + p * u_rect.zw * 2.0, 0.0, 1.0);
}
)";

const char kPresentFragment[] = R"(#version 430 core
layout(binding = 0) uniform sampler2D frame;
layout(std140, binding = 2) uniform GammaRamp { vec4 ramp[256]; } g;
uniform int u_gamma;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 c = texture(frame, v_uv);
    if (u_gamma != 0) {
        uvec3 i = uvec3(clamp(c.rgb, 0.0, 1.0) * 255.0 + 0.5);
        c.rgb = vec3(g.ramp[i.r].r, g.ramp[i.g].g, g.ramp[i.b].b);
    }
    out_color = vec4(c.rgb, 1.0);
}
)";

GLuint compileShader(GLenum stage, const std::string &source, uint64_t key)
{
    GLuint shader = glCreateShader(stage);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        std::fprintf(stderr, "[gl] %s shader key=%016llx failed: %s\n",
            stage == GL_VERTEX_SHADER ? "vertex" : "fragment", (unsigned long long)key, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vs, GLuint fs, uint64_t key)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof log, nullptr, log);
        std::fprintf(stderr, "[gl] program key=%016llx link failed: %s\n", (unsigned long long)key, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

struct RecompD3dPresenter {
    RecompD3dPresenterConfig config{};
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    uint32_t window_w = 1280, window_h = 720;
    bool s3tc = false;

    GLuint framebuffer = 0;      // reused for every target
    GLuint backbuffer_color = 0; // texture
    GLuint backbuffer_depth = 0; // renderbuffer
    GLuint backbuffer_copy = 0;
    GLuint frontbuffer_copy = 0;
    bool copy_mips = false;
    bool frontbuffer_used = false;
    GLuint dummy_texture = 0;

    GLuint filter_sampler = 0, point_sampler = 0, program_mask_sampler = 0;
    GLuint address_samplers[6][6]{};

    GLuint vertex_buffer = 0, index_buffer = 0, uniform_buffer = 0;
    StreamRing vertex_ring, index_ring, uniform_ring;
    GLint uniform_align = 256;

    DrawSegment segment;
    std::unordered_map<uint64_t, Program> programs;
    std::unordered_map<uint64_t, GLuint> vaos; // fvf | stride << 32

    std::vector<TextureEntry> textures = std::vector<TextureEntry>(kTextureSlots);
    std::unordered_multimap<uint32_t, uint32_t> texture_index;
    uint32_t texture_count = 0, next_texture_slot = 0;

    std::vector<RenderTargetEntry> render_targets;
    std::vector<DepthTargetEntry> depth_targets;
    uint64_t target_bytes = 0;

    float scale = 1.5f;
    bool widescreen = false;
    bool flip_winding = false;

    bool gamma_enabled = false;
    float gamma_ramp[256][4]{};
    GLuint gamma_buffer = 0;
    GLuint present_program = 0;
    GLint present_rect = -1, present_gamma = -1;
    GLuint empty_vao = 0;

    // Bound GL state, so draws skip redundant calls.
    DrawState bound{};
    bool bound_valid = false;
    GLuint bound_target_color = UINT32_MAX, bound_target_depth = UINT32_MAX;

    uint32_t present_count = 0;
    uint32_t draw_count = 0;
    bool immediate = false;
};

namespace {

inline uint32_t mainHeight(const RecompD3dPresenter *p) { return static_cast<uint32_t>(p->config.height * p->scale + 0.5f); }
inline uint32_t presentClientWidth(const RecompD3dPresenter *p, uint32_t height)
{
    return p->widescreen ? (height * 16u + 8u) / 9u : (height * 4u + 1u) / 3u;
}
inline uint32_t mainWidth(const RecompD3dPresenter *p)
{
    return p->scale == 1.0f ? p->config.width : presentClientWidth(p, mainHeight(p));
}
inline bool squeezeWidescreen2d(float left, float right, float width)
{
    return left > width / 8.0f + 1.0f || right < width * 7.0f / 8.0f - 1.0f;
}

GLuint createTexture2D(GLenum internal_format, uint32_t width, uint32_t height, uint32_t levels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, static_cast<GLsizei>(levels), internal_format,
        static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1u));
    return texture;
}

GLuint makeSampler(GLint min_filter, GLint mag_filter, GLint wrap_u, GLint wrap_v)
{
    GLuint sampler = 0;
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min_filter);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, mag_filter);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap_u);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap_v);
    glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, 1000.0f);
    return sampler;
}

GLuint lookupDrawSampler(RecompD3dPresenter *p, uint32_t address_u, uint32_t address_v)
{
    const uint32_t u = address_u <= 5u ? address_u : 1u;
    const uint32_t v = address_v <= 5u ? address_v : 1u;
    if (p->address_samplers[u][v] == 0) {
        p->address_samplers[u][v] = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR,
            glAddressMode(u), glAddressMode(v));
    }
    return p->address_samplers[u][v];
}

void bindTarget(RecompD3dPresenter *p, GLuint color, GLuint depth)
{
    if (p->bound_target_color == color && p->bound_target_depth == depth) return;
    glBindFramebuffer(GL_FRAMEBUFFER, p->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    p->bound_target_color = color;
    p->bound_target_depth = depth;
}

RenderTargetEntry *findRenderTarget(RecompD3dPresenter *p, const RecompD3dTextureDesc &desc)
{
    for (auto &entry : p->render_targets) {
        if (entry.desc.data == desc.data && entry.desc.width == desc.width && entry.desc.height == desc.height) {
            return &entry;
        }
    }
    return nullptr;
}

RecompD3dPresenterError findOrCreateRenderTarget(RecompD3dPresenter *p, const RecompD3dTextureDesc &desc,
                                                 RenderTargetEntry *&entry)
{
    entry = findRenderTarget(p, desc);
    if (entry) return RECOMP_D3D_PRESENTER_OK;
    const uint64_t bytes = static_cast<uint64_t>(desc.width) * desc.height * 4u;
    if (bytes > kTargetByteLimit - p->target_bytes) {
        std::fprintf(stderr, "recomp d3d presenter: target memory budget exhausted\n");
        return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
    }
    GLuint texture = createTexture2D(GL_RGBA8, desc.width, desc.height, 1u);
    if (texture == 0) return RECOMP_D3D_PRESENTER_HOST_FAILURE;
    p->render_targets.push_back({desc, texture, desc.width, desc.height});
    p->target_bytes += bytes;
    entry = &p->render_targets.back();
    return RECOMP_D3D_PRESENTER_OK;
}

GLuint lookupDepthTarget(RecompD3dPresenter *p, const RecompD3dPresenterTarget &target)
{
    if (target.no_depth) return 0;
    if (!target.custom_depth) return target.offscreen ? 0 : p->backbuffer_depth;

    const RecompD3dTextureDesc &desc = target.depth;
    const uint32_t guest_width = target.offscreen ? target.color.width : p->config.width;
    const uint32_t guest_height = target.offscreen ? target.color.height : p->config.height;
    if (desc.data == 0u || desc.width == 0u || desc.height == 0u ||
        desc.width != guest_width || desc.height != guest_height) {
        return 0;
    }
    const bool host_main = !target.offscreen && p->scale != 1.0f;
    for (const auto &entry : p->depth_targets) {
        if (entry.desc.data == desc.data && entry.desc.width == desc.width &&
            entry.desc.height == desc.height && entry.host_main == host_main) {
            return entry.renderbuffer;
        }
    }
    const uint32_t width = host_main ? mainWidth(p) : desc.width;
    const uint32_t height = host_main ? mainHeight(p) : desc.height;
    const uint64_t bytes = host_main ? 0u : static_cast<uint64_t>(width) * height * 4u;
    if (bytes > kTargetByteLimit - p->target_bytes) return 0;
    GLuint rb = 0;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    p->depth_targets.push_back({desc, rb, bytes, host_main});
    p->target_bytes += bytes;
    return rb;
}

void unindexTexture(RecompD3dPresenter *p, uint32_t slot)
{
    const auto range = p->texture_index.equal_range(p->textures[slot].data);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == slot) {
            p->texture_index.erase(it);
            return;
        }
    }
}

void flushSegment(RecompD3dPresenter *p);

// GL orders texture deletion after the draws already submitted, so a cache
// replacement needs no deferred release (unlike SDL_GPU's command buffers),
// but queued draws still name the texture: flush them first.
void retireTexture(RecompD3dPresenter *p, TextureEntry &entry)
{
    if (entry.texture != 0) {
        if (!p->segment.draws.empty()) flushSegment(p);
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
}

void copyMainTo(RecompD3dPresenter *p, GLuint copy)
{
    glCopyImageSubData(p->backbuffer_color, GL_TEXTURE_2D, 0, 0, 0, 0,
        copy, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(mainWidth(p)), static_cast<GLsizei>(mainHeight(p)), 1);
    if (p->copy_mips) {
        glBindTexture(GL_TEXTURE_2D, copy);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

GLuint uploadTexture(RecompD3dPresenter *p, const RecompD3dTextureDesc &desc, const void *bytes,
                     uint32_t byte_count, uint32_t levels, bool compressed, bool linear_bgra)
{
    GLuint texture = 0;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (compressed) {
        const uint32_t block_bytes = desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT1 ? 8u : 16u;
        const GLenum format = desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
            : desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
            : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        texture = createTexture2D(p->s3tc ? format : GL_RGBA8, desc.width, desc.height, levels);
        uint32_t w = desc.width, h = desc.height, offset = 0;
        std::vector<uint8_t> decoded;
        for (uint32_t level = 0; level < levels; ++level) {
            const uint32_t size = ((w + 3u) / 4u) * ((h + 3u) / 4u) * block_bytes;
            if (offset + size > byte_count) break;
            const auto *src = static_cast<const uint8_t *>(bytes) + offset;
            if (p->s3tc) {
                glCompressedTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
                    static_cast<GLsizei>(w), static_cast<GLsizei>(h), format, static_cast<GLsizei>(size), src);
            } else {
                decoded.resize(static_cast<size_t>(w) * h * 4u);
                decodeDxt(src, w, h, desc.format_byte, decoded.data());
                glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
                    static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, decoded.data());
            }
            offset += size;
            w = w > 1u ? w / 2u : 1u;
            h = h > 1u ? h / 2u : 1u;
        }
        return texture;
    }
    if (desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_A8 && !linear_bgra) {
        texture = createTexture2D(GL_R8, desc.width, desc.height, 1u);
        const GLint swizzle[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
            GL_RED, GL_UNSIGNED_BYTE, bytes);
        return texture;
    }
    texture = createTexture2D(GL_RGBA8, desc.width, desc.height, 1u);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
        GL_BGRA, GL_UNSIGNED_BYTE, bytes);
    return texture;
}

GLuint prepareTexture(RecompD3dPresenter *p, const RecompD3dPresenterDrawCommand &draw)
{
    StageTimer timer(2);
    const RecompD3dTextureDesc &desc = draw.texture;
    if (!draw.has_texture || desc.data == 0u || desc.width == 0u || desc.height == 0u) return 0;
    if (draw.texture_is_backbuffer) {
        const bool front = draw.texture_is_front_buffer;
        const GLuint copy = front ? p->frontbuffer_copy : p->backbuffer_copy;
        if (front && p->frontbuffer_used) return copy;
        if (front) p->frontbuffer_used = true;
        copyMainTo(p, copy);
        return copy;
    }
    if (RenderTargetEntry *rt = findRenderTarget(p, desc); rt && rt->texture) return rt->texture;

    const bool palettized = desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_P8;
    const bool linear_bgra = desc.format_byte == 0x12u;
    const bool compressed = desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT1 ||
                            desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT3 ||
                            desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_DXT5;
    const uint32_t levels = compressed && desc.mip_levels ? desc.mip_levels : 1u;
    if (compressed) {
        const uint32_t span = recomp_d3d_texture_compressed_mip_span(&desc);
        if (span == 0u || span > draw.texture_byte_count) return 0;
    }
    if (linear_bgra && (!desc.linear || desc.depth || desc.bits_per_pixel != 32u ||
        desc.pitch < desc.width * 4u || draw.texture_bytes == nullptr ||
        static_cast<uint64_t>(desc.height - 1u) * desc.pitch + desc.width * 4u > draw.texture_byte_count)) {
        return 0;
    }
    if (palettized && (draw.palette_bytes == nullptr || draw.palette_byte_count != kPaletteBytes)) return 0;

    const auto cached = p->texture_index.equal_range(desc.data);
    const bool fingerprinted = !linear_bgra && draw.texture_bytes != nullptr;
    // The fingerprint samples 128 cache-missing words of guest memory; a combat
    // frame binds the same few textures ~1000 times, so check each once a frame.
    uint64_t fingerprint = 0u;
    bool fingerprint_known = false;
    auto currentFingerprint = [&]() {
        if (!fingerprint_known) {
            fingerprint = textureFingerprint(draw.texture_bytes, draw.texture_byte_count);
            fingerprint_known = true;
        }
        return fingerprint;
    };
    for (auto it = cached.first; it != cached.second; ++it) {
        TextureEntry &entry = p->textures[it->second];
        if (entry.format_byte == desc.format_byte && entry.width == desc.width && entry.height == desc.height &&
            entry.mip_levels == levels &&
            (!palettized || std::memcmp(entry.palette, draw.palette_bytes, kPaletteBytes) == 0)) {
            if (fingerprinted && entry.checked_present != p->present_count) {
                if (entry.fingerprint != currentFingerprint()) {
                    unindexTexture(p, it->second);
                    retireTexture(p, entry);
                    entry.used = false;
                    break;
                }
                entry.checked_present = p->present_count;
            }
            if (linear_bgra && draw.texture_bytes != nullptr && draw.texture_byte_count > 0u) {
                // Movie frames: queued draws sampled the old contents.
                if (!p->segment.draws.empty()) flushSegment(p);
                glBindTexture(GL_TEXTURE_2D, entry.texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(desc.pitch / 4u));
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc.width),
                    static_cast<GLsizei>(desc.height), GL_BGRA, GL_UNSIGNED_BYTE, draw.texture_bytes);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            return entry.texture;
        }
    }

    const bool supported = linear_bgra || compressed || palettized ||
        desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_A8R8G8B8 || desc.format_byte == RECOMP_D3D_TEXTURE_FORMAT_A8;
    if (!supported || draw.texture_bytes == nullptr || draw.texture_byte_count == 0u ||
        (desc.linear && !linear_bgra)) {
        return 0;
    }

    std::vector<uint8_t> unswizzled;
    const void *upload = draw.texture_bytes;
    uint32_t upload_size = draw.texture_byte_count;
    if (linear_bgra) {
        unswizzled.resize(static_cast<size_t>(desc.width) * desc.height * 4u);
        const auto *src = static_cast<const uint8_t *>(draw.texture_bytes);
        for (uint32_t y = 0u; y < desc.height; ++y) {
            std::memcpy(unswizzled.data() + y * (desc.width * 4u), src + y * desc.pitch, desc.width * 4u);
        }
        upload = unswizzled.data();
        upload_size = static_cast<uint32_t>(unswizzled.size());
    } else if (palettized) {
        const size_t texels = static_cast<size_t>(desc.width) * desc.height;
        if (texels > draw.texture_byte_count) return 0;
        std::vector<uint8_t> indices(texels);
        if (!recomp_d3d_texture_unswizzle(static_cast<const uint8_t *>(draw.texture_bytes), indices.data(),
                desc.width, desc.height, 1u)) {
            return 0;
        }
        unswizzled.resize(texels * 4u);
        const auto *palette = static_cast<const uint8_t *>(draw.palette_bytes);
        for (size_t i = 0u; i < texels; ++i) std::memcpy(unswizzled.data() + i * 4u, palette + indices[i] * 4u, 4u);
        upload = unswizzled.data();
        upload_size = static_cast<uint32_t>(unswizzled.size());
    } else if (!compressed && !desc.linear) {
        const size_t texel_bytes = desc.bits_per_pixel / 8u;
        unswizzled.resize(static_cast<size_t>(desc.width) * desc.height * texel_bytes);
        if (!recomp_d3d_texture_unswizzle(static_cast<const uint8_t *>(draw.texture_bytes), unswizzled.data(),
                desc.width, desc.height, static_cast<uint32_t>(texel_bytes))) {
            return 0;
        }
        upload = unswizzled.data();
        upload_size = static_cast<uint32_t>(unswizzled.size());
    }

    const GLuint texture = uploadTexture(p, desc, upload, upload_size, levels, compressed, linear_bgra || palettized);
    if (texture == 0) return 0;

    const uint32_t slot = p->next_texture_slot;
    TextureEntry &entry = p->textures[slot];
    if (entry.used) {
        unindexTexture(p, slot);
        retireTexture(p, entry);
    }
    entry.used = true;
    entry.data = desc.data;
    entry.format_byte = desc.format_byte;
    entry.width = desc.width;
    entry.height = desc.height;
    entry.mip_levels = levels;
    entry.fingerprint = fingerprinted ? currentFingerprint() : 0u;
    entry.checked_present = p->present_count;
    entry.texture = texture;
    if (palettized) std::memcpy(entry.palette, draw.palette_bytes, kPaletteBytes);
    p->texture_index.insert({desc.data, slot});
    p->next_texture_slot = (p->next_texture_slot + 1u) % kTextureSlots;
    if (p->texture_count < kTextureSlots) ++p->texture_count;
    return texture;
}

GLuint getProgram(RecompD3dPresenter *p, const RecompD3dVertexLayout &layout, uint32_t fvf,
                  uint32_t program_count, const uint32_t (*program_tokens)[4])
{
    uint64_t key = fvf ^ (static_cast<uint64_t>(program_count) << 32);
    if (program_count && program_tokens) key ^= programHash(program_tokens, program_count);
    auto it = p->programs.find(key);
    if (it != p->programs.end()) return it->second.program;

    Program entry{};
    // The D3D11 presenter accepts vertex programs only with this FVF.
    if (!(program_count && fvf != 0x112u)) {
        const std::string vs_source = buildVertexShader(layout, program_count, program_tokens);
        if (!vs_source.empty()) {
            const GLuint vs = compileShader(GL_VERTEX_SHADER, vs_source, key);
            const GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, buildFragmentShader(layout, program_count), key) : 0;
            if (vs && fs) entry.program = linkProgram(vs, fs, key);
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
        }
    }
    entry.failed = entry.program == 0;
    p->programs.emplace(key, entry);
    return entry.program;
}

GLuint getVao(RecompD3dPresenter *p, const RecompD3dVertexLayout &layout, uint32_t fvf, uint32_t stride)
{
    const uint64_t key = fvf | (static_cast<uint64_t>(stride) << 32);
    auto it = p->vaos.find(key);
    if (it != p->vaos.end()) return it->second;
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    auto attribute = [](GLuint location, GLint size, GLenum type, GLboolean normalized, GLuint offset) {
        glEnableVertexAttribArray(location);
        glVertexAttribFormat(location, size, type, normalized, offset);
        glVertexAttribBinding(location, 0);
    };
    attribute(0, layout.pretransformed ? 4 : 3, GL_FLOAT, GL_FALSE, layout.position_offset);
    if (layout.blend_weight_count != 0u) {
        attribute(1, static_cast<GLint>(layout.blend_weight_count), GL_FLOAT, GL_FALSE, 12u);
    }
    if (layout.normal_offset != RECOMP_D3D_FVF_ABSENT) attribute(2, 3, GL_FLOAT, GL_FALSE, layout.normal_offset);
    if (layout.diffuse_offset != RECOMP_D3D_FVF_ABSENT) attribute(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, layout.diffuse_offset);
    if (layout.texcoord_offset != RECOMP_D3D_FVF_ABSENT) {
        const uint32_t count = layout.texcoord_count == 4u ? 4u : layout.texcoord_count == 2u ? 2u : 1u;
        for (uint32_t i = 0; i < count; ++i) {
            attribute(4u + i, 2, GL_FLOAT, GL_FALSE, layout.texcoord_offset + i * 8u);
        }
    }
    glBindVertexBuffer(0, p->vertex_buffer, 0, static_cast<GLsizei>(stride));
    glBindVertexArray(0);
    p->vaos.emplace(key, vao);
    return vao;
}

void applyCull(RecompD3dCullMode cull, bool flip)
{
    if (cull == RECOMP_D3D_CULL_NONE) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // D3D culls by on-screen winding (y down); GL with clip control keeps the
    // y-up sense, so a screen-clockwise triangle is GL counter-clockwise.
    const bool front_ccw = (cull == RECOMP_D3D_CULL_COUNTER_CLOCKWISE) != flip;
    glFrontFace(front_ccw ? GL_CCW : GL_CW);
}

void applyState(RecompD3dPresenter *p, const DrawState &s)
{
    DrawState &b = p->bound;
    const bool all = !p->bound_valid;
    if (all || s.program != b.program) glUseProgram(s.program);
    if (all || s.vao != b.vao) glBindVertexArray(s.vao);
    if (all || s.cull != b.cull) applyCull(s.cull, p->flip_winding);
    if (all || s.depth_test != b.depth_test || s.depth_write != b.depth_write) {
        // D3D writes depth with the test off; GL needs the test enabled, so
        // a disabled test becomes ALWAYS.
        if (s.depth_test || s.depth_write) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    }
    if (all || s.depth_write != b.depth_write) glDepthMask(s.depth_write ? GL_TRUE : GL_FALSE);
    const GLenum depth_func = s.depth_test ? s.depth_func : GL_ALWAYS;
    const GLenum bound_func = b.depth_test ? b.depth_func : GL_ALWAYS;
    if (all || depth_func != bound_func) glDepthFunc(depth_func);
    if (all || s.stencil != b.stencil) {
        if (s.stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    }
    if (s.stencil) {
        if (all || s.stencil_fail != b.stencil_fail || s.stencil_zfail != b.stencil_zfail || s.stencil_pass != b.stencil_pass) {
            glStencilOp(s.stencil_fail, s.stencil_zfail, s.stencil_pass);
        }
        if (all || s.stencil_write != b.stencil_write) glStencilMask(s.stencil_write);
    }
    if (all || s.blend != b.blend) {
        if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    }
    if (s.blend) {
        if (all || s.src_rgb != b.src_rgb || s.dst_rgb != b.dst_rgb || s.src_a != b.src_a || s.dst_a != b.dst_a) {
            glBlendFuncSeparate(s.src_rgb, s.dst_rgb, s.src_a, s.dst_a);
        }
        if (all || s.blend_op != b.blend_op) glBlendEquation(s.blend_op);
    }
    if (all || s.color_mask != b.color_mask) {
        glColorMask((s.color_mask & 1) != 0, (s.color_mask & 2) != 0, (s.color_mask & 4) != 0, (s.color_mask & 8) != 0);
    }
    // Stencil func and reference are applied per draw (the reference varies).
    b = s;
    p->bound_valid = true;
}

// Reserves and fills a range of a streaming buffer. Uploads go through
// GL_COPY_WRITE_BUFFER, which is not VAO state, so the element-buffer binding
// of whichever VAO is bound stays untouched.
void ringStep(StreamRing &ring)
{
    GLsync &leaving = ring.fences[ring.quarter];
    if (leaving) glDeleteSync(leaving);
    leaving = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    ring.quarter = (ring.quarter + 1u) & 3u;
    GLsync &entering = ring.fences[ring.quarter];
    if (entering) {
        if (glClientWaitSync(entering, 0, 0) == GL_TIMEOUT_EXPIRED) {
            ++ring.waits;
            glClientWaitSync(entering, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
        }
        glDeleteSync(entering);
        entering = nullptr;
    }
}

uint32_t ringUpload(StreamRing &ring, const void *data, uint32_t bytes, uint32_t align)
{
    uint32_t base = (ring.used + align - 1u) & ~(align - 1u);
    if (ring.map == nullptr) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, ring.buffer);
        if (base + bytes > ring.capacity) {
            // Orphan: the driver hands out fresh storage while queued draws keep the old.
            glBufferData(GL_COPY_WRITE_BUFFER, ring.capacity, nullptr, GL_STREAM_DRAW);
            base = 0;
        }
        if (bytes != 0u) glBufferSubData(GL_COPY_WRITE_BUFFER, base, bytes, data);
        ring.used = base + bytes;
        return base;
    }
    const uint32_t quarter_bytes = ring.capacity / 4u;
    if (base + bytes > ring.capacity) {
        while (ring.quarter != 0u) ringStep(ring);
        base = 0;
    }
    if (bytes != 0u) {
        // Keep a segment inside one quarter: a fence placed between its
        // upload and its draws would not cover them.
        const uint32_t last = (base + bytes - 1u) / quarter_bytes;
        if (base / quarter_bytes != last) base = last * quarter_bytes;
        while (ring.quarter != last) ringStep(ring);
        std::memcpy(ring.map + base, data, bytes);
    }
    ring.used = base + bytes;
    return base;
}

void ringCreate(StreamRing &ring, GLuint &buffer, uint32_t capacity)
{
    ring.capacity = capacity;
    glGenBuffers(1, &buffer);
    ring.buffer = buffer;
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    if (glBufferStoragePtr != nullptr && std::getenv("RECOMP_GL_SUBDATA") == nullptr) {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStoragePtr(GL_COPY_WRITE_BUFFER, capacity, nullptr, flags);
        ring.map = static_cast<uint8_t *>(glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, capacity, flags));
        if (ring.map != nullptr) return;
        std::fprintf(stderr, "[gl] persistent map failed 0x%x; using glBufferSubData\n", glGetError());
        glDeleteBuffers(1, &buffer);
        glGenBuffers(1, &buffer);
        ring.buffer = buffer;
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    }
    glBufferData(GL_COPY_WRITE_BUFFER, capacity, nullptr, GL_STREAM_DRAW);
}

void flushSegment(RecompD3dPresenter *p)
{
    DrawSegment &seg = p->segment;
    if (seg.draws.empty()) return;

    const uint32_t v_bytes = static_cast<uint32_t>(seg.vertex_data.size());
    const uint32_t i_bytes = static_cast<uint32_t>(seg.index_data.size());
    const uint32_t u_bytes = static_cast<uint32_t>(seg.uniform_data.size());
    if (v_bytes > kVertexRingBytes / 4u || i_bytes > kIndexRingBytes / 4u || u_bytes > kUniformRingBytes / 4u) {
        std::fprintf(stderr, "recomp d3d presenter: segment too large v=%u i=%u u=%u\n", v_bytes, i_bytes, u_bytes);
        seg.draws.clear();
        seg.vertex_data.clear();
        seg.index_data.clear();
        seg.uniform_data.clear();
        seg.vertex_seen.clear();
        return;
    }

    uint64_t upload_start = doa3_nx_ticks();
    const uint32_t v_base = ringUpload(p->vertex_ring, seg.vertex_data.data(), v_bytes, 256u);
    const uint32_t i_base = ringUpload(p->index_ring, seg.index_data.data(), i_bytes, 256u);
    const uint32_t u_base = ringUpload(p->uniform_ring, seg.uniform_data.data(), u_bytes,
        static_cast<uint32_t>(p->uniform_align));

    doa3_nx_render_stage_add(3, doa3_nx_ticks() - upload_start);
    StageTimer timer(4);
    bindTarget(p, seg.target_color, seg.target_depth);
    glViewport(0, 0, static_cast<GLsizei>(seg.target_w), static_cast<GLsizei>(seg.target_h));
    glDepthRangef(0.0f, 1.0f);

    float last_viewport[6] = {0.0f, 0.0f, seg.target_w, seg.target_h, 0.0f, 1.0f};
    GLuint last_t0 = UINT32_MAX, last_t1 = UINT32_MAX, last_s0 = UINT32_MAX, last_s1 = UINT32_MAX;
    uint64_t last_constant = UINT64_MAX;
    uint32_t last_uniform = UINT32_MAX, last_program_constants = UINT32_MAX;
    uint32_t last_v_offset = UINT32_MAX, last_stride = 0;
    GLuint last_vao = 0;
    int last_stencil_key = -1;

    for (const QueuedDraw &q : seg.draws) {
        if (q.state.program == 0) continue;
        applyState(p, q.state);
        // Vertex and element buffer bindings are VAO state; a VAO last used in
        // an earlier segment still points at that segment's offsets.
        if (q.state.vao != last_vao) {
            last_vao = q.state.vao;
            last_v_offset = UINT32_MAX;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->index_buffer);
        }

        const uint32_t v_offset = v_base + q.vertex_offset;
        if (v_offset != last_v_offset || q.vertex_stride != last_stride) {
            glBindVertexBuffer(0, p->vertex_buffer, v_offset, static_cast<GLsizei>(q.vertex_stride));
            last_v_offset = v_offset;
            last_stride = q.vertex_stride;
        }
        if (q.uniform_offset != last_uniform) {
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, p->uniform_buffer, u_base + q.uniform_offset, q.uniform_size);
            last_uniform = q.uniform_offset;
        }
        if (q.program_offset != kNoProgramConstants && q.program_offset != last_program_constants) {
            glBindBufferRange(GL_UNIFORM_BUFFER, 1, p->uniform_buffer, u_base + q.program_offset,
                kProgramConstantFloats * sizeof(float));
            last_program_constants = q.program_offset;
        }
        const GLuint t0 = q.texture ? q.texture : p->dummy_texture;
        const GLuint t1 = q.mask_texture ? q.mask_texture : p->dummy_texture;
        if (t0 != last_t0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, t0);
            last_t0 = t0;
        }
        if (t1 != last_t1) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, t1);
            last_t1 = t1;
        }
        if (q.sampler0 != last_s0) {
            glBindSampler(0, q.sampler0);
            last_s0 = q.sampler0;
        }
        if (q.sampler1 != last_s1) {
            glBindSampler(1, q.sampler1);
            last_s1 = q.sampler1;
        }
        const uint64_t constant_key = q.constant_color | (uint64_t{q.constant_alpha} << 32);
        if (constant_key != last_constant) {
            const uint32_t color = q.constant_color;
            const float alpha = ((color >> 24u) & 255u) / 255.0f;
            glBlendColor(q.constant_alpha ? alpha : ((color >> 16u) & 255u) / 255.0f,
                q.constant_alpha ? alpha : ((color >> 8u) & 255u) / 255.0f,
                q.constant_alpha ? alpha : (color & 255u) / 255.0f, alpha);
            last_constant = constant_key;
        }
        const float *want = q.viewport[2] > 0.0f ? q.viewport : nullptr;
        float target_viewport[6] = {0.0f, 0.0f, seg.target_w, seg.target_h, 0.0f, 1.0f};
        if (!want) want = target_viewport;
        if (std::memcmp(want, last_viewport, sizeof last_viewport) != 0) {
            glViewport(static_cast<GLint>(want[0]), static_cast<GLint>(want[1]),
                static_cast<GLsizei>(want[2]), static_cast<GLsizei>(want[3]));
            glDepthRangef(want[4], want[5]);
            std::memcpy(last_viewport, want, sizeof last_viewport);
        }
        if (q.state.stencil) {
            const int key = q.stencil_ref | (static_cast<int>(q.state.stencil_func) << 8) | (q.state.stencil_read << 24);
            if (key != last_stencil_key) {
                glStencilFunc(q.state.stencil_func, q.stencil_ref, q.state.stencil_read);
                last_stencil_key = key;
            }
        }
        glDrawElements(q.state.primitive, static_cast<GLsizei>(q.index_count), GL_UNSIGNED_SHORT,
            reinterpret_cast<const void *>(static_cast<uintptr_t>(i_base + q.index_offset)));
    }

    seg.draws.clear();
    seg.vertex_data.clear();
    seg.index_data.clear();
    seg.uniform_data.clear();
    seg.vertex_seen.clear();
}

bool initEgl(RecompD3dPresenter *p)
{
    NWindow *window = nwindowGetDefault();
    std::fprintf(stderr, "[gl] window %ux%u\n", p->window_w, p->window_h);
    nwindowSetDimensions(window, p->window_w, p->window_h);
    std::fprintf(stderr, "[gl] eglGetDisplay\n");
    p->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    std::fprintf(stderr, "[gl] eglInitialize\n");
    if (p->display == EGL_NO_DISPLAY || !eglInitialize(p->display, nullptr, nullptr)) {
        std::fprintf(stderr, "[gl] eglInitialize failed 0x%x\n", eglGetError());
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) return false;
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLConfig config;
    EGLint count = 0;
    std::fprintf(stderr, "[gl] eglChooseConfig\n");
    if (!eglChooseConfig(p->display, config_attributes, &config, 1, &count) || count == 0) {
        std::fprintf(stderr, "[gl] eglChooseConfig failed\n");
        return false;
    }
    std::fprintf(stderr, "[gl] eglCreateWindowSurface\n");
    p->surface = eglCreateWindowSurface(p->display, config, window, nullptr);
    if (p->surface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "[gl] eglCreateWindowSurface failed 0x%x\n", eglGetError());
        return false;
    }
    const EGLint context_attributes[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE
    };
    std::fprintf(stderr, "[gl] eglCreateContext 4.3 core\n");
    p->context = eglCreateContext(p->display, config, EGL_NO_CONTEXT, context_attributes);
    if (p->context == EGL_NO_CONTEXT) {
        std::fprintf(stderr, "[gl] eglCreateContext failed 0x%x\n", eglGetError());
        return false;
    }
    std::fprintf(stderr, "[gl] eglMakeCurrent\n");
    if (!eglMakeCurrent(p->display, p->surface, p->surface, p->context)) {
        std::fprintf(stderr, "[gl] eglMakeCurrent failed 0x%x\n", eglGetError());
        return false;
    }
    std::fprintf(stderr, "[gl] gladLoadGL\n");
    if (!gladLoadGL()) {
        std::fprintf(stderr, "[gl] gladLoadGL failed\n");
        return false;
    }
    glBufferStoragePtr = reinterpret_cast<BufferStorageFn>(eglGetProcAddress("glBufferStorage"));
    glClipControlPtr = reinterpret_cast<ClipControlFn>(eglGetProcAddress("glClipControl"));
    if (!glClipControlPtr) {
        std::fprintf(stderr, "[gl] glClipControl unavailable\n");
        return false;
    }
    GLint extension_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint i = 0; i < extension_count; ++i) {
        const char *name = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (name && (std::strcmp(name, "GL_EXT_texture_compression_s3tc") == 0)) p->s3tc = true;
    }
    std::fprintf(stderr, "[gl] %s / %s / %s s3tc=%d\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
        glGetString(GL_VERSION), p->s3tc ? 1 : 0);
    return true;
}

void destroyEgl(RecompD3dPresenter *p)
{
    if (p->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(p->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (p->context != EGL_NO_CONTEXT) eglDestroyContext(p->display, p->context);
        if (p->surface != EGL_NO_SURFACE) eglDestroySurface(p->display, p->surface);
        eglTerminate(p->display);
    }
    p->display = EGL_NO_DISPLAY;
    p->context = EGL_NO_CONTEXT;
    p->surface = EGL_NO_SURFACE;
}

void presentFrame(RecompD3dPresenter *p)
{
    StageTimer timer(5);  // includes the swap, reported separately too
    // Default framebuffer: lower-left origin, so the present pass flips rows.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p->bound_target_color = p->bound_target_depth = UINT32_MAX;
    glClipControlPtr(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glViewport(0, 0, static_cast<GLsizei>(p->window_w), static_cast<GLsizei>(p->window_h));
    glDepthRangef(0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    p->bound_valid = false;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const float src_aspect = static_cast<float>(presentClientWidth(p, mainHeight(p))) / mainHeight(p);
    const float dst_aspect = static_cast<float>(p->window_w) / p->window_h;
    float w = 1.0f, h = 1.0f;
    if (dst_aspect > src_aspect) w = src_aspect / dst_aspect; else h = dst_aspect / src_aspect;
    glUseProgram(p->present_program);
    glUniform4f(p->present_rect, -w, -h, w, h);
    glUniform1i(p->present_gamma, p->gamma_enabled ? 1 : 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, p->gamma_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p->backbuffer_color);
    glBindSampler(0, p->filter_sampler);
    glBindVertexArray(p->empty_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glClipControlPtr(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

    // GL errors are not checked per call; report the first few per second.
    static uint32_t error_reports = 0;
    if ((p->present_count % 60u) == 0u) error_reports = 0;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        if (error_reports++ < 4u) std::fprintf(stderr, "[gl] error 0x%04x by present %u\n", error, p->present_count);
    }

    {
        StageTimer swap_timer(6);
        eglSwapBuffers(p->display, p->surface);
    }

    if (p->frontbuffer_copy && p->frontbuffer_used) copyMainTo(p, p->frontbuffer_copy);
}

} // namespace

RecompD3dPresenterError gl_backend_create(const RecompD3dPresenterConfig *config, RecompD3dPresenter **out)
{
    if (config == nullptr || out == nullptr || config->width == 0u || config->height == 0u) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    auto *p = new (std::nothrow) RecompD3dPresenter();
    if (p == nullptr) return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
    p->config = *config;

    // RECOMP_D3D_SCALE as on the desktop: render height = 480 * scale. The
    // Switch default 1.5 renders 960x720 for the 720p screen.
    if (const char *scale = std::getenv("RECOMP_D3D_SCALE")) {
        const float value = static_cast<float>(std::atof(scale));
        if (std::isfinite(value) && value >= 1.0f) p->scale = std::min(value, 3.0f);
    }
    const char *widescreen = std::getenv("RECOMP_D3D_WIDESCREEN");
    p->widescreen = widescreen != nullptr && std::strcmp(widescreen, "0") != 0;
    const char *flip = std::getenv("RECOMP_GL_FLIP_WINDING");
    p->flip_winding = flip != nullptr && std::strcmp(flip, "0") != 0;
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        p->window_w = 1920;
        p->window_h = 1080;
    }

    if (!initEgl(p)) {
        destroyEgl(p);
        delete p;
        return RECOMP_D3D_PRESENTER_HOST_FAILURE;
    }
    glClipControlPtr(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &p->uniform_align);
    if (p->uniform_align < 16) p->uniform_align = 16;

    const uint32_t main_w = mainWidth(p), main_h = mainHeight(p);
    glGenFramebuffers(1, &p->framebuffer);
    p->backbuffer_color = createTexture2D(GL_RGBA8, main_w, main_h, 1u);
    glGenRenderbuffers(1, &p->backbuffer_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, p->backbuffer_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, static_cast<GLsizei>(main_w), static_cast<GLsizei>(main_h));
    uint32_t copy_levels = 1u;
    if (p->scale != 1.0f) {
        while ((std::max(main_w, main_h) >> copy_levels) != 0u) ++copy_levels;
    }
    p->copy_mips = copy_levels > 1u;
    p->backbuffer_copy = createTexture2D(GL_RGBA8, main_w, main_h, copy_levels);
    p->frontbuffer_copy = createTexture2D(GL_RGBA8, main_w, main_h, copy_levels);
    p->dummy_texture = createTexture2D(GL_RGBA8, 1u, 1u, 1u);
    const uint32_t white = 0xffffffffu;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &white);

    bindTarget(p, p->backbuffer_color, p->backbuffer_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[gl] main framebuffer incomplete\n");
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    p->filter_sampler = makeSampler(GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    p->program_mask_sampler = makeSampler(GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR, GL_REPEAT, GL_REPEAT);
    p->point_sampler = makeSampler(GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

    // Allocated through GL_COPY_WRITE_BUFFER: core profile has no default VAO
    // to hold an element-buffer binding.
    const auto allocate = [](GLuint &buffer, GLsizeiptr bytes, GLenum usage) {
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, usage);
    };
    ringCreate(p->vertex_ring, p->vertex_buffer, kVertexRingBytes);
    ringCreate(p->index_ring, p->index_buffer, kIndexRingBytes);
    ringCreate(p->uniform_ring, p->uniform_buffer, kUniformRingBytes);
    std::fprintf(stderr, "[gl] streaming buffers: %s\n",
        p->vertex_ring.map ? "persistent mapped" : "glBufferSubData");
    allocate(p->gamma_buffer, sizeof p->gamma_ramp, GL_DYNAMIC_DRAW);

    std::fprintf(stderr, "[gl] compiling the present pass\n");
    const GLuint vs = compileShader(GL_VERTEX_SHADER, kPresentVertex, 0);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, kPresentFragment, 0);
    p->present_program = (vs && fs) ? linkProgram(vs, fs, 0) : 0;
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (p->present_program == 0) {
        destroyEgl(p);
        delete p;
        return RECOMP_D3D_PRESENTER_HOST_FAILURE;
    }
    p->present_rect = glGetUniformLocation(p->present_program, "u_rect");
    p->present_gamma = glGetUniformLocation(p->present_program, "u_gamma");
    glGenVertexArrays(1, &p->empty_vao);

    eglSwapInterval(p->display, 0);  // see gl_backend_set_immediate_present
    std::fprintf(stderr, "[presenter] gl initialized scale=%.2f resolution=%ux%u window=%ux%u widescreen=%d\n",
        p->scale, main_w, main_h, p->window_w, p->window_h, p->widescreen ? 1 : 0);
    *out = p;
    return RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError gl_backend_submit(RecompD3dPresenter *p, const RecompD3dPresenterCommand *command)
{
    if (p == nullptr || command == nullptr) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;

    switch (command->type) {
    case RECOMP_D3D_PRESENTER_COMMAND_CLEAR: {
        StageTimer timer(7);
        const auto &clear = command->data.clear;
        if (clear.clear_depth && (!std::isfinite(clear.z) || clear.z < 0.0f || clear.z > 1.0f)) {
            return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        }
        flushSegment(p);
        GLuint color = p->backbuffer_color;
        uint32_t w = mainWidth(p), h = mainHeight(p);
        if (clear.target.offscreen) {
            RenderTargetEntry *rt = nullptr;
            const auto error = findOrCreateRenderTarget(p, clear.target.color, rt);
            if (error != RECOMP_D3D_PRESENTER_OK) return error;
            color = rt->texture;
            w = clear.target.color.width;
            h = clear.target.color.height;
        }
        const GLuint depth = lookupDepthTarget(p, clear.target);
        bindTarget(p, color, depth);
        glViewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        GLbitfield mask = 0;
        if (clear.clear_color) {
            constexpr float b2f = 1.0f / 255.0f;
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(((clear.color >> 16u) & 0xffu) * b2f, ((clear.color >> 8u) & 0xffu) * b2f,
                (clear.color & 0xffu) * b2f, ((clear.color >> 24u) & 0xffu) * b2f);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (depth != 0 && clear.clear_depth) {
            glDepthMask(GL_TRUE);
            glClearDepthf(clear.z);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (depth != 0 && clear.clear_stencil) {
            glStencilMask(0xff);
            glClearStencil(static_cast<GLint>(clear.stencil & 0xffu));
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (mask) glClear(mask);
        p->bound_valid = false; // masks changed
        return RECOMP_D3D_PRESENTER_OK;
    }

    case RECOMP_D3D_PRESENTER_COMMAND_PRESENT: {
        const auto &present = command->data.present;
        if (present.effective_flags != 5u || present.swap_counter == 0u) {
            return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        }
        flushSegment(p);
        presentFrame(p);
        ++p->present_count;
        return RECOMP_D3D_PRESENTER_OK;
    }

    case RECOMP_D3D_PRESENTER_COMMAND_DRAW: {
        StageTimer timer(1);
        const auto &draw = command->data.draw;
        if (draw.vertex_bytes == nullptr || draw.index_bytes == nullptr || draw.vertex_stride == 0u ||
            draw.vertex_count == 0u || draw.index_count == 0u ||
            static_cast<unsigned>(draw.cull_mode) > RECOMP_D3D_CULL_COUNTER_CLOCKWISE) {
            return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        }
        RecompD3dVertexLayout layout{};
        if (!recomp_d3d_fvf_layout(draw.fvf, &layout) || layout.stride > draw.vertex_stride ||
            (!layout.pretransformed && !draw.has_transform && !draw.program_count) ||
            (draw.directional.enabled && !draw.program_count && draw.directional.count > 8u) ||
            (draw.primitive_type == 2u && (draw.index_count < 2u || draw.index_count % 2u != 0u))) {
            return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        }

        GLuint target_color = p->backbuffer_color;
        uint32_t color_w = mainWidth(p), color_h = mainHeight(p);
        if (draw.target.offscreen) {
            RenderTargetEntry *rt = nullptr;
            const auto error = findOrCreateRenderTarget(p, draw.target.color, rt);
            if (error != RECOMP_D3D_PRESENTER_OK) return error;
            target_color = rt->texture;
            color_w = draw.target.color.width;
            color_h = draw.target.color.height;
        }
        const GLuint target_depth = lookupDepthTarget(p, draw.target);
        const float target_w = static_cast<float>(color_w), target_h = static_cast<float>(color_h);

        if (draw.has_texture && !draw.texture_is_backbuffer) {
            const RenderTargetEntry *sampled = findRenderTarget(p, draw.texture);
            if (sampled && sampled->texture == target_color) return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        }
        if (p->segment.target_color != target_color || p->segment.target_depth != target_depth ||
            (draw.has_texture && draw.texture_is_backbuffer)) {
            flushSegment(p);
        }
        p->segment.target_color = target_color;
        p->segment.target_depth = target_depth;
        p->segment.target_w = target_w;
        p->segment.target_h = target_h;

        const GLuint texture = draw.has_texture ? prepareTexture(p, draw) : 0;
        GLuint mask_texture = 0;
        if (draw.has_alpha_mask) {
            RecompD3dPresenterDrawCommand mask_cmd{};
            mask_cmd.has_texture = true;
            mask_cmd.texture = draw.alpha_mask;
            mask_cmd.texture_bytes = draw.alpha_mask_bytes;
            mask_cmd.texture_byte_count = draw.alpha_mask_byte_count;
            mask_cmd.palette_bytes = draw.alpha_mask_palette;
            mask_cmd.palette_byte_count = draw.alpha_mask_palette_byte_count;
            mask_texture = prepareTexture(p, mask_cmd);
        } else if (draw.has_reflection || draw.program_alpha_mask) {
            RecompD3dPresenterDrawCommand refl_cmd{};
            refl_cmd.has_texture = true;
            refl_cmd.texture = draw.reflection_texture;
            refl_cmd.texture_bytes = draw.reflection_bytes;
            refl_cmd.texture_byte_count = draw.reflection_byte_count;
            mask_texture = prepareTexture(p, refl_cmd);
        }

        DrawState state{};
        state.program = getProgram(p, layout, draw.fvf, draw.program_count, draw.program_count ? draw.program : nullptr);
        if (state.program == 0) return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
        state.vao = getVao(p, layout, draw.fvf, draw.vertex_stride);
        state.primitive = draw.primitive_type == RECOMP_D3D_PT_TRIANGLESTRIP ? GL_TRIANGLE_STRIP
            : draw.primitive_type == 2u ? GL_LINES : GL_TRIANGLES;
        state.cull = draw.cull_mode;
        state.depth_test = draw.depth.depth_test_enable;
        state.depth_write = draw.depth.depth_write_enable;
        state.depth_func = glCompare(draw.depth.depth_func);
        state.stencil = draw.depth.stencil_enable;
        if (state.stencil) {
            state.stencil_func = glCompare(draw.depth.stencil_func);
            state.stencil_read = static_cast<uint8_t>(draw.depth.stencil_read_mask);
            state.stencil_write = static_cast<uint8_t>(draw.depth.stencil_write_mask);
            state.stencil_fail = glStencilOpFor(draw.depth.stencil_fail);
            state.stencil_zfail = glStencilOpFor(draw.depth.stencil_zfail);
            state.stencil_pass = glStencilOpFor(draw.depth.stencil_pass);
        }
        state.blend = draw.blend.blend_enable;
        state.color_mask = draw.blend.color_write_mask;
        if (state.blend) {
            state.src_rgb = glBlendFactor(draw.blend.src_factor, false);
            state.dst_rgb = glBlendFactor(draw.blend.dst_factor, false);
            state.src_a = glBlendFactor(draw.blend.src_factor, true);
            state.dst_a = glBlendFactor(draw.blend.dst_factor, true);
            state.blend_op = glBlendOpFor(draw.blend.op);
        }

        // A segment must fit one quarter of each streaming ring; flush early
        // rather than have flushSegment drop it.
        {
            const uint64_t v_need = uint64_t(draw.vertex_stride) * draw.vertex_count + 16u;
            const uint64_t i_need = uint64_t(draw.primitive_type == RECOMP_D3D_PT_TRIANGLEFAN
                ? draw.triangle_count * 3u : draw.index_count) * 2u + 16u;
            const uint64_t u_need = 2u * (kTransformUniformFloats * sizeof(float) + p->uniform_align) +
                kProgramConstantFloats * sizeof(float) + p->uniform_align;
            const DrawSegment &seg = p->segment;
            if (!seg.draws.empty() && (seg.vertex_data.size() + v_need > kVertexRingBytes / 4u ||
                    seg.index_data.size() + i_need > kIndexRingBytes / 4u ||
                    seg.uniform_data.size() + u_need > kUniformRingBytes / 4u)) {
                flushSegment(p);
            }
        }

        // Vertex and index data.
        auto &vdata = p->segment.vertex_data;
        const uint32_t v_size = draw.vertex_stride * draw.vertex_count;
        uint32_t v_offset;
        auto seen = p->segment.vertex_seen.find(draw.vertex_bytes);
        if (seen != p->segment.vertex_seen.end() && seen->second.first == v_size) {
            v_offset = seen->second.second;
            doa3_nx_counter_add(3, v_size);
        } else {
            v_offset = static_cast<uint32_t>(vdata.size());
            if (v_offset % 16u != 0u) {
                vdata.resize((v_offset + 15u) & ~15u, 0);
                v_offset = static_cast<uint32_t>(vdata.size());
            }
            const auto *v_src = static_cast<const uint8_t *>(draw.vertex_bytes);
            vdata.insert(vdata.end(), v_src, v_src + v_size);
            p->segment.vertex_seen[draw.vertex_bytes] = {v_size, v_offset};
        }

        auto &idata = p->segment.index_data;
        uint32_t i_offset = static_cast<uint32_t>(idata.size());
        if (i_offset % 16u != 0u) {
            idata.resize((i_offset + 15u) & ~15u, 0);
            i_offset = static_cast<uint32_t>(idata.size());
        }
        uint32_t index_count = draw.index_count;
        if (draw.primitive_type == RECOMP_D3D_PT_TRIANGLEFAN) {
            const auto *src = static_cast<const uint16_t *>(draw.index_bytes);
            index_count = draw.triangle_count * 3u;
            for (uint32_t t = 0u; t < draw.triangle_count; ++t) {
                const uint16_t fan[3] = {src[0], src[t + 1u], src[t + 2u]};
                const auto *b = reinterpret_cast<const uint8_t *>(fan);
                idata.insert(idata.end(), b, b + sizeof fan);
            }
        } else {
            const auto *src = static_cast<const uint8_t *>(draw.index_bytes);
            idata.insert(idata.end(), src, src + draw.index_count * sizeof(uint16_t));
        }

        const GLuint s0 = (draw.four_tap_filter || draw.has_alpha_mask ||
                (draw.program_count && (!draw.address_u || !draw.address_v)))
            ? p->filter_sampler : lookupDrawSampler(p, draw.address_u, draw.address_v);
        const GLuint s1 = draw.program_alpha_mask ? p->program_mask_sampler
            : (draw.has_reflection && !draw.has_alpha_mask) ? s0 : p->filter_sampler;

        // Uniforms: the same float layout as the SDL_GPU presenter.
        float constants[kTransformUniformFloats]{};
        std::memcpy(constants, draw.transform, sizeof draw.transform);
        std::memcpy(constants + 16, draw.blend_transforms, sizeof draw.blend_transforms);
        if (layout.pretransformed || draw.program_count) {
            std::memset(constants, 0, sizeof draw.transform);
            const float guest_w = draw.target.offscreen ? target_w : static_cast<float>(p->config.width);
            const float guest_h = draw.target.offscreen ? target_h : static_cast<float>(p->config.height);
            constants[0] = 2.0f / guest_w;
            constants[5] = -2.0f / guest_h;
            constants[10] = 1.0f;
            constants[12] = -1.0f + 0.5f * constants[0];
            constants[13] = 1.0f + 0.5f * constants[5];
            constants[14] = 0.0f;
            constants[15] = 1.0f;
            if (p->widescreen && !draw.program_count && !draw.target.offscreen) {
                float left = INFINITY, right = -INFINITY;
                for (uint32_t i = 0u; i < draw.vertex_count; ++i) {
                    float x;
                    std::memcpy(&x, static_cast<const uint8_t *>(draw.vertex_bytes) + i * draw.vertex_stride, sizeof x);
                    if (!std::isfinite(x)) continue;
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
                const float width = static_cast<float>(p->config.width);
                if (squeezeWidescreen2d(left, right, width)) {
                    constants[12] += constants[0] * 0.125f * width;
                    constants[0] *= 0.75f;
                }
            }
        }
        if (draw.program_count) {
            for (unsigned axis = 0; axis < 2; ++axis) {
                const float scale = draw.program_constants[58][axis];
                if (std::isfinite(scale) && scale != 0.0f) {
                    constants[axis * 5] = 1.0f / scale;
                    constants[12 + axis] = -draw.program_constants[59][axis] / scale;
                }
            }
            const float depth_scale = draw.program_constants[58][2];
            if (std::isfinite(depth_scale) && depth_scale > 0.0f) {
                constants[10] = 1.0f / depth_scale;
                constants[14] = -draw.program_constants[59][2] / depth_scale;
            }
            constants[138] = draw.program_alpha_mask ? 1.0f : 0.0f;
            constants[139] = draw.program_mask_lod_bias;
        }
        if (draw.directional.enabled && !draw.program_count) {
            const auto &light = draw.directional;
            float *dir = constants + kLightingUniformFloats;
            std::memcpy(dir, light.normal_transforms, sizeof light.normal_transforms);
            std::memcpy(dir + 64, light.ambient_emissive, sizeof light.ambient_emissive);
            std::memcpy(dir + 68, light.material_diffuse, sizeof light.material_diffuse);
            std::memcpy(dir + 72, light.directions, sizeof light.directions);
            std::memcpy(dir + 104, light.colors, sizeof light.colors);
            std::memcpy(dir + 136, light.world_transforms, sizeof light.world_transforms);
            std::memcpy(dir + 200, light.positions, sizeof light.positions);
            std::memcpy(dir + 232, light.attenuation, sizeof light.attenuation);
            std::memcpy(dir + 264, light.ambient, sizeof light.ambient);
            std::memcpy(dir + 296, light.specular, sizeof light.specular);
            std::memcpy(dir + 328, light.view, sizeof light.view);
            constants[140] = 1.0f;
            constants[141] = light.normalize ? 1.0f : 0.0f;
            constants[142] = static_cast<float>(light.count);
            constants[143] = light.stage_mode ? 1.0f : 0.0f;
            std::memcpy(constants + 144, light.stage, sizeof light.stage);
        }
        if (draw.fog.mode != 0u && !draw.program_count) {
            float *fog = constants + kHeaderUniformFloats - 8u;
            fog[0] = static_cast<float>(draw.fog.mode);
            fog[1] = draw.fog.mode == 1u ? draw.fog.end : draw.fog.density;
            fog[2] = draw.fog.mode == 1u ? 1.0f / (draw.fog.end - draw.fog.start) : 0.0f;
            std::memcpy(fog + 4, draw.fog.color, sizeof draw.fog.color);
        }
        constants[64] = texture != 0 ? 1.0f : 0.0f;
        constants[65] = draw.depth.alpha_test_enable ? 1.0f : 0.0f;
        constants[66] = static_cast<float>(draw.depth.alpha_func);
        constants[67] = static_cast<float>(draw.depth.alpha_ref);
        constants[68] = draw.blend_weight_count != 0u ? 1.0f : 0.0f;
        constants[69] = draw.use_texture_factor ? 1.0f : (draw.modulate_texture_factor ? 2.0f : 0.0f);
        constants[70] = draw.material_alpha_mode == RECOMP_D3D_MATERIAL_ALPHA_NONE ? 1.0f : draw.material_alpha;
        constants[71] = draw.material_alpha_mode == RECOMP_D3D_MATERIAL_ALPHA_SELECT_DIFFUSE ? 1.0f : 0.0f;
        constants[72] = ((draw.texture_factor >> 16u) & 0xffu) / 255.0f;
        constants[73] = ((draw.texture_factor >> 8u) & 0xffu) / 255.0f;
        constants[74] = (draw.texture_factor & 0xffu) / 255.0f;
        constants[75] = ((draw.texture_factor >> 24u) & 0xffu) / 255.0f;
        constants[76] = draw.zero_diffuse_rgb ? 1.0f : 0.0f;
        constants[77] = draw.texture.format_byte == RECOMP_D3D_TEXTURE_FORMAT_A8 ? 1.0f : 0.0f;
        constants[78] = (draw.alpha_mask.linear && draw.alpha_mask.width) ? 1.0f / draw.alpha_mask.width : 1.0f;
        constants[79] = (draw.alpha_mask.linear && draw.alpha_mask.height) ? 1.0f / draw.alpha_mask.height : 1.0f;
        constants[80] = (draw.texture.linear && draw.texture.width) ? 1.0f / draw.texture.width : 1.0f;
        constants[81] = (draw.texture.linear && draw.texture.height) ? 1.0f / draw.texture.height : 1.0f;
        constants[82] = draw.four_tap_filter ? 1.0f : 0.0f;
        constants[83] = draw.has_alpha_mask ? 1.0f : 0.0f;
        if (draw.has_reflection) {
            std::memcpy(constants + 84, draw.reflection_world_view, 64u);
            std::memcpy(constants + 100, draw.reflection_normal, 64u);
            std::memcpy(constants + 116, draw.reflection_transform, 64u);
            std::memcpy(constants + 132, draw.reflection_diffuse, 16u);
            constants[136] = draw.reflection_mesh_uv ? 2.0f : 1.0f;
            constants[137] = draw.reflection_normalize ? 1.0f : 0.0f;
        }

        // The whole block is bound: unlit draws leave the lighting tail zero.
        const uint32_t uniform_floats = (draw.directional.enabled && !draw.program_count)
            ? kTransformUniformFloats : kHeaderUniformFloats;
        const uint32_t uniform_bytes = kTransformUniformFloats * sizeof(float);
        const uint32_t align = static_cast<uint32_t>(p->uniform_align);
        auto &udata = p->segment.uniform_data;
        uint32_t uniform_offset = 0;
        bool reused = false;
        if (!p->segment.draws.empty()) {
            const auto &prev = p->segment.draws.back();
            if (std::memcmp(constants, &udata[prev.uniform_offset], uniform_floats * sizeof(float)) == 0 &&
                (uniform_floats == kTransformUniformFloats ||
                 std::all_of(reinterpret_cast<const float *>(&udata[prev.uniform_offset]) + kHeaderUniformFloats,
                     reinterpret_cast<const float *>(&udata[prev.uniform_offset]) + kTransformUniformFloats,
                     [](float f) { return f == 0.0f; }))) {
                uniform_offset = prev.uniform_offset;
                reused = true;
            }
        }
        if (!reused) {
            uniform_offset = (static_cast<uint32_t>(udata.size()) + align - 1u) & ~(align - 1u);
            udata.resize(uniform_offset + uniform_bytes);
            std::memcpy(&udata[uniform_offset], constants, uniform_bytes);
        }
        uint32_t program_offset = kNoProgramConstants;
        if (draw.program_count) {
            program_offset = (static_cast<uint32_t>(udata.size()) + align - 1u) & ~(align - 1u);
            udata.resize(program_offset + kProgramConstantFloats * sizeof(float));
            std::memcpy(&udata[program_offset], &draw.program_constants[0][0], kProgramConstantFloats * sizeof(float));
        }

        QueuedDraw q{};
        q.state = state;
        q.vertex_offset = v_offset;
        q.vertex_stride = draw.vertex_stride;
        q.index_offset = i_offset;
        q.index_count = index_count;
        q.texture = texture;
        q.mask_texture = mask_texture;
        q.sampler0 = s0;
        q.sampler1 = s1;
        q.constant_color = draw.blend.constant_color;
        const auto alpha_factor = [](RecompD3dBlendFactor factor) {
            return factor == RECOMP_D3D_BLEND_CONSTANT_ALPHA || factor == RECOMP_D3D_BLEND_INV_CONSTANT_ALPHA;
        };
        q.constant_alpha = alpha_factor(draw.blend.src_factor) || alpha_factor(draw.blend.dst_factor);
        if (draw.has_transform && !draw.program_count && draw.viewport[2] > 0.0f) {
            const float guest_w = draw.target.color.width ? static_cast<float>(draw.target.color.width)
                : static_cast<float>(p->config.width);
            const float guest_h = draw.target.color.height ? static_cast<float>(draw.target.color.height)
                : static_cast<float>(p->config.height);
            const float sx = draw.target.offscreen ? 1.0f : target_w / guest_w;
            const float sy = draw.target.offscreen ? 1.0f : target_h / guest_h;
            q.viewport[0] = draw.viewport[0] * sx;
            q.viewport[1] = draw.viewport[1] * sy;
            q.viewport[2] = draw.viewport[2] * sx;
            q.viewport[3] = draw.viewport[3] * sy;
            q.viewport[4] = draw.viewport[4];
            q.viewport[5] = draw.viewport[5];
        }
        q.stencil_ref = static_cast<uint8_t>(draw.depth.stencil_ref);
        q.uniform_offset = uniform_offset;
        q.uniform_size = uniform_bytes;
        q.program_offset = program_offset;
        p->segment.draws.push_back(q);
        ++p->draw_count;
        return RECOMP_D3D_PRESENTER_OK;
    }

    case RECOMP_D3D_PRESENTER_COMMAND_GAMMA: {
        const auto &ramp = command->data.gamma;
        bool enabled = false;
        for (unsigned i = 0u; i < 256u; ++i) {
            for (unsigned channel = 0u; channel < 3u; ++channel) {
                p->gamma_ramp[i][channel] = ramp[channel][i] / 255.0f;
                enabled |= ramp[channel][i] != i;
            }
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, p->gamma_buffer);
        glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof p->gamma_ramp, p->gamma_ramp);
        p->gamma_enabled = enabled;
        return RECOMP_D3D_PRESENTER_OK;
    }
    }
    return RECOMP_D3D_PRESENTER_UNSUPPORTED_COMMAND;
}

RecompD3dPresenterError gl_backend_release_memory(RecompD3dPresenter *p, uint32_t base, uint32_t size)
{
    if (p == nullptr) return RECOMP_D3D_PRESENTER_NOT_INITIALIZED;
    flushSegment(p);
    if (base >= 0x80000000u && base < 0x84000000u) base -= 0x80000000u;
    if (size == 0u || static_cast<uint64_t>(base) + size > UINT64_C(0x100000000)) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }
    const auto released = [&](uint32_t data) {
        if (data >= 0x80000000u && data < 0x84000000u) data -= 0x80000000u;
        return static_cast<uint64_t>(data) >= base && static_cast<uint64_t>(data) < static_cast<uint64_t>(base) + size;
    };
    for (size_t i = 0u; i < p->render_targets.size();) {
        RenderTargetEntry &entry = p->render_targets[i];
        if (!released(entry.desc.data)) { ++i; continue; }
        if (entry.texture) glDeleteTextures(1, &entry.texture);
        if (p->bound_target_color == entry.texture) p->bound_target_color = UINT32_MAX;
        p->target_bytes -= static_cast<uint64_t>(entry.width) * entry.height * 4u;
        entry = p->render_targets.back();
        p->render_targets.pop_back();
    }
    for (size_t i = 0u; i < p->depth_targets.size();) {
        DepthTargetEntry &entry = p->depth_targets[i];
        if (!released(entry.desc.data)) { ++i; continue; }
        if (entry.renderbuffer) glDeleteRenderbuffers(1, &entry.renderbuffer);
        if (p->bound_target_depth == entry.renderbuffer) p->bound_target_depth = UINT32_MAX;
        p->target_bytes -= entry.bytes;
        entry = p->depth_targets.back();
        p->depth_targets.pop_back();
    }
    for (uint32_t i = 0u; i < p->texture_count; ++i) {
        TextureEntry &entry = p->textures[i];
        if (entry.used && released(entry.data)) {
            unindexTexture(p, i);
            if (entry.texture) glDeleteTextures(1, &entry.texture);
            entry.texture = 0;
            entry.used = false;
        }
    }
    return RECOMP_D3D_PRESENTER_OK;
}

RecompD3dPresenterError gl_backend_destroy(RecompD3dPresenter **presenter)
{
    if (presenter == nullptr || *presenter == nullptr) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    RecompD3dPresenter *p = *presenter;
    flushSegment(p);
    glFinish();
    for (StreamRing *ring : {&p->vertex_ring, &p->index_ring, &p->uniform_ring}) {
        if (ring->map) {
            glBindBuffer(GL_COPY_WRITE_BUFFER, ring->buffer);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            ring->map = nullptr;
        }
    }
    destroyEgl(p); // the context owns every GL object
    delete p;
    *presenter = nullptr;
    return RECOMP_D3D_PRESENTER_OK;
}

void gl_backend_set_immediate_present(RecompD3dPresenter *p, bool enabled)
{
    if (p == nullptr || p->display == EGL_NO_DISPLAY) return;
    p->immediate = enabled;
    // The guest already paces itself to the 60 Hz vblank. A second vsync here
    // makes the render thread wait a vblank of its own, so a frame that runs a
    // little long loses a whole extra one. The Switch compositor shows the
    // latest finished buffer, so interval 0 does not tear. RECOMP_GL_VSYNC=1
    // restores the old behaviour.
    static const bool vsync = [] {
        const char *value = std::getenv("RECOMP_GL_VSYNC");
        return value != nullptr && value[0] == '1';
    }();
    eglSwapInterval(p->display, (enabled || !vsync) ? 0 : 1);
}
