#include "sikradio.h"

stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config)
{
    unsigned char buf[IO_BUFFER_SIZE];
    icy_demuxer_t demuxer;
    int use_icy;

    (void)config;

    if (conn == NULL) {
        return STREAM_ERROR;
    }

    use_icy = 0;
    if (metaint > 0) {
        use_icy = 1;
        demuxer.metadata_buf = NULL;
        demuxer.metadata_cap = 0;

        if (icy_init(&demuxer, (size_t)metaint) != 0) {
            return STREAM_ERROR;
        }

        if (body_prefix_len > 0 && icy_feed(&demuxer, body_prefix, body_prefix_len) != 0) {
            icy_free(&demuxer);
            return STREAM_ERROR;
        }
    } else if (body_prefix_len > 0 && write_stdout_audio(body_prefix, body_prefix_len) != 0) {
        return STREAM_ERROR;
    }

    for (;;) {
        ssize_t rc = conn_read(conn, buf, sizeof(buf));

        if (rc < 0) {
            if (use_icy) {
                icy_free(&demuxer);
            }
            return STREAM_ERROR;
        }
        if (rc == 0) {
            if (use_icy) {
                icy_free(&demuxer);
            }
            return STREAM_SERVER_CLOSED;
        }
        if (use_icy) {
            if (icy_feed(&demuxer, buf, (size_t)rc) != 0) {
                icy_free(&demuxer);
                return STREAM_ERROR;
            }
        } else if (write_stdout_audio(buf, (size_t)rc) != 0) {
            return STREAM_ERROR;
        }
    }
}
