#include "sikradio.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Structures used from sikradio.h:
 * - no shared runtime structure is manipulated directly in this module;
 *   io.c mainly uses shared constants and exposes the central output and
 *   logging functions used by the rest of the program.
 */

/* Module-local logging threshold set once the configuration has been parsed.
 * All formatted diagnostic messages go through this single state variable. */
static int g_verbosity = DEFAULT_VERBOSITY;

/* Low-level write helper used by every other function in this module.
 * It guarantees that all requested bytes are written unless a real error
 * occurs, and it hides partial writes and EINTR handling from callers. */
int write_all_fd(int fd, const unsigned char *buf, size_t len)
{
    size_t written = 0;

    if (buf == NULL && len != 0) {
        errno = EINVAL;
        return -1;
    }

    while (written < len) {
        ssize_t rc = write(fd, buf + written, len - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            errno = EIO;
            return -1;
        }
        written += (size_t)rc;
    }

    return 0;
}

/* Public audio output path used by the streaming code.
 * This wrapper exists so future modules never write audio directly with
 * stdio functions and always send raw bytes to standard output. */
int write_stdout_audio(const unsigned char *buf, size_t len)
{
    return write_all_fd(STDOUT_FILENO, buf, len);
}

/* Public raw stderr writer used for diagnostics and text metadata.
 * It shares the same safe write loop as audio output, but always targets
 * standard error. */
int write_stderr_bytes(const unsigned char *buf, size_t len)
{
    return write_all_fd(STDERR_FILENO, buf, len);
}

/* Text-oriented stderr helper used by main() and log_msg().
 * It writes the provided string followed by a newline so callers can emit
 * human-readable messages without duplicating line-ending logic. */
int write_stderr_line(const char *s)
{
    size_t len;

    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }

    len = strlen(s);
    if (write_stderr_bytes((const unsigned char *)s, len) != 0) {
        return -1;
    }

    return write_stderr_bytes((const unsigned char *)"\n", 1);
}

/* Public setter called after successful argument parsing.
 * It stores the verbosity level that later controls log_msg() output. */
void log_set_verbosity(int verbosity)
{
    g_verbosity = verbosity;
}

/* Central formatted logger for diagnostic messages.
 * It is intended for communication logs, critical errors, noncritical
 * warnings, and debug output, and it prints only when the requested level
 * is enabled by log_set_verbosity(). */
void log_msg(int level, const char *fmt, ...)
{
    char buffer[ERRBUF_SIZE];
    va_list args;

    if (level > g_verbosity || fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    write_stderr_line(buffer);
}

/* Timestamp helper for communication logs.
 * Other modules will use it before important network events so all runtime
 * traces share one consistent YYYY.MM.DD HH.MM.SS format. */
void log_timestamp(void)
{
    char buffer[32];
    time_t now;
    struct tm *tm_info;

    if (g_verbosity < LOG_COMMUNICATION) {
        return;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return;
    }

    tm_info = localtime(&now);
    if (tm_info == NULL) {
        return;
    }

    if (strftime(buffer, sizeof(buffer), "%Y.%m.%d %H.%M.%S", tm_info) == 0) {
        return;
    }

    write_stderr_line(buffer);
}

/* Raw metadata output path intended for ICY text blocks.
 * Unlike log_msg(), this function must not check verbosity because stream
 * metadata should always reach stderr exactly as text payload. */
void log_metadata(const unsigned char *buf, size_t len)
{
    if (buf == NULL && len != 0) {
        return;
    }

    write_stderr_bytes(buf, len);
}
