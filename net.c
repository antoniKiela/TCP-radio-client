#define _POSIX_C_SOURCE 200112L

#include "sikradio.h"

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Structures used from sikradio.h:
 * - conn_t: stores the socket descriptor and TLS state managed here.
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

/* Helper for conn_open():
 * creates the TLS context and session, binds them to the connected socket,
 * applies SNI, and completes the client handshake. */
static int tls_connect_socket(int fd, const url_t *url,
                              SSL_CTX **ssl_ctx_out, SSL **ssl_out,
                              char *errbuf, size_t errlen)
{
    SSL_CTX *ssl_ctx;
    SSL *ssl;

    if (url == NULL || url->host == NULL || ssl_ctx_out == NULL || ssl_out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid TLS state");
        return -1;
    }

    *ssl_ctx_out = NULL;
    *ssl_out = NULL;
    ssl_ctx = NULL;
    ssl = NULL;

    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        set_errbuf(errbuf, errlen, "creating TLS context failed");
        return -1;
    }

    ssl = SSL_new(ssl_ctx);
    if (ssl == NULL) {
        set_errbuf(errbuf, errlen, "creating TLS session failed");
        SSL_CTX_free(ssl_ctx);
        return -1;
    }

    if (SSL_set_fd(ssl, fd) != 1) {
        set_errbuf(errbuf, errlen, "binding TLS session to socket failed");
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return -1;
    }

    if (SSL_set_tlsext_host_name(ssl, url->host) != 1) {
        set_errbuf(errbuf, errlen, "setting TLS SNI failed");
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return -1;
    }

    if (SSL_connect(ssl) != 1) {
        set_errbuf(errbuf, errlen, "TLS handshake failed");
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return -1;
    }

    *ssl_ctx_out = ssl_ctx;
    *ssl_out = ssl;
    set_errbuf(errbuf, errlen, NULL);
    return 0;
}

/* Public connection opener used by client_run() and later HTTP code.
 * At this roadmap step it resolves the host, tries TCP addresses in order,
 * logs the selected endpoint, and optionally upgrades the socket to TLS. */
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
            SSL_CTX *ssl_ctx;
            SSL *ssl;

            if (format_endpoint(current->ai_addr, current->ai_addrlen,
                                endpoint, sizeof(endpoint)) == 0) {
                log_msg(LOG_COMMUNICATION, "connecting to server %s", endpoint);
            } else {
                log_msg(LOG_COMMUNICATION, "connecting to server %s:%s",
                        url->host, url->port);
            }

            ssl_ctx = NULL;
            ssl = NULL;
            if (url->use_tls &&
                tls_connect_socket(fd, url, &ssl_ctx, &ssl, errbuf, errlen) != 0) {
                close(fd);
                freeaddrinfo(result);
                return -1;
            }

            conn->fd = fd;
            conn->use_tls = url->use_tls;
            conn->ssl_ctx = ssl_ctx;
            conn->ssl = ssl;
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
 * It validates the connection handle and then reads raw bytes either from the
 * plain socket or from the negotiated TLS session. */
ssize_t conn_read(conn_t *conn, unsigned char *buf, size_t len)
{
    SSL *ssl;

    if (conn == NULL || conn->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && len != 0) {
        errno = EINVAL;
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    if (!conn->use_tls) {
        return read(conn->fd, buf, len);
    }

    ssl = (SSL *)conn->ssl;
    if (ssl == NULL) {
        errno = EIO;
        return -1;
    }

    for (;;) {
        int rc;
        int chunk = len > (size_t)INT_MAX ? INT_MAX : (int)len;
        int ssl_error;

        rc = SSL_read(ssl, buf, chunk);
        if (rc > 0) {
            return (ssize_t)rc;
        }

        ssl_error = SSL_get_error(ssl, rc);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        if (ssl_error == SSL_ERROR_SYSCALL && rc == 0) {
            return 0;
        }

        errno = EIO;
        return -1;
    }
}

/* Public write helper for the connection layer.
 * It guarantees that all requested bytes are written unless a real error
 * occurs, hiding partial writes and EINTR handling from callers for both
 * plain sockets and TLS sessions. */
int conn_write_all(conn_t *conn, const unsigned char *buf, size_t len)
{
    size_t written = 0;
    SSL *ssl;

    if (conn == NULL || conn->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (buf == NULL && len != 0) {
        errno = EINVAL;
        return -1;
    }

    if (!conn->use_tls) {
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

    ssl = (SSL *)conn->ssl;
    if (ssl == NULL) {
        errno = EIO;
        return -1;
    }

    while (written < len) {
        int rc;
        int ssl_error;
        size_t remaining = len - written;
        int chunk = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;

        rc = SSL_write(ssl, buf + written, chunk);
        if (rc > 0) {
            written += (size_t)rc;
            continue;
        }

        ssl_error = SSL_get_error(ssl, rc);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }

        errno = EIO;
        return -1;
    }

    return 0;
}

/* Public cleanup function paired with conn_open():
 * closes the TLS session and socket when they are open, then resets the
 * structure afterwards. */
void conn_close(conn_t *conn)
{
    SSL *ssl;
    SSL_CTX *ssl_ctx;

    if (conn == NULL) {
        return;
    }

    ssl = (SSL *)conn->ssl;
    ssl_ctx = (SSL_CTX *)conn->ssl_ctx;
    if (ssl != NULL) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ssl_ctx != NULL) {
        SSL_CTX_free(ssl_ctx);
    }

    if (conn->fd >= 0) {
        close(conn->fd);
    }

    conn_reset(conn);
}
