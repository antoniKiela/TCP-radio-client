#include "sikradio.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

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

int http_build_request(const url_t *url, int want_metadata, const char *cookie,
                       unsigned char **out, size_t *out_len)
{
    (void)url;
    (void)want_metadata;
    (void)cookie;

    if (out != NULL) {
        *out = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }

    return -1;
}

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

int http_is_redirect(int status_code)
{
    return status_code == 301 || status_code == 302 || status_code == 303 ||
           status_code == 307 || status_code == 308;
}

int http_extract_cookie(const http_response_t *response, char **cookie_in_out)
{
    (void)response;
    (void)cookie_in_out;

    return 0;
}

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
