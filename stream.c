#define _POSIX_C_SOURCE 200112L

#include "sikradio.h"

#include <errno.h>
#include <poll.h>
#include <openssl/ssl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Structures used from sikradio.h:
 * - conn_t: provides the already opened plain or TLS connection to the server.
 * - config_t: carries the configured timeout used by the polling loop.
 * - icy_demuxer_t: is created only when the response uses icy-metaint.
 * - stream_result_t: reports whether the stream ended normally, by timeout,
 *   by user quit, or because of an error.
 */

enum {
    STDIN_LINE_BUFFER_SIZE = 256,
    STDIN_READ_BUFFER_SIZE = 64
};

/* For TLS connections, decrypted bytes can already be buffered in OpenSSL,
 * so the receive loop should consume them before waiting in poll(). */
static int conn_has_pending_data(const conn_t *conn)
{
    SSL *ssl;

    if (conn == NULL || !conn->use_tls) {
        return 0;
    }

    ssl = (SSL *)conn->ssl;
    if (ssl == NULL) {
        return 0;
    }

    return SSL_pending(ssl) > 0;
}

/* Returns a monotonic timestamp in milliseconds so timeout accounting does
 * not depend on wall-clock adjustments. */
static long long monotonic_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1;
    }

    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

/* Computes how much time is left before the configured receive timeout
 * expires, based only on the last successfully received server bytes. */
static int remaining_timeout_ms(long long last_data_ms, int timeout_ms)
{
    long long now;
    long long elapsed;

    now = monotonic_time_ms();
    if (now < 0) {
        return -1;
    }

    elapsed = now - last_data_ms;
    if (elapsed <= 0) {
        return timeout_ms;
    }
    if (elapsed >= timeout_ms) {
        return 0;
    }

    return timeout_ms - (int)elapsed;
}

/* Appends stdin bytes to the current line buffer and returns 1 only when
 * a complete line equal to "quit" has been observed. */
static int stdin_consume_bytes(const unsigned char *buf, size_t len,
                               unsigned char *line_buf, size_t *line_len,
                               int *line_overflow)
{
    size_t i;

    if ((buf == NULL && len != 0) || line_buf == NULL || line_len == NULL ||
        line_overflow == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < len; i++) {
        unsigned char ch = buf[i];

        if (ch == '\n') {
            size_t current_len = *line_len;

            if (current_len > 0 && line_buf[current_len - 1] == '\r') {
                current_len -= 1;
            }
            if (!*line_overflow && current_len == 4 &&
                memcmp(line_buf, "quit", 4) == 0) {
                return 1;
            }

            *line_len = 0;
            *line_overflow = 0;
            continue;
        }

        if (!*line_overflow) {
            if (*line_len < STDIN_LINE_BUFFER_SIZE) {
                line_buf[*line_len] = ch;
                *line_len += 1;
            } else {
                *line_overflow = 1;
            }
        }
    }

    return 0;
}

/* Reads one ready chunk from stdin, updates the buffered line state, and
 * disables further stdin polling once end-of-file is reached. */
static int stdin_read_ready(unsigned char *read_buf, size_t read_buf_size,
                            unsigned char *line_buf, size_t *line_len,
                            int *line_overflow, int *watch_stdin)
{
    int consume_rc;
    ssize_t stdin_rc;

    if (read_buf == NULL || line_buf == NULL || line_len == NULL ||
        line_overflow == NULL || watch_stdin == NULL) {
        errno = EINVAL;
        return -1;
    }

    stdin_rc = read(STDIN_FILENO, read_buf, read_buf_size);
    if (stdin_rc < 0) {
        if (errno == EINTR) {
            return 0;
        }

        return -1;
    }
    if (stdin_rc == 0) {
        *watch_stdin = 0;
        return 0;
    }

    consume_rc = stdin_consume_bytes(read_buf, (size_t)stdin_rc, line_buf,
                                     line_len, line_overflow);
    if (consume_rc < 0) {
        return -1;
    }

    return consume_rc > 0 ? 1 : 0;
}

/* Main streaming loop used after a successful 200/ICY 200 response.
 * It writes any body bytes already received with the headers, then polls the
 * network connection and stdin to handle audio, ICY metadata, timeouts, and
 * the "quit" command without mixing audio into stderr output.
 * This function intentionally uses goto cleanup to keep the many early-exit
 * paths visually simple while still funneling all shared cleanup through one
 * final block. */
stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config)
{
    unsigned char buf[IO_BUFFER_SIZE];
    unsigned char stdin_line[STDIN_LINE_BUFFER_SIZE];
    unsigned char stdin_buf[STDIN_READ_BUFFER_SIZE];
    icy_demuxer_t demuxer;
    long long last_data_ms;
    stream_result_t result;
    size_t stdin_line_len;
    int stdin_line_overflow;
    int watch_stdin;
    int use_icy;

    if (conn == NULL || conn->fd < 0 || config == NULL) {
        return STREAM_ERROR;
    }

    last_data_ms = monotonic_time_ms();
    if (last_data_ms < 0) {
        return STREAM_ERROR;
    }

    result = STREAM_ERROR;
    stdin_line_len = 0;
    stdin_line_overflow = 0;
    watch_stdin = 1;
    use_icy = 0;
    if (metaint > 0) {
        use_icy = 1;
        demuxer.metaint = 0;
        demuxer.audio_left = 0;
        demuxer.metadata_left = 0;
        demuxer.metadata_buf = NULL;
        demuxer.metadata_len = 0;
        demuxer.metadata_cap = 0;
        demuxer.state = ICY_AUDIO;

        if (icy_init(&demuxer, (size_t)metaint) != 0) {
            return STREAM_ERROR;
        }

        if (body_prefix_len > 0 && icy_feed(&demuxer, body_prefix, body_prefix_len) != 0) {
            goto cleanup;
        }
    } else if (body_prefix_len > 0 && write_stdout_audio(body_prefix, body_prefix_len) != 0) {
        return STREAM_ERROR;
    }

    if (body_prefix_len > 0) {
        last_data_ms = monotonic_time_ms();
        if (last_data_ms < 0) {
            goto cleanup;
        }
    }

    for (;;) {
        int stdin_ready;
        int conn_ready;
        ssize_t rc;

        conn_ready = conn_has_pending_data(conn);
        stdin_ready = 0;
        if (!conn_ready) {
            struct pollfd pfds[2];
            nfds_t pfds_count;
            int poll_rc;
            int timeout_ms;

            pfds[0].fd = conn->fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            pfds_count = 1;

            if (watch_stdin) {
                pfds[1].fd = STDIN_FILENO;
                pfds[1].events = POLLIN;
                pfds[1].revents = 0;
                pfds_count = 2;
            }

            timeout_ms = remaining_timeout_ms(last_data_ms, config->timeout_ms);
            if (timeout_ms < 0) {
                goto cleanup;
            }

            do {
                poll_rc = poll(pfds, pfds_count, timeout_ms);
            } while (poll_rc < 0 && errno == EINTR);

            if (poll_rc < 0) {
                goto cleanup;
            }
            if (poll_rc == 0) {
                result = STREAM_TIMEOUT;
                goto cleanup;
            }
            if ((pfds[0].revents & POLLNVAL) != 0) {
                goto cleanup;
            }
            if (pfds[0].revents != 0) {
                conn_ready = 1;
            }

            if (watch_stdin) {
                if ((pfds[1].revents & POLLNVAL) != 0) {
                    watch_stdin = 0;
                } else if ((pfds[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
                    stdin_ready = 1;
                }
            }
        } else if (watch_stdin) {
            struct pollfd pfd;
            int poll_rc;

            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            pfd.revents = 0;

            do {
                poll_rc = poll(&pfd, 1, 0);
            } while (poll_rc < 0 && errno == EINTR);

            if (poll_rc < 0) {
                goto cleanup;
            }
            if (poll_rc > 0) {
                if ((pfd.revents & POLLNVAL) != 0) {
                    watch_stdin = 0;
                } else if ((pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
                    stdin_ready = 1;
                }
            }
        }

        if (!conn_ready) {
            if (!stdin_ready) {
                continue;
            }
        } else {
            rc = conn_read(conn, buf, sizeof(buf));

            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                goto cleanup;
            }
            if (rc == 0) {
                result = STREAM_SERVER_CLOSED;
                goto cleanup;
            }
            if (use_icy) {
                if (icy_feed(&demuxer, buf, (size_t)rc) != 0) {
                    goto cleanup;
                }
            } else if (write_stdout_audio(buf, (size_t)rc) != 0) {
                goto cleanup;
            }

            last_data_ms = monotonic_time_ms();
            if (last_data_ms < 0) {
                goto cleanup;
            }
        }

        if (!stdin_ready) {
            continue;
        }

        {
            int stdin_result;

            stdin_result = stdin_read_ready(stdin_buf, sizeof(stdin_buf),
                                            stdin_line, &stdin_line_len,
                                            &stdin_line_overflow, &watch_stdin);
            if (stdin_result < 0) {
                goto cleanup;
            }
            if (stdin_result > 0) {
                result = STREAM_QUIT;
                goto cleanup;
            }
        }
    }

cleanup:
    if (use_icy) {
        icy_free(&demuxer);
    }

    return result;
}
