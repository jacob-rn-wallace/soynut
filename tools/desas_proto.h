/**
 * @file desas_proto.h
 * @brief Force-included prototype for char41(), which emu41gcc's vendored
 *        desas41.c calls without any header declaring it (upstream relied
 *        on implicit-int declarations) - see nut_disasm.c for the actual
 *        definition and full context.
 */

/**
 * @brief Decode one HP-41 CON-string display code to a printable ASCII char.
 * @param v Raw display code.
 * @return The decoded character.
 */
char char41(int v);
