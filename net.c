#define _POSIX_C_SOURCE 200112L

#include "sikradio.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Structures used from sikradio.h:
 * - conn_t: stores the socket descriptor and future TLS state managed here.
 * - url_t: provides the parsed host, port, and scheme for connection setup.
 * - ip_mode_t: selects IPv4, IPv6, or automatic address resolution.
 */

/* Helper for conn_open() and conn_close():
 * resets the connection structure to a predictable empty state. */
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

/* Helper shared across this module:
 * stores a short human-readable error message in the caller buffer. */
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

/* Helper for conn_open():
 * translates the project IP mode into the socket family used by getaddrinfo(). */
static int socket_family_for_mode(ip_mode_t ip_mode)
{
    switch (ip_mode) {
    case IP_MODE_IPV4:
        return AF_INET;
    case IP_MODE_IPV6:
        return AF_INET6;
    case IP_MODE_AUTO:
    default:
        return AF_UNSPEC;
    }
}

/* Helper for conn_open():
 * formats a numeric IPv4 or IPv6 endpoint for diagnostic logging. */
static int format_endpoint(const struct sockaddr *addr, socklen_t addrlen,
                           char *buf, size_t buflen)
{
    char host[128];
    char serv[32];
    int rc;

    if (addr == NULL || buf == NULL || buflen == 0) {
        return -1;
    }

    rc = getnameinfo(addr, addrlen, host, sizeof(host), serv, sizeof(serv),
                     NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        return -1;
    }

    if (addr->sa_family == AF_INET6) {
        snprintf(buf, buflen, "[%s]:%s", host, serv);
    } else {
        snprintf(buf, buflen, "%s:%s", host, serv);
    }

    return 0;
}

/* Public connection opener used by client_run() and later HTTP code.
 * At this roadmap step it resolves the host, tries TCP addresses in order,
 * logs the selected endpoint, and rejects HTTPS until TLS is implemented. */
int conn_open(conn_t *conn, const url_t *url, ip_mode_t ip_mode, char *errbuf, size_t errlen)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *current;
    int last_errno;

    if (conn == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing connection output");
        return -1;
    }

    conn_reset(conn);
    set_errbuf(errbuf, errlen, NULL);

    if (url == NULL || url->host == NULL || url->port == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid connection target");
        return -1;
    }

    if (url->use_tls) {
        set_errbuf(errbuf, errlen, "HTTPS not implemented yet");
        return -1;
    }

    log_timestamp();
    log_msg(LOG_COMMUNICATION, "resolving name %s", url->host);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = socket_family_for_mode(ip_mode);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    result = NULL;
    if (getaddrinfo(url->host, url->port, &hints, &result) != 0) {
        char message[ERRBUF_SIZE];
        snprintf(message, sizeof(message), "resolving name %s failed", url->host);
        set_errbuf(errbuf, errlen, message);
        return -1;
    }

    last_errno = 0;
    for (current = result; current != NULL; current = current->ai_next) {
        int fd;

        fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0) {
            last_errno = errno;
            continue;
        }

        if (connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            char endpoint[ERRBUF_SIZE];

            conn->fd = fd;
            conn->use_tls = url->use_tls;
            if (format_endpoint(current->ai_addr, current->ai_addrlen,
                                endpoint, sizeof(endpoint)) == 0) {
                log_msg(LOG_COMMUNICATION, "connecting to server %s", endpoint);
            } else {
                log_msg(LOG_COMMUNICATION, "connecting to server %s:%s",
                        url->host, url->port);
            }
            freeaddrinfo(result);
            return 0;
        }

        last_errno = errno;
        close(fd);
    }

    freeaddrinfo(result);
    if (last_errno != 0) {
        char message[ERRBUF_SIZE];
        snprintf(message, sizeof(message), "connecting to server failed: %s",
                 strerror(last_errno));
        set_errbuf(errbuf, errlen, message);
        errno = last_errno;
    } else {
        set_errbuf(errbuf, errlen, "connecting to server failed");
    }
    return -1;
}

/* Public read wrapper for the connection layer.
 * It validates the connection handle and then reads raw bytes from the socket. */
ssize_t conn_read(conn_t *conn, unsigned char *buf, size_t len)
{
    if (conn == NULL || conn->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && len != 0) {
        errno = EINVAL;
        return -1;
    }

    return read(conn->fd, buf, len);
}

/* Public write helper for the connection layer.
 * It guarantees that all requested bytes are written unless a real error
 * occurs, hiding partial writes and EINTR handling from callers. */
int conn_write_all(conn_t *conn, const unsigned char *buf, size_t len)
{
    size_t written = 0;

    if (conn == NULL || conn->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && len != 0) {
        errno = EINVAL;
        return -1;
    }

    while (written < len) {
        ssize_t rc = write(conn->fd, buf + written, len - written);
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

/* Public accessor used by modules that need to poll the active connection.
 * It returns the socket descriptor or -1 when the handle is missing. */
int conn_fd(const conn_t *conn)
{
    if (conn == NULL) {
        return -1;
    }

    return conn->fd;
}

/* Public cleanup function paired with conn_open():
 * closes the socket when it is open and resets the structure afterwards. */
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
