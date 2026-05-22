#include "sikradio.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Chooses the best available critical error message so the higher-level
 * client flow can keep its cleanup logic separate from logging decisions. */
static void log_critical_error(const char *message, const char *fallback)
{
    if (message != NULL && message[0] != '\0') {
        log_msg(LOG_CRITICAL, "%s", message);
    } else if (fallback != NULL) {
        log_msg(LOG_CRITICAL, "%s", fallback);
    }
}

/* Structures used from sikradio.h:
 * - config_t: provides the original URL string chosen by argument parsing.
 * - url_t: stores the current parsed URL, including redirect targets.
 * - conn_t: carries the active plain or TLS connection for one request.
 * - http_response_t: owns the parsed response headers and initial body bytes.
 * - stream_result_t: tells the client whether to exit, reconnect, or fail.
 */

/* High-level client entry point coordinating the full runtime flow.
 * It validates the original URL, follows redirects, carries cookies between
 * requests, restarts from the original URL after timeouts, and converts the
 * lower-level streaming outcomes into the final process exit status. */
int client_run(const config_t *config)
{
    char errbuf[ERRBUF_SIZE];
    char *cookie;
    int status;

    if (config == NULL) {
        log_critical_error(NULL, "internal error: missing client configuration");
        return 1;
    }

    cookie = NULL;
    memset(errbuf, 0, sizeof(errbuf));
    status = 1;

    for (;;) {
        conn_t conn;
        int redirect_count;
        http_response_t response;
        const char *metaint_header;
        unsigned char *request;
        size_t request_len;
        ssize_t metaint;
        stream_result_t stream_result;
        url_t current_url;
        int restart_from_original;

        redirect_count = 0;
        request = NULL;
        request_len = 0;
        metaint = -1;
        restart_from_original = 0;
        memset(&conn, 0, sizeof(conn));
        conn.fd = -1;
        memset(&response, 0, sizeof(response));
        memset(&current_url, 0, sizeof(current_url));
        memset(errbuf, 0, sizeof(errbuf));

        if (url_parse(config->url, &current_url, errbuf, sizeof(errbuf)) != 0) {
            log_critical_error(errbuf, "invalid URL");
            free(cookie);
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
                log_critical_error(errbuf, "connection failed");
                break;
            }

            if (http_build_request(&current_url, config->want_metadata, cookie,
                                   &request, &request_len) != 0) {
                log_critical_error(NULL, "building HTTP request failed");
                break;
            }

            if (config->verbosity >= LOG_COMMUNICATION) {
                write_stderr_bytes(request, request_len);
                write_stderr_line("");
            }

            if (conn_write_all(&conn, request, request_len) != 0) {
                log_critical_error(NULL, "sending HTTP request failed");
                break;
            }
            free(request);
            request = NULL;

            memset(errbuf, 0, sizeof(errbuf));
            if (http_read_response(&conn, config->timeout_ms, &response, errbuf, sizeof(errbuf)) != 0) {
                if (strcmp(errbuf, "data receiving timeout") == 0) {
                    log_msg(LOG_COMMUNICATION, "data receiving timeout");
                    restart_from_original = 1;
                    break;
                }
                log_critical_error(errbuf, "reading HTTP response failed");
                break;
            }

            if (http_is_redirect(response.status_code)) {
                const char *location;
                url_t next_url;

                if (redirect_count == MAX_REDIRECTS) {
                    log_critical_error(NULL, "too many redirects");
                    break;
                }

                if (http_extract_cookie(&response, &cookie) != 0) {
                    log_critical_error(NULL, "extracting cookie failed");
                    break;
                }

                location = http_header_get(&response, "Location");
                if (location == NULL) {
                    log_critical_error(NULL, "redirect response missing Location");
                    break;
                }

                memset(&next_url, 0, sizeof(next_url));
                memset(errbuf, 0, sizeof(errbuf));
                if (url_resolve_redirect(&current_url, location, &next_url,
                                         errbuf, sizeof(errbuf)) != 0) {
                    log_critical_error(errbuf, "invalid redirect location");
                    break;
                }

                http_response_free(&response);
                conn_close(&conn);
                url_free(&current_url);
                current_url = next_url;
                redirect_count += 1;
                continue;
            }

            if (response.status_code != 200) {
                break;
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
            if (stream_result == STREAM_TIMEOUT) {
                log_msg(LOG_COMMUNICATION, "data receiving timeout");
                restart_from_original = 1;
            } else if (stream_result == STREAM_QUIT) {
                status = 0;
            } else if (stream_result == STREAM_SERVER_CLOSED) {
                status = 0;
            }

            break;
        }

        free(request);
        http_response_free(&response);
        conn_close(&conn);
        url_free(&current_url);

        if (restart_from_original) {
            free(cookie);
            cookie = NULL;
            continue;
        }

        free(cookie);
        break;
    }

    return status;
}
