/**
 * @file hp41_register_decode.c
 * @brief Implements hp41_elite_decode_register() - see
 *        hp41_register_decode.h for the public contract and provenance.
 */

#include "hp41_register_decode.h"

#include <assert.h>
#include <stddef.h>

#define GLOBAL extern
#include "nutcpu.h"

/**
 * @brief Decode one stack register (T/Z/Y/X) directly from espaceRAM.
 *
 * See hp41_register_decode.h for the public contract; this is the
 * implementation. Nibble layout and packing confirmed directly from
 * emu41gcc/nutcpu.c's recallData()/storeData() and exec2()'s
 * field-selector table - see CLAUDE.md.
 */
void hp41_elite_decode_register(int stack_index, hp41_elite_number_t *out)
{
    assert(stack_index >= HP41_ELITE_REG_T && stack_index <= HP41_ELITE_REG_X);
    assert(out != NULL);

    int base = stack_index * 8;
    /* Byte 0 is a write-protect flag; documented as essentially always 0
     * for the stack registers - advisory check only, not a behavior gate. */
    assert(espaceRAM[base] == 0);

    uint8_t nibble[14];
    for (int i = 0; i < 7; i++) {
        uint8_t b = espaceRAM[base + 1 + i];
        nibble[2 * i] = b & 0x0F;
        nibble[2 * i + 1] = (b >> 4) & 0x0F;
    }
    for (int i = 0; i < 14; i++)
        assert(nibble[i] <= 9);

    out->exponent_units = nibble[0];
    out->exponent_tens = nibble[1];
    out->exponent_negative = (nibble[2] == 9);
    out->mantissa_negative = (nibble[13] == 9);
    for (int k = 1; k <= 10; k++)
        out->mantissa_digits[k - 1] = nibble[13 - k];

    assert(out->exponent_tens <= 9 && out->exponent_units <= 9);
}
