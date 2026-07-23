/**
 * @file sim_pty.h
 * @brief Optional virtual serial port for the simulator - a PTY whose
 *        slave device path can be handed to
 *        tools/hp41_keyboard_gui.py's --port flag, so that tool's
 *        clickable photo-keyboard can drive the sim exactly like it
 *        drives real hardware, with zero changes to the GUI itself.
 *
 * Purely additive: if opening the PTY fails, the sim keeps working
 * standalone via the SDL window's own keyboard input (sim_keyboard.h) -
 * GUI integration is never a hard requirement.
 */
#ifndef SOYNUT_SIM_PTY_H
#define SOYNUT_SIM_PTY_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Open a PTY and report its slave device path.
 *
 * Sets the master side non-blocking internally, so sim_pty_read_byte()
 * never stalls the caller. Call once at startup.
 *
 * @param slave_path_out Buffer to receive the NUL-terminated slave
 *                        device path (e.g. "/dev/ttys005").
 * @param slave_path_buf_size Size of @p slave_path_out - must be at
 *                             least 128 bytes (openpty()'s own
 *                             documented minimum).
 * @return true if the PTY was opened successfully; false otherwise
 *         (non-fatal - see the file header).
 */
bool sim_pty_open(char *slave_path_out, size_t slave_path_buf_size);

/**
 * @brief Read one pending byte from the PTY, without blocking.
 *
 * @return The byte (0-255), or -1 if none is pending or the PTY was
 *         never successfully opened.
 */
int sim_pty_read_byte(void);

/**
 * @brief Write raw bytes to the PTY, if it's open.
 *
 * Used to tee sim_dbg()'s log lines to a connected GUI's serial log
 * pane, mirroring real hardware's single-USB-CDC-connection behavior
 * (debug log and key traffic share one stream). A no-op if the PTY was
 * never successfully opened.
 *
 * @param data Bytes to write.
 * @param len Number of bytes in @p data.
 */
void sim_pty_write(const char *data, size_t len);

#endif // SOYNUT_SIM_PTY_H
