#include "sikradio.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structures used from sikradio.h:
 * - url_t: stores the parsed URL components owned by this module.
 * - url_scheme_t: distinguishes between plain HTTP and HTTPS defaults.
 */

/* Helper for url_parse(), url_resolve_redirect(), and url_free():
 * resets the structure to a predictable empty state. */
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

/* Helper for url_parse() and url_resolve_redirect():
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

/* Helper shared across this module: duplicates a full NUL-terminated string
 * into heap storage owned by the resulting url_t. */
static char *duplicate_string(const char *text)
{
    size_t len;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    len = strlen(text) + 1;
    copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, len);
    return copy;
}

/* Helper shared across this module: duplicates a byte range and appends
 * a terminating NUL so URL components can be stored independently. */
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

/* Helper for url_parse() and url_resolve_redirect():
 * returns the default port associated with the chosen scheme. */
static const char *default_port_for_scheme(url_scheme_t scheme)
{
    return scheme == URL_SCHEME_HTTPS ? "443" : "80";
}

/* Helper for url_parse() and redirect handling:
 * rejects spaces and control characters that would make the URL invalid
 * or ambiguous for later HTTP processing. */
static int contains_space_or_control(const char *start, const char *end)
{
    const unsigned char *p = (const unsigned char *)start;
    const unsigned char *limit = (const unsigned char *)end;

    while (p < limit) {
        if (isspace(*p) || iscntrl(*p)) {
            return 1;
        }
        ++p;
    }

    return 0;
}

/* Helper for url_parse(): identifies the supported URL scheme and returns
 * the pointer to the authority part that follows "http://" or "https://". */
static int parse_scheme_prefix(const char *text, url_scheme_t *scheme,
                               const char **authority_start,
                               char *errbuf, size_t errlen)
{
    if (text == NULL || scheme == NULL || authority_start == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid URL parser state");
        return -1;
    }

    if (strncmp(text, "http://", 7) == 0) {
        *scheme = URL_SCHEME_HTTP;
        *authority_start = text + 7;
        return 0;
    }

    if (strncmp(text, "https://", 8) == 0) {
        *scheme = URL_SCHEME_HTTPS;
        *authority_start = text + 8;
        return 0;
    }

    set_errbuf(errbuf, errlen, "invalid URL scheme");
    return -1;
}

/* Helper for url_parse(): finds where the authority part ends and the
 * request target path/query begins. It treats both '/' and '?' as the start
 * of the request target. */
static const char *find_authority_end(const char *start, const char *end)
{
    const char *p = start;

    while (p < end) {
        if (*p == '/' || *p == '?') {
            return p;
        }
        ++p;
    }

    return end;
}

/* Helper for url_parse(): validates the hostname syntax accepted by this
 * minimal implementation. It rejects empty hosts, unsupported IPv6-literal
 * syntax, userinfo, and whitespace/control characters. */
static int validate_host_part(const char *host, size_t host_len,
                              char *errbuf, size_t errlen)
{
    size_t i;

    if (host == NULL || host_len == 0) {
        set_errbuf(errbuf, errlen, "missing URL host");
        return -1;
    }

    for (i = 0; i < host_len; ++i) {
        unsigned char ch = (unsigned char)host[i];

        if (isspace(ch) || iscntrl(ch) || ch == '/' || ch == '?' || ch == '#' ||
            ch == '@' || ch == '[' || ch == ']' || ch == ':') {
            set_errbuf(errbuf, errlen, "invalid URL host");
            return -1;
        }
    }

    return 0;
}

/* Helper for url_parse(): validates an explicit numeric port.
 * The roadmap only needs host:port forms, so this parser accepts decimal
 * ports in the TCP range 1..65535. */
static int validate_port_part(const char *port, size_t port_len,
                              char *errbuf, size_t errlen)
{
    size_t i;
    unsigned long value = 0;

    if (port == NULL || port_len == 0) {
        set_errbuf(errbuf, errlen, "missing URL port");
        return -1;
    }

    for (i = 0; i < port_len; ++i) {
        unsigned char ch = (unsigned char)port[i];

        if (!isdigit(ch)) {
            set_errbuf(errbuf, errlen, "invalid URL port");
            return -1;
        }

        value = value * 10U + (unsigned long)(ch - '0');
        if (value > 65535U) {
            set_errbuf(errbuf, errlen, "invalid URL port");
            return -1;
        }
    }

    if (value == 0U) {
        set_errbuf(errbuf, errlen, "invalid URL port");
        return -1;
    }

    return 0;
}

/* Helper for url_parse(): creates the final path_query string.
 * If the URL has no explicit path, it produces "/". If the authority is
 * followed directly by a query, it normalizes it to "/?query". */
static int build_path_query(const char *path_start, const char *end,
                            char **path_query_out, char *errbuf, size_t errlen)
{
    size_t len;
    char *path_query;

    if (path_query_out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing path output");
        return -1;
    }

    if (path_start == NULL || path_start >= end) {
        path_query = duplicate_string("/");
        if (path_query == NULL) {
            set_errbuf(errbuf, errlen, "memory allocation failed");
            return -1;
        }

        *path_query_out = path_query;
        return 0;
    }

    if (contains_space_or_control(path_start, end)) {
        set_errbuf(errbuf, errlen, "invalid URL path");
        return -1;
    }

    len = (size_t)(end - path_start);
    if (*path_start == '/') {
        path_query = duplicate_range(path_start, len);
    } else if (*path_start == '?') {
        path_query = malloc(len + 2);
        if (path_query != NULL) {
            path_query[0] = '/';
            memcpy(path_query + 1, path_start, len);
            path_query[len + 1] = '\0';
        }
    } else {
        set_errbuf(errbuf, errlen, "invalid URL path");
        return -1;
    }

    if (path_query == NULL) {
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    *path_query_out = path_query;
    return 0;
}

/* Helper for url_parse(): splits the authority into host and optional port,
 * applies default ports when needed, and stores the final strings in parsed. */
static int parse_authority_parts(const char *authority_start, const char *authority_end,
                                 url_t *parsed, char *errbuf, size_t errlen)
{
    const char *colon = NULL;
    const char *p;
    size_t host_len;
    size_t port_len;

    if (authority_start == NULL || authority_end == NULL || parsed == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid authority state");
        return -1;
    }

    if (authority_start >= authority_end) {
        set_errbuf(errbuf, errlen, "missing URL host");
        return -1;
    }

    if (contains_space_or_control(authority_start, authority_end)) {
        set_errbuf(errbuf, errlen, "invalid URL host");
        return -1;
    }

    for (p = authority_start; p < authority_end; ++p) {
        if (*p == ':') {
            if (colon != NULL) {
                set_errbuf(errbuf, errlen, "invalid URL host");
                return -1;
            }
            colon = p;
        }
    }

    if (colon == NULL) {
        host_len = (size_t)(authority_end - authority_start);
        if (validate_host_part(authority_start, host_len, errbuf, errlen) != 0) {
            return -1;
        }

        parsed->host = duplicate_range(authority_start, host_len);
        parsed->port = duplicate_string(default_port_for_scheme(parsed->scheme));
    } else {
        host_len = (size_t)(colon - authority_start);
        port_len = (size_t)(authority_end - colon - 1);

        if (validate_host_part(authority_start, host_len, errbuf, errlen) != 0) {
            return -1;
        }
        if (validate_port_part(colon + 1, port_len, errbuf, errlen) != 0) {
            return -1;
        }

        parsed->host = duplicate_range(authority_start, host_len);
        parsed->port = duplicate_range(colon + 1, port_len);
    }

    if (parsed->host == NULL || parsed->port == NULL) {
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    return 0;
}

/* Helper for url_resolve_redirect(): copies base scheme/authority and pairs
 * them with a newly built path_query string. */
static int build_url_from_base(const url_t *base, const char *path_query,
                               url_t *out, char *errbuf, size_t errlen)
{
    url_t resolved;

    if (base == NULL || path_query == NULL || out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid redirect state");
        return -1;
    }

    url_reset(&resolved);
    resolved.scheme = base->scheme;
    resolved.use_tls = base->use_tls;
    resolved.host = duplicate_string(base->host);
    resolved.port = duplicate_string(base->port);
    resolved.path_query = duplicate_string(path_query);
    if (resolved.host == NULL || resolved.port == NULL || resolved.path_query == NULL) {
        url_free(&resolved);
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    *out = resolved;
    set_errbuf(errbuf, errlen, NULL);
    return 0;
}

/* Helper for url_resolve_redirect(): returns the directory prefix of the
 * current request target, ignoring any existing query string. */
static size_t current_directory_length(const char *path_query)
{
    const char *end;
    const char *last_slash = NULL;
    const char *p;

    if (path_query == NULL || path_query[0] == '\0') {
        return 1;
    }

    end = strchr(path_query, '?');
    if (end == NULL) {
        end = path_query + strlen(path_query);
    }

    for (p = path_query; p < end; ++p) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    if (last_slash == NULL) {
        return 1;
    }

    return (size_t)(last_slash - path_query + 1);
}

/* Helper for url_resolve_redirect(): creates the new path/query for relative
 * redirects. It handles absolute-path locations, query-only locations, and
 * plain relative names resolved against the current directory. */
static int build_redirect_path(const url_t *base, const char *location_start,
                               const char *location_end, char **path_out,
                               char *errbuf, size_t errlen)
{
    size_t location_len;
    size_t prefix_len;
    const char *query_pos;
    char *path;

    if (base == NULL || base->path_query == NULL || location_start == NULL ||
        location_end == NULL || path_out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid redirect state");
        return -1;
    }

    if (location_start >= location_end) {
        set_errbuf(errbuf, errlen, "invalid redirect location");
        return -1;
    }

    if (contains_space_or_control(location_start, location_end)) {
        set_errbuf(errbuf, errlen, "invalid redirect location");
        return -1;
    }

    location_len = (size_t)(location_end - location_start);
    if (location_start[0] == '/') {
        path = duplicate_range(location_start, location_len);
    } else if (location_start[0] == '?') {
        query_pos = strchr(base->path_query, '?');
        prefix_len = query_pos == NULL ? strlen(base->path_query)
                                       : (size_t)(query_pos - base->path_query);
        path = malloc(prefix_len + location_len + 1);
        if (path != NULL) {
            memcpy(path, base->path_query, prefix_len);
            memcpy(path + prefix_len, location_start, location_len);
            path[prefix_len + location_len] = '\0';
        }
    } else {
        prefix_len = current_directory_length(base->path_query);
        path = malloc(prefix_len + location_len + 1);
        if (path != NULL) {
            memcpy(path, base->path_query, prefix_len);
            memcpy(path + prefix_len, location_start, location_len);
            path[prefix_len + location_len] = '\0';
        }
    }

    if (path == NULL) {
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    *path_out = path;
    return 0;
}

/* Public URL parser used by client_run() and later HTTP redirect handling.
 * It accepts the minimal http:// and https:// forms required by the roadmap,
 * fills url_t with owned strings, and applies default ports and "/" when the
 * input omits them. */
int url_parse(const char *text, url_t *out, char *errbuf, size_t errlen)
{
    url_t parsed;
    url_scheme_t scheme;
    const char *authority_start;
    const char *authority_end;
    const char *parse_end;
    const char *fragment;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing URL output");
        return -1;
    }

    url_reset(&parsed);
    set_errbuf(errbuf, errlen, NULL);

    if (text == NULL || text[0] == '\0') {
        set_errbuf(errbuf, errlen, "empty URL");
        return -1;
    }

    if (parse_scheme_prefix(text, &scheme, &authority_start, errbuf, errlen) != 0) {
        return -1;
    }

    fragment = strchr(authority_start, '#');
    if (fragment == NULL) {
        parse_end = text + strlen(text);
    } else {
        parse_end = fragment;
    }
    if (authority_start >= parse_end) {
        set_errbuf(errbuf, errlen, "missing URL host");
        return -1;
    }

    parsed.scheme = scheme;
    parsed.use_tls = scheme == URL_SCHEME_HTTPS ? 1 : 0;

    authority_end = find_authority_end(authority_start, parse_end);
    if (parse_authority_parts(authority_start, authority_end, &parsed,
                              errbuf, errlen) != 0) {
        url_free(&parsed);
        return -1;
    }

    if (build_path_query(authority_end, parse_end, &parsed.path_query,
                         errbuf, errlen) != 0) {
        url_free(&parsed);
        return -1;
    }

    *out = parsed;
    set_errbuf(errbuf, errlen, NULL);
    return 0;
}

/* Public redirect resolver used later by the HTTP client.
 * It supports absolute Location values, absolute-path redirects relative to
 * the same authority, and plain relative targets resolved against the current
 * request directory. */
int url_resolve_redirect(const url_t *base, const char *location, url_t *out,
                         char *errbuf, size_t errlen)
{
    const char *location_end;
    char *path_query;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing redirect output");
        return -1;
    }

    if (base == NULL || base->host == NULL || base->port == NULL ||
        base->path_query == NULL || location == NULL) {
        set_errbuf(errbuf, errlen, "invalid redirect location");
        return -1;
    }

    if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
        return url_parse(location, out, errbuf, errlen);
    }

    location_end = strchr(location, '#');
    if (location_end == NULL) {
        location_end = location + strlen(location);
    }

    path_query = NULL;
    if (build_redirect_path(base, location, location_end, &path_query, errbuf, errlen) != 0) {
        return -1;
    }

    if (build_url_from_base(base, path_query, out, errbuf, errlen) != 0) {
        free(path_query);
        return -1;
    }

    free(path_query);
    return 0;
}

/* Public cleanup function paired with url_parse() and url_resolve_redirect():
 * releases all dynamically owned URL components and resets the structure. */
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
