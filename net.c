#include "sikradio.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static void conn_reset(conn_t *conn)
{
    if (conn == NULL) {
        return;
    }

    conn->fd = -1;
    conn->use_tls = 0;
    conn->ssl_ctx = NULL;
    conn->ssl = NULL;
}

static void set_errbuf(char *errbuf, size_t errlen, const char *message)
{
    if (errbuf == NULL || errlen == 0) {
        return;
    }

    if (message == NULL) {
        errbuf[0] = '\0';
        return;
    }

    snprintf(errbuf, errlen, "%s", message);
}

int conn_open(conn_t *conn, const url_t *url, ip_mode_t ip_mode, char *errbuf, size_t errlen)
{
    (void)url;
    (void)ip_mode;

    if (conn == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing connection output");
        return -1;
    }

    conn_reset(conn);
    set_errbuf(errbuf, errlen, "network layer not implemented yet");
    return -1;
}

ssize_t conn_read(conn_t *conn, unsigned char *buf, size_t len)
{
    (void)conn;
    (void)buf;
    (void)len;

    errno = ENOSYS;
    return -1;
}

int conn_write_all(conn_t *conn, const unsigned char *buf, size_t len)
{
    (void)conn;
    (void)buf;
    (void)len;

    errno = ENOSYS;
    return -1;
}

int conn_fd(const conn_t *conn)
{
    if (conn == NULL) {
        return -1;
    }

    return conn->fd;
}

void conn_close(conn_t *conn)
{
    if (conn == NULL) {
        return;
    }

    if (conn->fd >= 0) {
        close(conn->fd);
    }

    conn_reset(conn);
}
