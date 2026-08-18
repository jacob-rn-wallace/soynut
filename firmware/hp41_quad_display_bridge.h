/**
 * @file hp41_quad_display_bridge.h
 * @brief Computes a framebuffer for the QUAD-style 400x240 Sharp
 *        Memory LCD display, in either of its two view modes.
 *
 * Phase 3b of the Magellan/QUAD plan
 * (/Users/jake/.claude/plans/gentle-mapping-dewdrop.md) - see its
 * Context section and CLAUDE.md's "Sharp Memory LCD bring-up" section
 * for the full picture. Pure logic, no hardware access - safe to
 * call/test on a host build, same as hp41_display_bridge.h.
 *
 * **Framebuffer polarity is the opposite of hp41_display_bridge.h's**:
 * this is a light-background/dark-segment reflective panel (confirmed
 * on real hardware in quad_bringup/ - see CLAUDE.md), cleared to
 * 0xFF (all-white), where a lit (visible, dark) segment is drawn by
 * *clearing* its bit, not setting it.
 */
#ifndef SOYNUT_HP41_QUAD_DISPLAY_BRIDGE_H
#define SOYNUT_HP41_QUAD_DISPLAY_BRIDGE_H

#include <stdint.h>

#include "hp41_quad_font_table.h"

/** Framebuffer size for this panel: 1bpp, row-major, MSB-first, no row
 *  padding (400/8 = 50 divides evenly) - matches
 *  third_party/pico_sharpmem_display's own BITMAP_SIZE() macro's shape,
 *  but defined independently here so firmware/ code (shared by a future
 *  quad/ build target) never needs to depend on quad_bringup/'s
 *  vendored subtree. */
#define HP41_QUAD_FB_SIZE \
    (((HP41_QUAD_DISP_WIDTH_PX + 7) / 8) * HP41_QUAD_DISP_HEIGHT_PX)

/**
 * @brief Which of the display's two view modes to render.
 *
 * Toggled by a new one-shot bridge command (see hp41_key_bridge.h's
 * "[DSP]" escape, Phase 3c) - mirrors the QUAD's own dedicated DSP key,
 * minus its System-Info chrome and multiline program-listing views
 * (both explicitly out of scope for this pass - see the plan's Context
 * section).
 */
typedef enum {
    /** T/Z/Y/X, each row real FIX/SCI/ENG-formatted (hp41_register_format.h). */
    HP41_QUAD_VIEW_STACK,
    /** Whatever the classic single-line 12-character display currently
     *  shows (ALPHA text, errors, program steps, catalog entries, ...) -
     *  the exact same decode hp41_display_bridge.c already does, just
     *  plotted at this panel's own scale. The only view that can show
     *  arbitrary text, since T/Z/Y/X are hardware-numeric-only registers. */
    HP41_QUAD_VIEW_CLASSIC_LINE,
} hp41_quad_view_t;

/**
 * @brief Compute a framebuffer for the requested view.
 *
 * No floating point or polygon math at runtime - matches every other
 * display path in this codebase (precomputed pixel-lookup tables only).
 *
 * @param fb   Output buffer, at least HP41_QUAD_FB_SIZE bytes; fully
 *             overwritten (cleared to all-white, 0xFF, then segments
 *             plotted by clearing bits).
 * @param view Which view to render.
 */
void hp41_quad_display_compute_framebuffer(uint8_t *fb, hp41_quad_view_t view);

#endif // SOYNUT_HP41_QUAD_DISPLAY_BRIDGE_H
