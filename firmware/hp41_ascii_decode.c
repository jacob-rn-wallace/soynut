/**
 * @file hp41_ascii_decode.c
 * @brief Implements hp41_decode_ascii() - see hp41_ascii_decode.h for
 *        the public contract and provenance.
 */

#include "hp41_ascii_decode.h"

#include <assert.h>

int hp41_decode_ascii(int v)
{
    v &= 0x13f;
    assert(v >= 0 && v <= 0x13f);

    int result;
    if (v <= 0x1f) {
        result = v + '@';
    } else if (v <= 0x3f) {
        if (v == 0x2c)      result = '<';  /* backward flying goose */
        else if (v == 0x2e) result = '>';  /* flying goose */
        else if (v == 0x3a) result = '*';  /* starburst */
        else                result = v;
    } else if (v <= 0x105) {
        result = v - 0xa0;
    } else if (v <= 0x11f) {
        switch (v) {
            case 0x106: result = '~';  break; /* top bar */
            case 0x107: result = '\''; break; /* append */
            case 0x10c: result = 'u';  break; /* micro */
            case 0x10d: result = '#';  break; /* different sign */
            case 0x10e: result = 's';  break; /* sigma */
            case 0x10f: result = 'a';  break; /* angle */
            default:    result = 'x';  break; /* non-displayable */
        }
    } else {
        result = v - 0x120 + 'a' - 1;
    }

    /* Guards the assumption every caller relies on: the result is used
     * directly as an index into a 128-entry char_segments table (after a
     * caller-side & 0x7f). This function's input domain is sparse in
     * practice (only combinations the real ROM's display registers
     * actually produce), not the full 0-0x13f range the mask above
     * allows - if a future code path ever fed something outside that
     * sparse set, the v-0xa0 branch above could go negative, and a
     * negative index into that table is undefined behavior. Catch it
     * here instead. */
    assert(result >= 0);
    return result;
}
