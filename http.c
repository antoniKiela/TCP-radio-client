#include "sikradio.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Structures used from sikradio.h:
 * - url_t: provides the request target and host used to build HTTP GET.
 * - http_response_t: stores parsed response state for later roadmap steps.
 * - conn_t: will carry the active connection when response parsing is added.
 */

/* Helper for http_read_response() and http_response_free():
 * resets the response structure to a predictable empty state. */
static void http_response_reset(http_response_t *response)
{
    if (response == NULL) {
        return;
    }

    response->status_line = NULL;
    response->status_code = 0;
    response->is_icy = 0;
    response->headers = NULL;
    response->header_count = 0;
    response->body_prefix = NULL;
    response->body_prefix_len = 0;
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

/* Public request builder used by client_run() and later redirect handling.
 * It creates the exact HTTP GET bytes required by the roadmap, including
 * optional Cookie and Icy-MetaData headers in the required order. */
int http_build_request(const url_t *url, int want_metadata, const char *cookie,
                       unsigned char **out, size_t *out_len)
{
    size_t path_len;
    size_t host_len;
    size_t cookie_len;
    size_t request_len;
    size_t pos;
    unsigned char *request;

    if (out != NULL) {
        *out = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }

    if (url == NULL || url->host == NULL || url->path_query == NULL ||
        out == NULL || out_len == NULL) {
        errno = EINVAL;
        return -1;
    }

    path_len = strlen(url->path_query);
    host_len = strlen(url->host);
    cookie_len = cookie == NULL ? 0 : strlen(cookie);

    request_len = 4 + path_len + 11 +
                  6 + host_len + 2 +
                  24 +
                  (cookie == NULL ? 0 : 8 + cookie_len + 2) +
                  (want_metadata ? 17 : 0) +
                  2;

    request = malloc(request_len + 1);
    if (request == NULL) {
        errno = ENOMEM;
        return -1;
    }

    pos = 0;
    memcpy(request + pos, "GET ", 4);
    pos += 4;
    memcpy(request + pos, url->path_query, path_len);
    pos += path_len;
    memcpy(request + pos, " HTTP/1.1\r\n", 11);
    pos += 11;

    memcpy(request + pos, "Host: ", 6);
    pos += 6;
    memcpy(request + pos, url->host, host_len);
    pos += host_len;
    memcpy(request + pos, "\r\n", 2);
    pos += 2;

    memcpy(request + pos, "Connection: Keep-Alive\r\n", 24);
    pos += 24;

    if (cookie != NULL) {
        memcpy(request + pos, "Cookie: ", 8);
        pos += 8;
        memcpy(request + pos, cookie, cookie_len);
        pos += cookie_len;
        memcpy(request + pos, "\r\n", 2);
        pos += 2;
    }

    if (want_metadata) {
        memcpy(request + pos, "Icy-MetaData: 1\r\n", 17);
        pos += 17;
    }

    memcpy(request + pos, "\r\n", 2);
    pos += 2;
    request[pos] = '\0';

    *out = request;
    *out_len = pos;
    return 0;
}

/* Public response reader placeholder for the current roadmap state.
 * It resets the output structure and reports that parsing is not implemented
 * yet, which will change in the next step. */
int http_read_response(conn_t *conn, int timeout_ms, http_response_t *out,
                       char *errbuf, size_t errlen)
{
    (void)conn;
    (void)timeout_ms;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing HTTP response output");
        return -1;
    }

    http_response_reset(out);
    set_errbuf(errbuf, errlen, "HTTP response parser not implemented yet");
    return -1;
}

/* Public header lookup helper used by later HTTP and ICY processing.
 * It performs a case-insensitive scan across parsed response headers. */
const char *http_header_get(const http_response_t *response, const char *name)
{
    size_t i;

    if (response == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < response->header_count; ++i) {
        if (response->headers[i].name != NULL &&
            strcasecmp(response->headers[i].name, name) == 0) {
            return response->headers[i].value;
        }
    }

    return NULL;
}

/* Public redirect classifier used by later client redirect logic.
 * It returns true for the HTTP redirect status codes accepted by the project. */
int http_is_redirect(int status_code)
{
    return status_code == 301 || status_code == 302 || status_code == 303 ||
           status_code == 307 || status_code == 308;
}

/* Public cookie extraction placeholder for a later roadmap step.
 * It currently keeps the signature stable but does not parse cookies yet. */
int http_extract_cookie(const http_response_t *response, char **cookie_in_out)
{
    (void)response;
    (void)cookie_in_out;

    return 0;
}

/* Public cleanup function paired with http_read_response():
 * frees all dynamically owned response fields and resets the structure. */
void http_response_free(http_response_t *response)
{
    size_t i;

    if (response == NULL) {
        return;
    }

    free(response->status_line);
    for (i = 0; i < response->header_count; ++i) {
        free(response->headers[i].name);
        free(response->headers[i].value);
    }
    free(response->headers);
    free(response->body_prefix);

    http_response_reset(response);
}
