#include "sikradio.h"

#include <signal.h>
#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: stores the parsed command-line configuration used to
 *   initialize logging and to drive the main client logic.
 */

/* Program entry point coordinating the high-level startup flow.
 * It installs SIGPIPE handling, parses arguments, initializes the logging
 * threshold, runs the client, and finally releases configuration resources. */
int main(int argc, char **argv)
{
    config_t config;
    char errbuf[ERRBUF_SIZE];
    int status;

    memset(&config, 0, sizeof(config));
    memset(errbuf, 0, sizeof(errbuf));

    signal(SIGPIPE, SIG_IGN);

    if (config_parse(argc, argv, &config, errbuf, sizeof(errbuf)) != 0) {
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("invalid arguments");
        }
        config_free(&config);
        return 1;
    }

    log_set_verbosity(config.verbosity);
    status = client_run(&config);
    config_free(&config);

    return status;
}
