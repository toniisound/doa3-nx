#include "d3d_miniport_adapter.h"
#include "kernel_abi.h"

/* DOA3 (D3D8 3925) addresses; see docs/doa3-d3d-symbols.md. CDevice::Init
   (0x001B94F0) stays generated: it allocates the context and push buffer and
   builds the frame buffers. These replace only what it does to the GPU. */
enum {
    KICK_OFF = 0x001b88c0u,              /* CDevice::KickOff, thiscall */
    MINIPORT_INIT = 0x001bb770u,         /* thiscall */
    MINIPORT_CREATE_CHANNEL = 0x001bb1f3u,       /* thiscall, 5 args */
    MINIPORT_BIND_OBJECT = 0x001bb256u,          /* thiscall, 1 arg */
    MINIPORT_CREATE_DMA_OBJECT = 0x001bb595u,    /* thiscall, 5 args */
    MINIPORT_CREATE_GRAPHICS_OBJECT = 0x001bb66au, /* thiscall, 3 args */
    MINIPORT_SET_MODE = 0x001ba7d8u,             /* thiscall, 7 args */
    MINIPORT_SET_TILE = 0x001bb8a8u,             /* thiscall, 7 args */
    MINIPORT_CLEAR_TILE = 0x001bba96u,           /* thiscall, 2 args */
    FLUSH_PUSHER = 0x001b8890u,  /* PFIFO flush and spin, no args */

    GPU_REGISTER_BASE = 0xfd000000u,
    CHANNEL_SIZE = 0x80u,
    CHANNEL_DMA_PUT = 0x40u,
    CHANNEL_DMA_GET = 0x44u,

#ifdef RECOMP_DOA3_JP_BINDINGS
    /* D3D8 4134 (Japan 3.1) CDevice, from CDevice::KickOff and D3D_SetFence;
       see docs/doa3-jp-test.md. */
    DEVICE_PUT = 0x0000u,
    DEVICE_FLAGS = 0x0008u,
    DEVICE_KICKED_PUT = 0x002cu,
    DEVICE_FENCE = 0x0030u,
    DEVICE_FENCE_RECORDS = 0x0050u,
    DEVICE_CONTEXT = 0x0034u,
    DEVICE_ALTERNATE_PUT = 0x035cu,
    DEVICE_FLIPS_DONE = 0x2458u,
    DEVICE_CHANNEL = 0x2264u,
    DEVICE_FRAME_COUNTER = 0x2abcu,
#else
    DEVICE_PUT = 0x0000u,
    DEVICE_FLAGS = 0x000cu,
    DEVICE_KICKED_PUT = 0x0018u,
    DEVICE_FENCE = 0x001cu,
    DEVICE_FENCE_RECORDS = 0x00e4u,
    DEVICE_CONTEXT = 0x03f0u,
    DEVICE_ALTERNATE_PUT = 0x0400u,
    DEVICE_FLIPS_DONE = 0x2518u,
    DEVICE_CHANNEL = 0x2304u,
    DEVICE_FRAME_COUNTER = 0x2b60u,
#endif
    DEVICE_FLAG_ALTERNATE = 0x4u,
};

static uint32_t u32(uint32_t address)
{
    return *recomp_memory_u32(address);
}

static void set_u32(uint32_t address, uint32_t value)
{
    *recomp_memory_u32(address) = value;
}

void recomp_d3d_miniport_kick_off(uint32_t device)
{
    uint32_t put = u32(device + DEVICE_PUT);
    uint32_t channel = u32(device + DEVICE_CHANNEL);
    uint32_t fence = u32(device + DEVICE_FENCE);
    uint32_t record = device + DEVICE_FENCE_RECORDS +
        ((fence >> 1u) & 63u) * 12u;

    if (u32(device + DEVICE_FLAGS) & DEVICE_FLAG_ALTERNATE) {
        put = u32(device + DEVICE_ALTERNATE_PUT);
    }
    set_u32(channel + CHANNEL_DMA_PUT, put & 0x03ffffffu);
    set_u32(channel + CHANNEL_DMA_GET, put & 0x03ffffffu);
    set_u32(device + DEVICE_KICKED_PUT, put);
    /* Every fence issued so far has passed, and every flip is done. */
    /* InsertFence records the issued value before KickOff, then increments
       the counter. Other callers see the next, still unissued value. */
    set_u32(u32(device + DEVICE_CONTEXT),
            u32(record) == fence ? fence : fence - 2u);
    set_u32(device + DEVICE_FLIPS_DONE, u32(device + DEVICE_FRAME_COUNTER));
}

/* thiscall: ECX is the object; kernel_return pops the stack arguments. */
static void kick_off(void)
{
    recomp_d3d_miniport_kick_off(recomp_runtime.registers.ecx);
    kernel_return(0u, recomp_runtime.registers.eax);
}

static void miniport_init(void)
{
    /* Word 0 is the register base Init copies to device+0x404. Keeping the
       real base makes any GPU access left in generated code stop loudly. */
    set_u32(recomp_runtime.registers.ecx, GPU_REGISTER_BASE);
    kernel_return(0u, 1u);
}

static void create_channel(void)
{
    /* The channel stands in for the PFIFO user registers (DMA put/get). */
    uint32_t channel = recomp_kernel_allocate_pool(CHANNEL_SIZE);

    if (channel == 0u) {
        kernel_return(5u, 0u);
        return;
    }
    recomp_guest_memset(channel, 0, CHANNEL_SIZE);
    set_u32(kernel_arg(5u), channel);
    kernel_return(5u, 1u);
}

/* The 16-byte object record Init and the hash-table bind read back. */
static void write_object(uint32_t record, uint32_t handle, uint32_t object_class,
                         uint16_t bound)
{
    recomp_guest_memset(record, 0, 16u);
    set_u32(record, handle);
    *recomp_memory_u16(record + 6u) = bound;
    set_u32(record + 8u, object_class);
}

static void create_dma_object(void)
{
    write_object(kernel_arg(5u), kernel_arg(1u), kernel_arg(2u), 0u);
    kernel_return(5u, 1u);
}

static void create_graphics_object(void)
{
    write_object(kernel_arg(3u), kernel_arg(1u), kernel_arg(2u), 1u);
    kernel_return(3u, 1u);
}

static void bind_object(void)
{
    kernel_return(1u, 1u);
}

static void set_mode(void)
{
    kernel_return(7u, 1u);
}

/* Tiles only change how the GPU addresses memory; there is no GPU. */
static void set_tile(void)
{
    kernel_return(7u, 1u);
}

static void clear_tile(void)
{
    kernel_return(2u, 1u);
}

static void flush_pusher(void)
{
    kernel_return(0u, recomp_runtime.registers.eax);
}

RecompFunction recomp_d3d_miniport_lookup_manual(uint32_t guest_address)
{
    switch (guest_address) {
    case KICK_OFF: return kick_off;
    case MINIPORT_INIT: return miniport_init;
    case MINIPORT_CREATE_CHANNEL: return create_channel;
    case MINIPORT_BIND_OBJECT: return bind_object;
    case MINIPORT_CREATE_DMA_OBJECT: return create_dma_object;
    case MINIPORT_CREATE_GRAPHICS_OBJECT: return create_graphics_object;
    case MINIPORT_SET_MODE: return set_mode;
    case MINIPORT_SET_TILE: return set_tile;
    case MINIPORT_CLEAR_TILE: return clear_tile;
    case FLUSH_PUSHER: return flush_pusher;
    default: return NULL;
    }
}
