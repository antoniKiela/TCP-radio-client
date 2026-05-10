#include "sikradio.h"

#include <stdio.h>
#include <stdlib.h>

/* Structures used from sikradio.h:
 * - config_t: stores the parsed runtime configuration and is the main data
 *   structure initialized, filled, and cleaned up by this module.
 */

/* Helper for config_parse() and config_free(): initializes the structure
 * to a known default state used by the whole program. */
static void config_init(config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->url = NULL;
    config->want_metadata = 0;
    config->timeout_ms = DEFAULT_TIMEOUT_MS;
    config->verbosity = DEFAULT_VERBOSITY;
    config->ip_mode = IP_MODE_AUTO;
}

/* Helper for config_parse(): stores a short human-readable error message
 * in the caller-provided buffer when argument parsing fails. */
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

/* Public parser used by main(): reads command-line arguments, validates
 * them, fills config_t, and reports user-facing parsing errors via errbuf.
 * It relies on config_init() for defaults and on helper functions such as
 * set_errbuf() to keep error handling consistent. */
int config_parse(int argc, char **argv, config_t *out, char *errbuf, size_t errlen)
{
    (void)argc;
    (void)argv;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing config output");
        return -1;
    }

    config_init(out);
    set_errbuf(errbuf, errlen, "config parser not implemented yet");
    return -1;
}

/* Public cleanup function paired with config_parse(): releases any dynamic
 * memory owned by config_t and then reinitializes the structure so it does
 * not keep stale pointers or partially parsed state. */
void config_free(config_t *config)
{
    if (config == NULL) {
        return;
    }

    free(config->url);
    config_init(config);
}
