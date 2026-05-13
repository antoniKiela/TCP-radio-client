#include "sikradio.h"

stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config)
{
    unsigned char buf[IO_BUFFER_SIZE];

    (void)metaint;
    (void)config;

    if (conn == NULL) {
        return STREAM_ERROR;
    }

    if (body_prefix_len > 0 && write_stdout_audio(body_prefix, body_prefix_len) != 0) {
        return STREAM_ERROR;
    }

    for (;;) {
        ssize_t rc = conn_read(conn, buf, sizeof(buf));

        if (rc < 0) {
            return STREAM_ERROR;
        }
        if (rc == 0) {
            return STREAM_SERVER_CLOSED;
        }
        if (write_stdout_audio(buf, (size_t)rc) != 0) {
            return STREAM_ERROR;
        }
    }
}
