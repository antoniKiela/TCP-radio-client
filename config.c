#include "sikradio.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: stores the parsed runtime configuration and is the main data
 *   structure initialized, filled, and cleaned up by this module.
 * - ip_mode_t: stores the final IP selection derived from the presence of
 *   -4 and -6 flags while parsing command-line options.
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

/* Helper for config_parse(): duplicates a string into dynamically allocated
 * storage so config_t can safely own parsed argument values. */
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

/* Helper for config_parse(): stores the most recent -u value.
 * Repeated -u options are allowed, so this function frees the previous URL
 * and replaces it with the last one seen by the parser. */
static int config_set_url(config_t *config, const char *value, char *errbuf, size_t errlen)
{
    char *copy;

    if (config == NULL || value == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid URL storage");
        return -1;
    }

    copy = duplicate_string(value);
    if (copy == NULL) {
        set_errbuf(errbuf, errlen, "memory allocation failed");
        return -1;
    }

    free(config->url);
    config->url = copy;
    return 0;
}

/* Helper for config_parse(): obtains the value belonging to an option that
 * accepts an argument. It supports both attached forms such as -t5000 and
 * separated forms such as -t 5000. */
static int consume_option_value(const char *arg, int *index, int argc, char **argv,
                                size_t value_offset, const char **value_out,
                                char *errbuf, size_t errlen, const char *option_name)
{
    if (arg == NULL || index == NULL || value_out == NULL || option_name == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid parser state");
        return -1;
    }

    if (arg[value_offset] != '\0') {
        *value_out = arg + value_offset;
        return 0;
    }

    if (*index + 1 >= argc || argv[*index + 1] == NULL || argv[*index + 1][0] == '-') {
        char message[ERRBUF_SIZE];
        snprintf(message, sizeof(message), "missing value for %s", option_name);
        set_errbuf(errbuf, errlen, message);
        return -1;
    }

    *index += 1;
    *value_out = argv[*index];
    return 0;
}

/* Helper for config_parse(): parses a strict base-10 integer and verifies
 * that it falls within the caller-provided range. It is shared by -t and -v
 * so numeric validation stays consistent across options. */
static int parse_bounded_int(const char *text, int min_value, int max_value, int *out,
                             const char *invalid_message, const char *range_message,
                             char *errbuf, size_t errlen)
{
    char *end = NULL;
    long value;

    if (text == NULL || out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid numeric parser state");
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0' || errno == ERANGE ||
        value < INT_MIN || value > INT_MAX) {
        set_errbuf(errbuf, errlen, invalid_message);
        return -1;
    }

    if (value < min_value || value > max_value) {
        set_errbuf(errbuf, errlen, range_message);
        return -1;
    }

    *out = (int)value;
    return 0;
}

/* Helper for config_parse(): converts the presence of -4 and -6 flags into
 * the final ip_mode_t value defined by the project rules. */
static void finalize_ip_mode(config_t *config, int saw_ipv4, int saw_ipv6)
{
    if (config == NULL) {
        return;
    }

    if (saw_ipv4 && saw_ipv6) {
        config->ip_mode = IP_MODE_AUTO;
    } else if (saw_ipv4) {
        config->ip_mode = IP_MODE_IPV4;
    } else if (saw_ipv6) {
        config->ip_mode = IP_MODE_IPV6;
    } else {
        config->ip_mode = IP_MODE_AUTO;
    }
}

/* Helper for config_parse(): parses one argv token that begins with '-'.
 * It supports grouped flag options like -mq, -6m, or -m46.
 *
 * Argument selection rules:
 * - flag-only options (-m, -4, -6, -q) are read one character at a time
 *   from the current token,
 * - value-taking options (-u, -t, -v) first try to use the rest of the same
 *   token as their value, for example -uhttp://x, -t3500, or -v4,
 * - if there is no attached suffix, the next argv element is consumed as the
 *   value, for example -u http://x or -t 3500,
 * - once a value-taking option is handled, parsing of the current token ends,
 *   because the suffix or the next argv element belongs entirely to that one
 *   option.
 *
 * Invalid cases handled here or by its helpers:
 * - the token does not start with '-' or is just "-",
 * - an unknown option letter appears,
 * - a value-taking option has no value attached and no usable next argv
 *   element,
 * - -t or -v receives a non-numeric value or a number outside the allowed
 *   range.
 *
 * Repeated options follow the project rules:
 * - repeated -u replaces the previously stored URL, so the last URL wins,
 * - repeated -t, -v, or -q overwrite the previous timeout/verbosity setting,
 * - repeated -m simply keeps metadata enabled,
 * - -4 and -6 are only remembered here; the final ip_mode_t is resolved later
 *   by finalize_ip_mode(), where both present means IP_MODE_AUTO. */
static int parse_option_group(const char *arg, int *index, int argc, char **argv,
                              config_t *config, int *saw_ipv4, int *saw_ipv6,
                              char *errbuf, size_t errlen)
{
    size_t pos;

    if (arg == NULL || index == NULL || config == NULL ||
        saw_ipv4 == NULL || saw_ipv6 == NULL) {
        set_errbuf(errbuf, errlen, "internal error: invalid option parser state");
        return -1;
    }

    if (arg[0] != '-' || arg[1] == '\0') {
        set_errbuf(errbuf, errlen, "unexpected positional argument");
        return -1;
    }

    for (pos = 1; arg[pos] != '\0'; ++pos) {
        const char *value = NULL;
        int parsed_int = 0;

        switch (arg[pos]) {
        case 'm':
            config->want_metadata = 1;
            break;
        case '4':
            *saw_ipv4 = 1;
            break;
        case '6':
            *saw_ipv6 = 1;
            break;
        case 'q':
            config->verbosity = 0;
            break;
        case 'u':
            if (consume_option_value(arg, index, argc, argv, pos + 1, &value,
                                     errbuf, errlen, "-u") != 0) {
                return -1;
            }
            return config_set_url(config, value, errbuf, errlen);
        case 't':
            if (consume_option_value(arg, index, argc, argv, pos + 1, &value,
                                     errbuf, errlen, "-t") != 0) {
                return -1;
            }
            if (parse_bounded_int(value, MIN_TIMEOUT_MS, MAX_TIMEOUT_MS, &parsed_int,
                                  "invalid timeout value",
                                  "timeout value out of range",
                                  errbuf, errlen) != 0) {
                return -1;
            }
            config->timeout_ms = parsed_int;
            return 0;
        case 'v':
            if (consume_option_value(arg, index, argc, argv, pos + 1, &value,
                                     errbuf, errlen, "-v") != 0) {
                return -1;
            }
            if (parse_bounded_int(value, MIN_VERBOSITY, MAX_VERBOSITY, &parsed_int,
                                  "invalid verbosity value",
                                  "verbosity value out of range",
                                  errbuf, errlen) != 0) {
                return -1;
            }
            config->verbosity = parsed_int;
            return 0;
        default: {
            char message[ERRBUF_SIZE];
            snprintf(message, sizeof(message), "unknown option: -%c", arg[pos]);
            set_errbuf(errbuf, errlen, message);
            return -1;
        }
        }
    }

    return 0;
}

/* Public parser used by main(): reads command-line arguments, validates
 * them, fills config_t, and reports user-facing parsing errors via errbuf.
 * It relies on config_init() for defaults and on helper functions such as
 * set_errbuf() to keep error handling consistent. */
int config_parse(int argc, char **argv, config_t *out, char *errbuf, size_t errlen)
{
    config_t parsed;
    int i;
    int saw_ipv4 = 0;
    int saw_ipv6 = 0;

    if (out == NULL) {
        set_errbuf(errbuf, errlen, "internal error: missing config output");
        return -1;
    }

    if (argc < 0 || (argc > 0 && argv == NULL)) {
        set_errbuf(errbuf, errlen, "internal error: invalid argument vector");
        return -1;
    }

    config_init(&parsed);
    set_errbuf(errbuf, errlen, NULL);

    for (i = 1; i < argc; ++i) {
        if (argv[i] == NULL || argv[i][0] == '\0') {
            set_errbuf(errbuf, errlen, "unexpected empty argument");
            config_free(&parsed);
            return -1;
        }

        if (argv[i][0] != '-') {
            set_errbuf(errbuf, errlen, "unexpected positional argument");
            config_free(&parsed);
            return -1;
        }

        if (parse_option_group(argv[i], &i, argc, argv, &parsed,
                               &saw_ipv4, &saw_ipv6, errbuf, errlen) != 0) {
            config_free(&parsed);
            return -1;
        }
    }

    if (parsed.url == NULL) {
        set_errbuf(errbuf, errlen, "missing required option -u");
        config_free(&parsed);
        return -1;
    }

    finalize_ip_mode(&parsed, saw_ipv4, saw_ipv6);
    set_errbuf(errbuf, errlen, NULL);
    *out = parsed;
    return 0;
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
