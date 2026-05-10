#include "sikradio.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_verbosity = DEFAULT_VERBOSITY;

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

int write_stdout_audio(const unsigned char *buf, size_t len)
{
    return write_all_fd(STDOUT_FILENO, buf, len);
}

int write_stderr_bytes(const unsigned char *buf, size_t len)
{
    return write_all_fd(STDERR_FILENO, buf, len);
}

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

void log_set_verbosity(int verbosity)
{
    g_verbosity = verbosity;
}

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

void log_timestamp(void)
{
    char buffer[32];
    time_t now;
    struct tm *tm_info;

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

void log_metadata(const unsigned char *buf, size_t len)
{
    if (buf == NULL && len != 0) {
        return;
    }

    write_stderr_bytes(buf, len);
}
