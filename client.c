#include "sikradio.h"

#include <stdlib.h>
#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: provides the original URL string chosen by argument parsing.
 * - url_t: stores the parsed form of that URL for early validation.
 * - conn_t: carries the temporary network connection opened in this step.
 */

/* Minimal client entry point for the current roadmap step.
 * At this stage it validates the configured URL, opens one connection,
 * builds and sends a single HTTP GET request, reports errors to stderr,
 * and frees temporary state before returning. */
int client_run(const config_t *config)
{
    char errbuf[ERRBUF_SIZE];
    conn_t conn;
    http_response_t response;
    unsigned char *request;
    size_t request_len;
    url_t url;

    if (config == NULL) {
        write_stderr_line("internal error: missing client configuration");
        return 1;
    }

    memset(errbuf, 0, sizeof(errbuf));
    memset(&conn, 0, sizeof(conn));
    conn.fd = -1;
    memset(&response, 0, sizeof(response));
    request = NULL;
    request_len = 0;
    memset(&url, 0, sizeof(url));

    if (url_parse(config->url, &url, errbuf, sizeof(errbuf)) != 0) {
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("invalid URL");
        }
        return 1;
    }

    memset(errbuf, 0, sizeof(errbuf));
    if (conn_open(&conn, &url, config->ip_mode, errbuf, sizeof(errbuf)) != 0) {
        url_free(&url);
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("connection failed");
        }
        return 1;
    }

    if (http_build_request(&url, config->want_metadata, NULL, &request, &request_len) != 0) {
        conn_close(&conn);
        url_free(&url);
        write_stderr_line("building HTTP request failed");
        return 1;
    }

    if (config->verbosity >= LOG_COMMUNICATION) {
        write_stderr_bytes(request, request_len);
    }

    if (conn_write_all(&conn, request, request_len) != 0) {
        free(request);
        conn_close(&conn);
        url_free(&url);
        write_stderr_line("sending HTTP request failed");
        return 1;
    }

    free(request);
    memset(errbuf, 0, sizeof(errbuf));
    if (http_read_response(&conn, config->timeout_ms, &response, errbuf, sizeof(errbuf)) != 0) {
        conn_close(&conn);
        url_free(&url);
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("reading HTTP response failed");
        }
        return 1;
    }

    if (response.status_code != 200) {
        http_response_free(&response);
        conn_close(&conn);
        url_free(&url);
        return 1;
    }

    http_response_free(&response);
    conn_close(&conn);
    url_free(&url);
    return 0;
}
