/**
 * @file sim_pty.c
 * @brief Optional virtual serial port (PTY) for GUI keyboard
 *        integration. See sim_pty.h for the contract.
 *
 * macOS-specific (<util.h>'s openpty()) - Linux would need <pty.h>'s
 * openpty() instead, not built/tested here since this project's
 * toolchain docs are already macOS-first (see CLAUDE.md).
 */

#include "sim_pty.h"

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

/** Master (primary) side fd, or -1 if the PTY was never successfully opened. */
static int master_fd = -1;

/** The replica (slave) fd openpty() also hands back, held open for the
 *  whole process lifetime purely as a keepalive - on macOS, a PTY's
 *  master side can see EIO/EOF-like behavior before anything has
 *  opened the slave path. Keeping our own fd to the slave open
 *  guarantees the master stays usable immediately, even before
 *  tools/hp41_keyboard_gui.py (opening the same path as a second,
 *  independent fd - a PTY slave path is not exclusive-open) attaches.
 *  Never read/written by this process. */
static int replica_fd_keepalive = -1;

/**
 * @brief Open a PTY and report its slave device path; see the header.
 *
 * @param slave_path_out Buffer to receive the slave device path.
 * @param slave_path_buf_size Size of @p slave_path_out (>= 128).
 * @return true on success.
 */
bool sim_pty_open(char *slave_path_out, size_t slave_path_buf_size)
{
    assert(slave_path_out != NULL);
    assert(slave_path_buf_size >= 128); /* openpty()'s own documented minimum */

    /* Raw mode (no ECHO/ICANON/ISIG/etc.) is essential, not cosmetic:
     * the default tty line discipline echoes back whatever's written to
     * the master as if it were terminal input - since sim_dbg() writes
     * debug-log text to the master (see sim_pty_write()) and
     * sim_pty_read_byte() reads from that same master, a non-raw PTY
     * would feed the sim's own log text back into
     * hp41_key_bridge_feed_byte() as spurious keypresses. Confirmed as
     * a real bug during development, not a theoretical concern - see
     * CLAUDE.md's "Host-native simulator" section. */
    struct termios raw_termios;
    cfmakeraw(&raw_termios);

    char name_buf[128];
    int primary_fd = -1;
    int replica_fd = -1;
    if (openpty(&primary_fd, &replica_fd, name_buf, &raw_termios, NULL) != 0)
        return false;

    int flags = fcntl(primary_fd, F_GETFL, 0);
    assert(flags != -1);
    int fcntl_ok = fcntl(primary_fd, F_SETFL, flags | O_NONBLOCK);
    assert(fcntl_ok == 0);
    (void)fcntl_ok;

    master_fd = primary_fd;
    replica_fd_keepalive = replica_fd;

    size_t name_len = strnlen(name_buf, sizeof(name_buf));
    assert(name_len < slave_path_buf_size);
    memcpy(slave_path_out, name_buf, name_len + 1);

    assert(master_fd >= 0);
    return true;
}

/**
 * @brief Read one pending byte from the PTY, without blocking; see the header.
 *
 * @return The byte, or -1 if none is pending / the PTY isn't open.
 */
int sim_pty_read_byte(void)
{
    if (master_fd < 0)
        return -1;

    unsigned char byte;
    ssize_t n = read(master_fd, &byte, 1);
    assert(n == 1 || n == 0 || n == -1); /* read()'s only possible results for a 1-byte non-blocking request */
    if (n != 1)
        return -1; /* EAGAIN/EWOULDBLOCK (nothing pending), EOF, or an error - all treated alike */

    int result = (int)byte;
    assert(result >= 0 && result <= 255);
    return result;
}

/**
 * @brief Write raw bytes to the PTY, if it's open; see the header.
 *
 * @param data Bytes to write.
 * @param len Number of bytes in @p data.
 */
void sim_pty_write(const char *data, size_t len)
{
    assert(data != NULL || len == 0);
    if (master_fd < 0)
        return;

    ssize_t written = write(master_fd, data, len);
    assert(written <= (ssize_t)len); /* write() never reports more bytes written than requested */
    (void)written; /* best-effort - a disconnected/full PTY losing a log line is not fatal */
}

/* replica_fd_keepalive is intentionally never used past sim_pty_open() -
 * see its own comment above for why it must still be held open. */
