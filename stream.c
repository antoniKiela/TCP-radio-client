#include "sikradio.h"

stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config)
{
    (void)conn;
    (void)body_prefix;
    (void)body_prefix_len;
    (void)metaint;
    (void)config;

    log_msg(2, "stream receiver not implemented yet");
    return STREAM_ERROR;
}
