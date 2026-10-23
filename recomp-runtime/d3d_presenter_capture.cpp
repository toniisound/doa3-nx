#include "d3d_presenter_capture.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>

namespace {

using Draw = RecompD3dPresenterDrawCommand;
constexpr const void *Draw::*pointer_fields[] = {
    &Draw::vertex_bytes, &Draw::index_bytes, &Draw::texture_bytes,
    &Draw::palette_bytes, &Draw::alpha_mask_bytes,
    &Draw::alpha_mask_palette, &Draw::reflection_bytes,
};
// No readable guest span can exceed the runner's entire 64 MiB RAM region.
constexpr uint64_t max_span_bytes = 64u * 1024u * 1024u;
constexpr size_t null_offset = SIZE_MAX;
constexpr size_t borrowed_offset = SIZE_MAX - 1u;
// A removed span slot: lookups probe past it, so chains stay intact.
const void *const span_tombstone = reinterpret_cast<const void *>(uintptr_t{1});

size_t spanHash(const void *source)
{
    uint64_t key = reinterpret_cast<uintptr_t>(source);
    key ^= key >> 29u;
    key *= 0xbf58476d1ce4e5b9ull;
    key ^= key >> 32u;
    return static_cast<size_t>(key);
}

/* The backend reads swizzled, compressed and P8 texture bytes only on a cache
   miss, keyed by address and shape, so those stay borrowed from guest RAM;
   linear BGRA (0x12) is re-uploaded on every hit and is copied.
   ponytail: a miss after the guest frees and refills an address within the
   queued ticks reads the newer bytes; copy on first use per key if that shows. */
bool borrowed(const Draw &draw, size_t field)
{
    const RecompD3dTextureDesc *desc = field == 2u ? &draw.texture
        : field == 4u ? &draw.alpha_mask : field == 6u ? &draw.reflection_texture : nullptr;
    return desc != nullptr && desc->format_byte != 0x12u;
}

void copyCommand(RecompD3dPresenterCommand &to, const RecompD3dPresenterCommand &from)
{
    if (from.type != RECOMP_D3D_PRESENTER_COMMAND_DRAW || from.data.draw.program_count != 0u) {
        to = from;
        return;
    }
    // Fixed-function draws never read the 5 KiB program arrays.
    constexpr size_t head = offsetof(Draw, program);
    constexpr size_t tail = offsetof(Draw, program_alpha_mask);
    to.type = from.type;
    std::memcpy(&to.data.draw, &from.data.draw, head);
    std::memcpy(reinterpret_cast<char *>(&to.data.draw) + tail,
        reinterpret_cast<const char *>(&from.data.draw) + tail, sizeof(Draw) - tail);
}

} // namespace

RecompD3dPresenterError D3dCapturePacket::add(
    const RecompD3dPresenterCommand &command)
{
    if (sealed_ || static_cast<unsigned>(command.type) >
        RECOMP_D3D_PRESENTER_COMMAND_GAMMA) {
        return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    }

    uint64_t sizes[7]{};
    if (command.type == RECOMP_D3D_PRESENTER_COMMAND_DRAW) {
        const auto &draw = command.data.draw;
        sizes[0] = uint64_t(draw.vertex_count) * draw.vertex_stride;
        sizes[1] = uint64_t(draw.index_count) * 2u;
        sizes[2] = draw.texture_byte_count;
        sizes[3] = draw.palette_byte_count;
        sizes[4] = draw.alpha_mask_byte_count;
        sizes[5] = draw.alpha_mask_palette_byte_count;
        sizes[6] = draw.reflection_byte_count;
        for (uint64_t size : sizes) {
            if (size > max_span_bytes) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
        }
    }

    const size_t old_payload_size = payload_.size();
    const size_t old_command_count = commands_.size();
    const void *inserted_sources[7]{};
    size_t inserted_count = 0u;
    try {
        CapturedCommand &captured = commands_.emplace_back();
        copyCommand(captured.value, command);
        if (command.type == RECOMP_D3D_PRESENTER_COMMAND_DRAW) {
            for (size_t i = 0u; i < 7u; ++i) {
                const void *source = command.data.draw.*pointer_fields[i];
                if (source != nullptr && borrowed(command.data.draw, i)) {
                    captured.offsets[i] = borrowed_offset;
                    continue;
                }
                captured.value.data.draw.*pointer_fields[i] = nullptr;
                captured.offsets[i] = null_offset;
                if (source == nullptr) continue;

                const size_t size = static_cast<size_t>(sizes[i]);
                captured.offsets[i] = findSpan(source, size);
                if (captured.offsets[i] != null_offset) continue;

                const size_t offset = payload_.size();
                // Preserve non-null empty spans without reading their source.
                const size_t words = size == 0u ? 1u : (size + 7u) / 8u;
                if (words > payload_.max_size() - offset) throw std::length_error("capture payload");
                payload_.resize(offset + words);
                // The allocator leaves new words uninitialized; define the tail before it can be relocated.
                payload_[offset + words - 1u] = 0u;
                if (size != 0u) std::memcpy(payload_.data() + offset, source, size);
                insertSpan(source, size, offset);
                inserted_sources[inserted_count++] = source;
                captured.offsets[i] = offset;
            }
        }
        entries_.push_back({{COMMAND, 0u, 0u}, old_command_count});
        return RECOMP_D3D_PRESENTER_OK;
    } catch (const std::bad_alloc &) {
    } catch (const std::length_error &) {
    }

    // Erase only spans from this failed add; rehashing may have moved iterators.
    for (size_t i = 0u; i < inserted_count; ++i) eraseSpansFrom(inserted_sources[i], old_payload_size);
    commands_.resize(old_command_count);
    payload_.resize(old_payload_size);
    return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
}

size_t D3dCapturePacket::findSpan(const void *source, size_t size) const
{
    if (span_slots_.empty()) return null_offset;
    const size_t mask = span_slots_.size() - 1u;
    for (size_t h = spanHash(source) & mask;; h = (h + 1u) & mask) {
        const SpanSlot &slot = span_slots_[h];
        if (slot.source == nullptr) return null_offset;
        if (slot.source == source && slot.size == size && (size == 0u ||
            std::memcmp(source, payload_.data() + slot.offset, size) == 0)) {
            return slot.offset;
        }
    }
}

void D3dCapturePacket::insertSpan(const void *source, size_t size, size_t offset)
{
    // Slots used (tombstones included) stay under half the table.
    if ((span_count_ + 1u) * 2u > span_slots_.size()) {
        std::vector<SpanSlot> old;
        old.swap(span_slots_);
        try {
            span_slots_.assign(std::max<size_t>(4096u, old.size() * 2u), SpanSlot{nullptr, 0u, 0u});
        } catch (...) {
            span_slots_.swap(old);
            throw;
        }
        span_count_ = 0u;
        for (const SpanSlot &slot : old) {
            if (slot.source != nullptr && slot.source != span_tombstone) {
                insertSpan(slot.source, slot.size, slot.offset);
            }
        }
    }
    const size_t mask = span_slots_.size() - 1u;
    size_t h = spanHash(source) & mask;
    while (span_slots_[h].source != nullptr) h = (h + 1u) & mask;
    span_slots_[h] = SpanSlot{source, size, offset};
    ++span_count_;
}

void D3dCapturePacket::eraseSpansFrom(const void *source, size_t first_offset)
{
    if (span_slots_.empty()) return;
    const size_t mask = span_slots_.size() - 1u;
    for (size_t h = spanHash(source) & mask; span_slots_[h].source != nullptr; h = (h + 1u) & mask) {
        SpanSlot &slot = span_slots_[h];
        if (slot.source == source && slot.offset >= first_offset) slot.source = span_tombstone;
    }
}

RecompD3dPresenterError D3dCapturePacket::addRecord(
    Kind kind, uint32_t base, uint32_t size)
{
    if (sealed_) return RECOMP_D3D_PRESENTER_INVALID_ARGUMENT;
    try {
        entries_.push_back({{kind, base, size}, 0u});
        return RECOMP_D3D_PRESENTER_OK;
    } catch (const std::bad_alloc &) {
    } catch (const std::length_error &) {
    }
    return RECOMP_D3D_PRESENTER_OUT_OF_MEMORY;
}

RecompD3dPresenterError D3dCapturePacket::addRelease(uint32_t base, uint32_t size)
{
    return addRecord(RELEASE, base, size);
}

RecompD3dPresenterError D3dCapturePacket::addReport()
{
    return addRecord(REPORT, 0u, 0u);
}

void D3dCapturePacket::seal()
{
    if (sealed_) return;
    for (auto &captured : commands_) {
        if (captured.value.type != RECOMP_D3D_PRESENTER_COMMAND_DRAW) continue;
        for (size_t i = 0u; i < 7u; ++i) {
            if (captured.offsets[i] == borrowed_offset) continue;
            captured.value.data.draw.*pointer_fields[i] = captured.offsets[i] == null_offset
                ? nullptr : payload_.data() + captured.offsets[i];
        }
    }
    sealed_ = true;
}

size_t D3dCapturePacket::count() const
{
    return entries_.size();
}

const D3dCapturePacket::Record &D3dCapturePacket::record(size_t index) const
{
    return entries_[index].record;
}

const RecompD3dPresenterCommand &D3dCapturePacket::command(size_t index) const
{
    assert(sealed_ && entries_[index].record.kind == COMMAND);
    return commands_[entries_[index].command_index].value;
}

void D3dCapturePacket::clear()
{
    entries_.clear();
    commands_.clear();
    payload_.clear();
    if (span_count_ != 0u) {
        std::fill(span_slots_.begin(), span_slots_.end(), SpanSlot{nullptr, 0u, 0u});
        span_count_ = 0u;
    }
    sealed_ = false;
}

uint64_t D3dCapturePacket::bytes() const
{
    return uint64_t(commands_.size()) * sizeof(RecompD3dPresenterCommand) +
        uint64_t(entries_.size()) * sizeof(Record) + uint64_t(payload_.size()) * sizeof(uint64_t);
}
