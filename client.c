#include "sikradio.h"

#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: provides the original URL string chosen by argument parsing.
 * - url_t: stores the parsed form of that URL for early validation.
 * - conn_t: carries the temporary network connection opened in this step.
 */

/* Minimal client entry point for the current roadmap step.
 * At this stage it validates the configured URL, opens and closes one
 * TCP connection, reports errors to stderr, and frees temporary state
 * before returning. */
int client_run(const config_t *config)
{
    char errbuf[ERRBUF_SIZE];
    conn_t conn;
    url_t url;

    if (config == NULL) {
        write_stderr_line("internal error: missing client configuration");
        return 1;
    }

    memset(errbuf, 0, sizeof(errbuf));
    memset(&conn, 0, sizeof(conn));
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

    conn_close(&conn);
    url_free(&url);
    return 0;
}
