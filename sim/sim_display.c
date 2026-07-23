/**
 * @file sim_display.c
 * @brief SDL2-backed virtual LCD - defines st7920_init()/st7920_clear()/
 *        st7920_draw_frame() (declared in firmware/st7920.h) as a
 *        drop-in replacement for the real GPIO driver in
 *        firmware/st7920.c. Never called on a fixed timer - only from
 *        the same fdsp/redraw_needed-gated call sites real firmware
 *        already uses, so redraw cadence stays faithful to real
 *        hardware. Performs no segment decoding of its own: it only
 *        ever blits the already-fully-decoded 1bpp bytes it's handed,
 *        keeping hp41_display_bridge.c the single source of truth for
 *        what a pixel means.
 */

#include "st7920.h"

#include <assert.h>
#include <stdint.h>

#include <SDL.h>

#include "sim_display.h"

/** Lit-pixel and background colors, ARGB packed to match the streaming
 *  texture's SDL_PIXELFORMAT_ARGB8888 format. Approximates the real
 *  HP-41's reflective LCD look (pale yellow-green background, dark
 *  near-black segments) rather than an emissive phosphor-style display
 *  - easily swapped later. */
#define SIM_DISPLAY_COLOR_LIT 0xFF1A1A14u
#define SIM_DISPLAY_COLOR_OFF 0xFFC6C996u

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;

/** Fixed-size, module-static pixel buffer (Power of 10, Rule 3: no
 *  dynamic allocation after st7920_init()'s one-time SDL object
 *  creation) - reused by both st7920_clear() and st7920_draw_frame(). */
static uint32_t pixels[LCD_WIDTH_PX * LCD_HEIGHT_PX];

/**
 * @brief Push the current contents of pixels[] to the window.
 *
 * Shared tail end of st7920_clear() and st7920_draw_frame() - both just
 * fill pixels[] differently first.
 */
static void present_pixels(void)
{
    assert(texture != NULL);
    assert(renderer != NULL);
    int update_ok = SDL_UpdateTexture(texture, NULL, pixels, LCD_WIDTH_PX * (int)sizeof(pixels[0]));
    assert(update_ok == 0);
    (void)update_ok;
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/**
 * @brief Configure the SDL window/renderer/texture and run once-only setup.
 *
 * Must be called once before st7920_clear()/st7920_draw_frame() - see
 * st7920.h.
 */
void st7920_init(void)
{
    assert(window == NULL); /* not already initialized */
    assert(renderer == NULL);

    int sdl_ok = SDL_Init(SDL_INIT_VIDEO);
    assert(sdl_ok == 0);
    (void)sdl_ok;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest-neighbor: keep segment edges crisp */

    window = SDL_CreateWindow(
        "Soynut - virtual HP-41 LCD",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LCD_WIDTH_PX * SIM_DISPLAY_SCALE, LCD_HEIGHT_PX * SIM_DISPLAY_SCALE,
        SDL_WINDOW_SHOWN);
    assert(window != NULL);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    assert(renderer != NULL);

    int logical_ok = SDL_RenderSetLogicalSize(renderer, LCD_WIDTH_PX, LCD_HEIGHT_PX);
    assert(logical_ok == 0);
    (void)logical_ok;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 LCD_WIDTH_PX, LCD_HEIGHT_PX);
    assert(texture != NULL);
}

/**
 * @brief Blank the virtual LCD; see st7920.h.
 */
void st7920_clear(void)
{
    assert(texture != NULL);
    for (int i = 0; i < LCD_WIDTH_PX * LCD_HEIGHT_PX; i++) {
        pixels[i] = SIM_DISPLAY_COLOR_OFF;
    }
    assert(pixels[0] == SIM_DISPLAY_COLOR_OFF);
    present_pixels();
}

/**
 * @brief Push a full framebuffer to the window; see st7920.h.
 *
 * @param fb LCD_FB_SIZE bytes, MSB-first per row, row-major, 1bpp.
 */
void st7920_draw_frame(const uint8_t *fb)
{
    assert(fb != NULL);
    assert(texture != NULL);

    for (int y = 0; y < LCD_HEIGHT_PX; y++) {
        for (int x = 0; x < LCD_WIDTH_PX; x++) {
            int byte_idx = y * LCD_BYTES_PER_ROW + x / 8;
            int bit = (fb[byte_idx] >> (7 - (x % 8))) & 1;
            pixels[y * LCD_WIDTH_PX + x] = bit ? SIM_DISPLAY_COLOR_LIT : SIM_DISPLAY_COLOR_OFF;
        }
    }
    present_pixels();
}
