#include "sikradio.h"

#include <ctype.h>
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

/* Helper shared across this module:
 * duplicates a byte range and appends a terminating NUL for owned strings. */
static char *duplicate_range(const char *start, size_t len)
{
    char *copy;

    copy = malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

/* Helper for http_read_response():
 * finds the first "\r\n\r\n" marker and returns the index of its first
 * carriage return, or `(size_t)-1` when the full header block has not arrived. */
static size_t find_header_end(const unsigned char *buf, size_t len)
{
    size_t i;

    if (buf == NULL || len < 4) {
        return (size_t)-1;
    }

    for (i = 0; i + 3 < len; ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i;
        }
    }

    return (size_t)-1;
}

/* Helper for http_read_response():
 * parses the status line accepted by the project and extracts the numeric
 * status code together with the ICY marker. */
static int parse_status_line(const char *line, int *status_code, int *is_icy,
                             char *errbuf, size_t errlen)
{
    const char *code_start;
    int code;

    if (line == NULL || status_code == NULL || is_icy == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid HTTP status state");
        return -1;
    }

    if (strncmp(line, "ICY ", 4) == 0) {
        code_start = line + 4;
        *is_icy = 1;
    } else if (strncmp(line, "HTTP/1.", 7) == 0 &&
               isdigit((unsigned char)line[7]) && line[8] == ' ') {
        code_start = line + 9;
        *is_icy = 0;
    } else {
        set_errbuf(errbuf, errlen, "invalid HTTP status line");
        return -1;
    }

    if (!isdigit((unsigned char)code_start[0]) ||
        !isdigit((unsigned char)code_start[1]) ||
        !isdigit((unsigned char)code_start[2])) {
        set_errbuf(errbuf, errlen, "invalid HTTP status line");
        return -1;
    }

    if (code_start[3] != '\0' && code_start[3] != ' ' && code_start[3] != '\t') {
        set_errbuf(errbuf, errlen, "invalid HTTP status line");
        return -1;
    }

    code = (code_start[0] - '0') * 100 +
           (code_start[1] - '0') * 10 +
           (code_start[2] - '0');
    *status_code = code;
    return 0;
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
 * It reads through the end of HTTP/ICY headers, parses status and headers,
 * keeps any already received body bytes, and logs the parsed response. */
int http_read_response(conn_t *conn, int timeout_ms, http_response_t *out,
                       char *errbuf, size_t errlen)
{
    http_response_t parsed;
    unsigned char *raw;
    size_t raw_len;
    size_t header_end;
    size_t body_start;
    char *header_limit;
    char *status_line_end;
    char *line;
    char *next_line;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing HTTP response output");
        return -1;
    }

    if (conn == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing HTTP connection");
        return -1;
    }

    (void)timeout_ms;
    http_response_reset(&parsed);
    http_response_reset(out);
    set_errbuf(errbuf, errlen, NULL);

    raw = malloc(MAX_HEADER_BYTES + 1);
    if (raw == NULL) {
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    raw_len = 0;
    header_end = (size_t)-1;
    while (header_end == (size_t)-1) {
        ssize_t rc;

        if (raw_len == MAX_HEADER_BYTES) {
            free(raw);
            set_errbuf(errbuf, errlen, "HTTP headers too large");
            return -1;
        }

        rc = conn_read(conn, raw + raw_len, MAX_HEADER_BYTES - raw_len);
        if (rc < 0) {
            free(raw);
            set_errbuf(errbuf, errlen, "reading HTTP response failed");
            return -1;
        }
        if (rc == 0) {
            free(raw);
            set_errbuf(errbuf, errlen, "connection closed before complete HTTP headers");
            return -1;
        }

        raw_len += (size_t)rc;
        header_end = find_header_end(raw, raw_len);
    }

    raw[raw_len] = '\0';
    body_start = header_end + 4;
    if (raw_len > body_start) {
        parsed.body_prefix_len = raw_len - body_start;
        parsed.body_prefix = malloc(parsed.body_prefix_len);
        if (parsed.body_prefix == NULL) {
            free(raw);
            set_errbuf(errbuf, errlen, "memory allocation failed");
            return -1;
        }
        memcpy(parsed.body_prefix, raw + body_start, parsed.body_prefix_len);
    }

    header_limit = (char *)raw + header_end;
    status_line_end = strstr((char *)raw, "\r\n");
    if (status_line_end == NULL || status_line_end > header_limit) {
        free(raw);
        http_response_free(&parsed);
        set_errbuf(errbuf, errlen, "invalid HTTP response headers");
        return -1;
    }

    parsed.status_line = duplicate_range((char *)raw,
                                         (size_t)(status_line_end - (char *)raw));
    if (parsed.status_line == NULL) {
        free(raw);
        http_response_free(&parsed);
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    if (parse_status_line(parsed.status_line, &parsed.status_code, &parsed.is_icy,
                          errbuf, errlen) != 0) {
        free(raw);
        http_response_free(&parsed);
        return -1;
    }

    line = status_line_end + 2;
    while (line < header_limit) {
        char *colon;
        char *value_start;
        char *value_end;
        header_t *new_headers;

        next_line = strstr(line, "\r\n");
        if (next_line == NULL || next_line > header_limit) {
            free(raw);
            http_response_free(&parsed);
            set_errbuf(errbuf, errlen, "invalid HTTP response headers");
            return -1;
        }

        *next_line = '\0';
        colon = strchr(line, ':');
        if (colon == NULL || colon == line) {
            free(raw);
            http_response_free(&parsed);
            set_errbuf(errbuf, errlen, "invalid HTTP header line");
            return -1;
        }

        value_start = colon + 1;
        while (*value_start == ' ' || *value_start == '\t') {
            ++value_start;
        }

        value_end = next_line;
        while (value_end > value_start &&
               (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            --value_end;
        }

        new_headers = realloc(parsed.headers, (parsed.header_count + 1) * sizeof(header_t));
        if (new_headers == NULL) {
            free(raw);
            http_response_free(&parsed);
            set_errbuf(errbuf, errlen, "memory allocation failed");
            return -1;
        }
        parsed.headers = new_headers;
        parsed.headers[parsed.header_count].name = NULL;
        parsed.headers[parsed.header_count].value = NULL;

        parsed.headers[parsed.header_count].name =
            duplicate_range(line, (size_t)(colon - line));
        parsed.headers[parsed.header_count].value =
            duplicate_range(value_start, (size_t)(value_end - value_start));
        if (parsed.headers[parsed.header_count].name == NULL ||
            parsed.headers[parsed.header_count].value == NULL) {
            free(raw);
            http_response_free(&parsed);
            set_errbuf(errbuf, errlen, "memory allocation failed");
            return -1;
        }

        parsed.header_count += 1;
        line = next_line + 2;
    }

    free(raw);
    log_msg(LOG_COMMUNICATION, "%s", parsed.status_line);
    for (size_t i = 0; i < parsed.header_count; ++i) {
        log_msg(LOG_COMMUNICATION, "%s: %s",
                parsed.headers[i].name, parsed.headers[i].value);
    }
    log_msg(LOG_COMMUNICATION, "");
    log_msg(LOG_COMMUNICATION, "");

    *out = parsed;
    return 0;
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

/* Public cookie extraction helper used by redirect handling.
 * It keeps only the first Set-Cookie value fragment up to the first ';'
 * and replaces the caller-owned cookie string when one is present. */
int http_extract_cookie(const http_response_t *response, char **cookie_in_out)
{
    const char *set_cookie;
    const char *cookie_end;
    char *cookie_copy;

    if (cookie_in_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    set_cookie = http_header_get(response, "Set-Cookie");
    if (set_cookie == NULL) {
        return 0;
    }

    cookie_end = strchr(set_cookie, ';');
    if (cookie_end == NULL) {
        cookie_end = set_cookie + strlen(set_cookie);
    }

    cookie_copy = duplicate_range(set_cookie, (size_t)(cookie_end - set_cookie));
    if (cookie_copy == NULL) {
        errno = ENOMEM;
        return -1;
    }

    free(*cookie_in_out);
    *cookie_in_out = cookie_copy;
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
