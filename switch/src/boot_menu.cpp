// SPDX-License-Identifier: GPL-3.0-or-later
// Boot menu: the render resolution, drawn on a software framebuffer with the
// console's own 16x16 font scaled up (the same look as DOA2U NX's menu).
#include <switch.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

// RGBA8888 as the framebuffer stores it (R in the low byte).
constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000u | (b << 16) | (g << 8) | r; }
constexpr uint32_t kBackground = rgb(0x12, 0x14, 0x1a);
constexpr uint32_t kFrame = rgb(0xe6, 0xe6, 0xe6);
constexpr uint32_t kText = rgb(0xff, 0xff, 0xff);
constexpr uint32_t kDim = rgb(0xb4, 0xb4, 0xb4);
constexpr uint32_t kAccent = rgb(0xe8, 0x62, 0x2c);

struct Canvas {
    uint32_t *pixels;
    uint32_t stride; // in pixels
    const uint16_t *font;

    void fill(int x, int y, int w, int h, uint32_t color)
    {
        for (int row = y < 0 ? 0 : y; row < y + h && row < kHeight; ++row) {
            uint32_t *line = pixels + row * stride;
            for (int col = x < 0 ? 0 : x; col < x + w && col < kWidth; ++col) line[col] = color;
        }
    }

    // A rectangle with its corners cut, close enough to rounded at this size.
    void rounded(int x, int y, int w, int h, int r, uint32_t color)
    {
        for (int row = 0; row < h; ++row) {
            int inset = 0;
            const int edge = row < r ? r - row : row >= h - r ? row - (h - r) + 1 : 0;
            if (edge > 0) {
                // Circle of radius r: inset = r - sqrt(r^2 - (r - edge)^2), integer search.
                const int d = r - edge;
                while (inset < r && (r - inset) * (r - inset) + d * d > r * r) ++inset;
            }
            fill(x + inset, y + row, w - 2 * inset, 1, color);
        }
    }

    void outline(int x, int y, int w, int h, int r, int thickness, uint32_t color, uint32_t inside)
    {
        rounded(x, y, w, h, r, color);
        rounded(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
            r > thickness ? r - thickness : 0, inside);
    }

    // libnx glyphs: 16 rows of 16 bits, the leftmost pixel in bit 15.
    void glyph(int x, int y, char c, int scale, uint32_t color)
    {
        const uint16_t *rows = font + 16 * static_cast<uint8_t>(c);
        for (int row = 0; row < 16; ++row) {
            for (int col = 0; col < 16; ++col) {
                if (rows[row] & (0x8000u >> col)) fill(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }

    static int textWidth(const char *text, int scale) { return static_cast<int>(std::strlen(text)) * 16 * scale; }

    void centered(int y, const char *text, int scale, uint32_t color)
    {
        int x = (kWidth - textWidth(text, scale)) / 2;
        for (const char *c = text; *c; ++c, x += 16 * scale) glyph(x, y, *c, scale, color);
    }
};

struct Choice { int height; const char *label; const char *note; };
const Choice kChoices[] = {
    {720, "720p", "Best quality"},
    {600, "600p", "Recommended"},
    {480, "480p", "Smoother"},
};
constexpr int kCount = sizeof kChoices / sizeof kChoices[0];

void draw(Canvas &canvas, const char *version, int selected)
{
    canvas.fill(0, 0, kWidth, kHeight, kBackground);
    canvas.outline(40, 30, kWidth - 80, kHeight - 60, 36, 4, kFrame, kBackground);

    char title[64];
    std::snprintf(title, sizeof title, "DOA3 NX %s", version);
    canvas.centered(62, title, 3, kText);
    canvas.centered(124, "Choose the resolution", 2, kDim);

    constexpr int kBoxW = 520, kBoxH = 136, kTop = 180, kGap = 12;
    for (int i = 0; i < kCount; ++i) {
        const int y = kTop + i * (kBoxH + kGap);
        if (i == selected) canvas.rounded((kWidth - kBoxW) / 2, y, kBoxW, kBoxH, 10, kAccent);
        canvas.centered(y + 14, kChoices[i].label, 5, kText);
        canvas.centered(y + 98, kChoices[i].note, 2, i == selected ? kText : kDim);
    }
    canvas.centered(kHeight - 92, "Up/Down: select  A: confirm", 2, kDim);
}

} // namespace

// Returns the chosen render height (720, 600 or 480), or 0 to leave.
extern "C" int doa3_nx_boot_menu(int initial_height, const char *version)
{
    int selected = 0;
    for (int i = 0; i < kCount; ++i) {
        if (kChoices[i].height == initial_height) selected = i;
    }

    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, nwindowGetDefault(), kWidth, kHeight, PIXEL_FORMAT_RGBA_8888, 2))) {
        return initial_height;
    }
    framebufferMakeLinear(&fb);
    Canvas canvas{nullptr, 0, static_cast<const uint16_t *>(consoleGetDefault()->font.gfx)};

    PadState pad;
    padInitializeDefault(&pad);
    padUpdate(&pad);
    int result = 0;
    bool redraw = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            selected = (selected + kCount - 1) % kCount;
            redraw = true;
        }
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            selected = (selected + 1) % kCount;
            redraw = true;
        }
        if (down & HidNpadButton_A) {
            result = kChoices[selected].height;
            break;
        }
        if (down & HidNpadButton_Plus) break;
        if (redraw) {
            redraw = false;
            u32 stride = 0;
            canvas.pixels = static_cast<uint32_t *>(framebufferBegin(&fb, &stride));
            canvas.stride = stride / sizeof(uint32_t);
            draw(canvas, version, selected);
            framebufferEnd(&fb);
        }
        svcSleepThread(16'000'000);
    }
    framebufferClose(&fb);
    return result;
}
