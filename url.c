#include "sikradio.h"

#include <stdio.h>
#include <stdlib.h>

static void url_reset(url_t *url)
{
    if (url == NULL) {
        return;
    }

    url->scheme = URL_SCHEME_HTTP;
    url->host = NULL;
    url->port = NULL;
    url->path_query = NULL;
    url->use_tls = 0;
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

int url_parse(const char *text, url_t *out, char *errbuf, size_t errlen)
{
    (void)text;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing URL output");
        return -1;
    }

    url_reset(out);
    set_errbuf(errbuf, errlen, "URL parser not implemented yet");
    return -1;
}

int url_resolve_redirect(const url_t *base, const char *location, url_t *out,
                         char *errbuf, size_t errlen)
{
    (void)base;
    (void)location;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing redirect output");
        return -1;
    }

    url_reset(out);
    set_errbuf(errbuf, errlen, "redirect resolution not implemented yet");
    return -1;
}

void url_free(url_t *url)
{
    if (url == NULL) {
        return;
    }

    free(url->host);
    free(url->port);
    free(url->path_query);
    url_reset(url);
}
