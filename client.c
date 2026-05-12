#include "sikradio.h"

#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: provides the original URL string chosen by argument parsing.
 * - url_t: stores the parsed form of that URL for early validation.
 */

/* Minimal client entry point for the current roadmap step.
 * At this stage it only validates the configured URL, reports parsing
 * errors to stderr, and frees the parsed structure before returning. */
int client_run(const config_t *config)
{
    char errbuf[ERRBUF_SIZE];
    url_t url;

    if (config == NULL) {
        write_stderr_line("internal error: missing client configuration");
        return 1;
    }

    memset(errbuf, 0, sizeof(errbuf));
    memset(&url, 0, sizeof(url));

    if (url_parse(config->url, &url, errbuf, sizeof(errbuf)) != 0) {
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("invalid URL");
        }
        return 1;
    }

    url_free(&url);
    return 0;
}
