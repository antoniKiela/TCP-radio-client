#include "sikradio.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Structures used from sikradio.h:
 * - config_t: provides the original URL string chosen by argument parsing.
 * - url_t: stores the parsed form of that URL for early validation.
 * - conn_t: carries the temporary network connection opened in this step.
 */

/* Minimal client entry point for the current roadmap step.
 * At this stage it validates the configured URL, follows HTTP redirects,
 * carries a simple cookie across them, starts streaming on the first 200
 * response, and frees temporary state before returning. */
int client_run(const config_t *config)
{
    char errbuf[ERRBUF_SIZE];
    char *cookie;
    conn_t conn;
    int redirect_count;
    http_response_t response;
    const char *metaint_header;
    int status;
    unsigned char *request;
    size_t request_len;
    ssize_t metaint;
    stream_result_t stream_result;
    url_t current_url;

    if (config == NULL) {
        write_stderr_line("internal error: missing client configuration");
        return 1;
    }

    cookie = NULL;
    memset(errbuf, 0, sizeof(errbuf));
    redirect_count = 0;
    status = 1;
    memset(&current_url, 0, sizeof(current_url));

    if (url_parse(config->url, &current_url, errbuf, sizeof(errbuf)) != 0) {
        if (errbuf[0] != '\0') {
            write_stderr_line(errbuf);
        } else {
            write_stderr_line("invalid URL");
        }
        return 1;
    }

    for (;;) {
        memset(&conn, 0, sizeof(conn));
        conn.fd = -1;
        memset(&response, 0, sizeof(response));
        request = NULL;
        request_len = 0;
        metaint = -1;
        memset(errbuf, 0, sizeof(errbuf));

        if (conn_open(&conn, &current_url, config->ip_mode, errbuf, sizeof(errbuf)) != 0) {
            if (errbuf[0] != '\0') {
                write_stderr_line(errbuf);
            } else {
                write_stderr_line("connection failed");
            }
            goto cleanup;
        }

        if (http_build_request(&current_url, config->want_metadata, cookie,
                               &request, &request_len) != 0) {
            write_stderr_line("building HTTP request failed");
            goto cleanup;
        }

        if (config->verbosity >= LOG_COMMUNICATION) {
            write_stderr_bytes(request, request_len);
        }

        if (conn_write_all(&conn, request, request_len) != 0) {
            write_stderr_line("sending HTTP request failed");
            goto cleanup;
        }
        free(request);
        request = NULL;

        memset(errbuf, 0, sizeof(errbuf));
        if (http_read_response(&conn, config->timeout_ms, &response, errbuf, sizeof(errbuf)) != 0) {
            if (errbuf[0] != '\0') {
                write_stderr_line(errbuf);
            } else {
                write_stderr_line("reading HTTP response failed");
            }
            goto cleanup;
        }

        if (http_is_redirect(response.status_code)) {
            const char *location;
            url_t next_url;

            if (redirect_count == MAX_REDIRECTS) {
                write_stderr_line("too many redirects");
                goto cleanup;
            }

            if (http_extract_cookie(&response, &cookie) != 0) {
                write_stderr_line("extracting cookie failed");
                goto cleanup;
            }

            location = http_header_get(&response, "Location");
            if (location == NULL) {
                write_stderr_line("redirect response missing Location");
                goto cleanup;
            }

            memset(&next_url, 0, sizeof(next_url));
            memset(errbuf, 0, sizeof(errbuf));
            if (url_resolve_redirect(&current_url, location, &next_url,
                                     errbuf, sizeof(errbuf)) != 0) {
                if (errbuf[0] != '\0') {
                    write_stderr_line(errbuf);
                } else {
                    write_stderr_line("invalid redirect location");
                }
                goto cleanup;
            }

            http_response_free(&response);
            conn_close(&conn);
            url_free(&current_url);
            current_url = next_url;
            redirect_count += 1;
            continue;
        }

        if (response.status_code != 200) {
            goto cleanup;
        }

        metaint_header = http_header_get(&response, "icy-metaint");
        if (metaint_header != NULL) {
            char *endptr;
            long parsed_metaint;

            errno = 0;
            parsed_metaint = strtol(metaint_header, &endptr, 10);
            if (errno == 0 && endptr != metaint_header &&
                *endptr == '\0' && parsed_metaint > 0) {
                metaint = (ssize_t)parsed_metaint;
            }
        }

        stream_result = stream_receive(&conn, response.body_prefix, response.body_prefix_len,
                                       metaint, config);
        if (stream_result == STREAM_SERVER_CLOSED) {
            status = 0;
        }
        goto cleanup;
    }

cleanup:
    free(request);
    http_response_free(&response);
    conn_close(&conn);
    url_free(&current_url);
    free(cookie);

    return status;
}
