#include "d3d_draw_model.h"

#include <float.h>
#include <string.h>
#include <math.h>

bool recomp_d3d_normal_transform(const float world[16], float normal[16])
{
    float n[16] = {0};
    if (world == NULL || normal == NULL) return false;
    for (uint32_t i = 0; i < 16; ++i) if (!isfinite(world[i])) return false;
    for (uint32_t row = 0; row < 3; ++row) {
        uint32_t a = (row + 1) % 3, b = (row + 2) % 3;
        for (uint32_t col = 0; col < 3; ++col) {
            uint32_t c = (col + 1) % 3, d = (col + 2) % 3;
            n[row*4+col] = world[a*4+c]*world[b*4+d] - world[a*4+d]*world[b*4+c];
        }
    }
    float determinant = world[0]*n[0] + world[1]*n[1] + world[2]*n[2];
    if (!isfinite(determinant) || determinant == 0) return false;
    for (uint32_t i = 0; i < 16; ++i) {
        n[i] /= determinant;
        if (!isfinite(n[i])) return false;
    }
    memcpy(normal, n, sizeof n);
    return true;
}


enum {
    /* Fixed-function FVF component bits this seam decodes. */
    RECOMP_D3D_FVF_POSITION_MASK = 0x00fu,
    RECOMP_D3D_FVF_XYZ = 0x002u,
    RECOMP_D3D_FVF_XYZRHW = 0x004u,
    RECOMP_D3D_FVF_XYZB1 = 0x006u,
    RECOMP_D3D_FVF_XYZB2 = 0x008u,
    RECOMP_D3D_FVF_XYZB3 = 0x00au,
    RECOMP_D3D_FVF_INDEXED_WEIGHTS = 0x9000u,
    RECOMP_D3D_FVF_NORMAL = 0x010u,
    RECOMP_D3D_FVF_DIFFUSE = 0x040u,
    RECOMP_D3D_FVF_SPECULAR = 0x080u,
    RECOMP_D3D_FVF_TEXCOUNT_SHIFT = 8u,
    RECOMP_D3D_FVF_TEXCOUNT_MASK = 0x00fu,
};

void recomp_d3d_draw_reset(RecompD3dDrawState *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof *state);
    }
}

uint32_t recomp_d3d_fvf_stride(uint32_t fvf)
{
    uint32_t position = fvf & RECOMP_D3D_FVF_POSITION_MASK;
    uint32_t blend_weight_count;
    uint32_t texture_count =
        (fvf >> RECOMP_D3D_FVF_TEXCOUNT_SHIFT) & RECOMP_D3D_FVF_TEXCOUNT_MASK;
    uint32_t size;

    /* XYZRHW bypasses transformation and cannot carry a normal. */
    if ((fvf & RECOMP_D3D_FVF_INDEXED_WEIGHTS) != 0u ||
        (position != RECOMP_D3D_FVF_XYZ &&
         position != RECOMP_D3D_FVF_XYZRHW &&
         position != RECOMP_D3D_FVF_XYZB1 &&
         position != RECOMP_D3D_FVF_XYZB2 &&
         position != RECOMP_D3D_FVF_XYZB3)) {
        return 0u;
    }
    if (position == RECOMP_D3D_FVF_XYZRHW &&
        (fvf & RECOMP_D3D_FVF_NORMAL) != 0u) {
        return 0u;
    }
    if (texture_count > 4u) {
        return 0u;
    }

    blend_weight_count = position == RECOMP_D3D_FVF_XYZ ||
        position == RECOMP_D3D_FVF_XYZRHW
        ? 0u : (position - 4u) / 2u;
    size = position == RECOMP_D3D_FVF_XYZRHW
        ? 16u : 12u + blend_weight_count * 4u;
    if ((fvf & RECOMP_D3D_FVF_NORMAL) != 0u) {
        size += 12u;
    }
    if ((fvf & RECOMP_D3D_FVF_DIFFUSE) != 0u) {
        size += 4u;
    }
    if ((fvf & RECOMP_D3D_FVF_SPECULAR) != 0u) {
        size += 4u;
    }
    return size + texture_count * 8u;
}

uint32_t recomp_d3d_fvf_for_stream(uint32_t fvf, uint32_t vertex_stride)
{
    /* Retail meshes can declare more texture-coordinate sets than their
       stream stores (swimsuit skins declare TEX3 in two-set vertices). */
    while (recomp_d3d_fvf_stride(fvf) > vertex_stride &&
           ((fvf >> RECOMP_D3D_FVF_TEXCOUNT_SHIFT) & RECOMP_D3D_FVF_TEXCOUNT_MASK) != 0u) {
        fvf -= 1u << RECOMP_D3D_FVF_TEXCOUNT_SHIFT;
    }
    return fvf;
}

bool recomp_d3d_fvf_layout(uint32_t fvf, RecompD3dVertexLayout *layout)
{
    RecompD3dVertexLayout decoded;
    uint32_t offset;

    if (layout == NULL || recomp_d3d_fvf_stride(fvf) == 0u) {
        return false;
    }

    decoded.texcoord_count =
        (fvf >> RECOMP_D3D_FVF_TEXCOUNT_SHIFT) & RECOMP_D3D_FVF_TEXCOUNT_MASK;
    decoded.position_offset = 0u;
    decoded.pretransformed =
        (fvf & RECOMP_D3D_FVF_POSITION_MASK) == RECOMP_D3D_FVF_XYZRHW;
    decoded.blend_weight_count =
        decoded.pretransformed ||
        (fvf & RECOMP_D3D_FVF_POSITION_MASK) == RECOMP_D3D_FVF_XYZ
            ? 0u : ((fvf & RECOMP_D3D_FVF_POSITION_MASK) - 4u) / 2u;
    offset = decoded.pretransformed ? 16u : 12u + decoded.blend_weight_count * 4u;
    if ((fvf & RECOMP_D3D_FVF_NORMAL) != 0u) {
        decoded.normal_offset = offset;
        offset += 12u;
    } else {
        decoded.normal_offset = RECOMP_D3D_FVF_ABSENT;
    }
    if ((fvf & RECOMP_D3D_FVF_DIFFUSE) != 0u) {
        decoded.diffuse_offset = offset;
        offset += 4u;
    } else {
        decoded.diffuse_offset = RECOMP_D3D_FVF_ABSENT;
    }
    if ((fvf & RECOMP_D3D_FVF_SPECULAR) != 0u) {
        decoded.specular_offset = offset;
        offset += 4u;
    } else {
        decoded.specular_offset = RECOMP_D3D_FVF_ABSENT;
    }
    decoded.texcoord_offset = decoded.texcoord_count != 0u
        ? offset
        : RECOMP_D3D_FVF_ABSENT;
    decoded.stride = offset + decoded.texcoord_count * 8u;

    *layout = decoded;
    return true;
}

uint32_t recomp_d3d_point_quads(
    const RecompD3dPointState *state,
    const uint8_t *vertices,
    uint32_t stride,
    const RecompD3dVertexLayout *layout,
    const uint16_t *indices,
    uint32_t count,
    uint32_t first_vertex,
    const float world_view[16],
    const float projection[16],
    float viewport_width,
    float viewport_height,
    uint8_t *out_vertices,
    uint16_t *out_indices)
{
    static const float corner[4][2] = {{-1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}};
    static const uint16_t quad[6] = {0u, 1u, 2u, 2u, 1u, 3u};
    if (layout->pretransformed || layout->blend_weight_count || count > 16384u) return 0u;
    const bool diffuse = layout->diffuse_offset != RECOMP_D3D_FVF_ABSENT;
    const bool texcoord = state->sprite || layout->texcoord_offset != RECOMP_D3D_FVF_ABSENT;
    const uint32_t fvf = RECOMP_D3D_FVF_XYZ | (diffuse ? RECOMP_D3D_FVF_DIFFUSE : 0u) |
        (texcoord ? 1u << RECOMP_D3D_FVF_TEXCOUNT_SHIFT : 0u);
    const uint32_t out_stride = recomp_d3d_fvf_stride(fvf);
    /* shortcut: D3D8's size formula and a 64-pixel cap stand in for NV2A's
       fixed-point point parameters; port 0x001B6120's encoding if sizes differ. */
    const float largest = fminf(state->size_max, 64.0f);
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t *in = vertices + (size_t)(indices[i] - first_vertex) * stride;
        float p[3], e[3];
        memcpy(p, in, sizeof p);
        for (uint32_t c = 0u; c < 3u; ++c)
            e[c] = p[0] * world_view[c] + p[1] * world_view[4u + c] + p[2] * world_view[8u + c] + world_view[12u + c];
        const float w = e[0] * projection[3] + e[1] * projection[7] + e[2] * projection[11] + projection[15];
        float size = state->size;
        if (state->scale_enable) {
            const float d = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            const float q = state->scale[0] + state->scale[1] * d + state->scale[2] * d * d;
            size = q > 0.0f ? viewport_height * size / sqrtf(q) : largest;
        }
        size = fminf(fmaxf(size, state->size_min), largest);
        /* Half the quad in view units: size pixels at this clip w. */
        float hx = size * w / (viewport_width * fabsf(projection[0]));
        float hy = size * w / (viewport_height * fabsf(projection[5]));
        if (!(w > 0.0f) || !isfinite(hx) || !isfinite(hy)) hx = hy = 0.0f;
        for (uint32_t k = 0u; k < 4u; ++k) {
            uint8_t *out = out_vertices + (size_t)(4u * i + k) * out_stride;
            const float position[3] = {e[0] + corner[k][0] * hx, e[1] + corner[k][1] * hy, e[2]};
            memcpy(out, position, sizeof position);
            out += sizeof position;
            if (diffuse) {
                memcpy(out, in + layout->diffuse_offset, 4u);
                out += 4u;
            }
            if (state->sprite) {
                const float uv[2] = {0.5f + 0.5f * corner[k][0], 0.5f - 0.5f * corner[k][1]};
                memcpy(out, uv, sizeof uv);
            } else if (texcoord) {
                memcpy(out, in + layout->texcoord_offset, 8u);
            }
        }
        for (uint32_t k = 0u; k < 6u; ++k) out_indices[6u * i + k] = (uint16_t)(4u * i + quad[k]);
    }
    return fvf;
}

uint32_t recomp_d3d_draw_vertex_bytes(uint32_t stride, uint32_t max_index)
{
    uint64_t bytes;

    if (stride == 0u) {
        return 0u;
    }
    bytes = (uint64_t)stride * ((uint64_t)max_index + 1u);
    return bytes > 0xffffffffu ? 0u : (uint32_t)bytes;
}

uint32_t recomp_d3d_draw_triangle_count(
    uint32_t primitive_type,
    uint32_t index_count)
{
    switch (primitive_type) {
    case RECOMP_D3D_PT_TRIANGLELIST:
        return index_count / 3u;
    case RECOMP_D3D_PT_TRIANGLESTRIP:
    case RECOMP_D3D_PT_TRIANGLEFAN:
        return index_count < 3u ? 0u : index_count - 2u;
    default:
        return 0u;
    }
}

RecompD3dDrawResult recomp_d3d_draw_indexed(
    const RecompD3dDrawState *state,
    uint32_t primitive_type,
    uint32_t index_count,
    uint32_t index_data)
{
    RecompD3dDrawResult result = {0};
    uint32_t triangles;

    if (state == NULL || index_data == 0u) {
        result.error = RECOMP_D3D_DRAW_INVALID_ARGUMENT;
        return result;
    }
    if (state->stream0.vertex_data == 0u || state->stream0.stride == 0u) {
        result.error = RECOMP_D3D_DRAW_NO_STREAM;
        return result;
    }
    if (primitive_type != RECOMP_D3D_PT_TRIANGLELIST &&
        primitive_type != RECOMP_D3D_PT_TRIANGLESTRIP &&
        primitive_type != RECOMP_D3D_PT_TRIANGLEFAN) {
        result.error = RECOMP_D3D_DRAW_UNSUPPORTED_PRIMITIVE;
        return result;
    }

    triangles = recomp_d3d_draw_triangle_count(primitive_type, index_count);
    if (triangles == 0u) {
        result.error = RECOMP_D3D_DRAW_EMPTY;
        return result;
    }

    result.error = RECOMP_D3D_DRAW_OK;
    result.plan.primitive_type = primitive_type;
    result.plan.index_count = index_count;
    result.plan.triangle_count = triangles;
    result.plan.index_data = index_data;
    /* Xbox index buffers are 16-bit. */
    result.plan.index_bytes = index_count * 2u;
    result.plan.vertex_data = state->stream0.vertex_data;
    result.plan.vertex_stride = state->stream0.stride;
    result.plan.fvf = state->fvf;
    return result;
}

void recomp_d3d_quad_texcoord_clamp(
    const void *vertices,
    uint32_t stride,
    uint32_t vertex_count,
    uint32_t texcoord_offset,
    float bounds[4])
{
    /* Per vertex: screen x, y, then texcoord u, v. */
    float v[4][4], lo[4], hi[4], rhw[4];
    bounds[0] = bounds[1] = -FLT_MAX;
    bounds[2] = bounds[3] = FLT_MAX;
    if (vertices == NULL || vertex_count != 4u || texcoord_offset == RECOMP_D3D_FVF_ABSENT) return;
    for (uint32_t i = 0u; i < 4u; ++i) {
        const uint8_t *vertex = (const uint8_t *)vertices + i * stride;
        memcpy(v[i], vertex, 2u * sizeof(float));
        memcpy(&rhw[i], vertex + 3u * sizeof(float), sizeof(float));
        memcpy(v[i] + 2, vertex + texcoord_offset, 2u * sizeof(float));
    }
    /* Unequal RHW interpolates perspective-correct, so the inset is not affine. */
    if (rhw[1] != rhw[0] || rhw[2] != rhw[0] || rhw[3] != rhw[0]) return;
    for (uint32_t c = 0u; c < 4u; ++c) {
        lo[c] = hi[c] = v[0][c];
        for (uint32_t i = 1u; i < 4u; ++i) {
            lo[c] = fminf(lo[c], v[i][c]);
            hi[c] = fmaxf(hi[c], v[i][c]);
        }
        if (!isfinite(lo[c]) || !isfinite(hi[c])) return;
    }
    if (!(hi[0] > lo[0]) || !(hi[1] > lo[1])) return;
    /* Each corner's texcoord must sit on the matching (or mirrored) edge. */
    for (uint32_t axis = 0u; axis < 2u; ++axis) {
        int flip = -1;
        for (uint32_t i = 0u; i < 4u; ++i) {
            const float p = v[i][axis], t = v[i][axis + 2u];
            if ((p != lo[axis] && p != hi[axis]) ||
                (t != lo[axis + 2u] && t != hi[axis + 2u])) return;
            if (lo[axis + 2u] == hi[axis + 2u]) continue;
            const int f = (p == hi[axis]) != (t == hi[axis + 2u]);
            if (flip < 0) flip = f;
            else if (flip != f) return;
        }
    }
    for (uint32_t axis = 0u; axis < 2u; ++axis) {
        const float inset = 0.5f * (hi[axis + 2u] - lo[axis + 2u]) / (hi[axis] - lo[axis]);
        bounds[axis] = lo[axis + 2u] + inset;
        bounds[axis + 2u] = hi[axis + 2u] - inset;
    }
}
