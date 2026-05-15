#define _POSIX_C_SOURCE 200112L

#include "sikradio.h"

#include <errno.h>
#include <poll.h>
#include <openssl/ssl.h>

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

stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config)
{
    unsigned char buf[IO_BUFFER_SIZE];
    icy_demuxer_t demuxer;
    stream_result_t result;
    int use_icy;

    if (conn == NULL || conn->fd < 0 || config == NULL) {
        return STREAM_ERROR;
    }

    result = STREAM_ERROR;
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

    for (;;) {
        ssize_t rc;

        if (!conn_has_pending_data(conn)) {
            struct pollfd pfd;
            int poll_rc;

            pfd.fd = conn->fd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            do {
                poll_rc = poll(&pfd, 1, config->timeout_ms);
            } while (poll_rc < 0 && errno == EINTR);

            if (poll_rc < 0) {
                goto cleanup;
            }
            if (poll_rc == 0) {
                result = STREAM_TIMEOUT;
                goto cleanup;
            }
            if (pfd.revents == 0) {
                continue;
            }
            if ((pfd.revents & POLLNVAL) != 0) {
                goto cleanup;
            }
        }

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
    }

cleanup:
    if (use_icy) {
        icy_free(&demuxer);
    }

    return result;
}
