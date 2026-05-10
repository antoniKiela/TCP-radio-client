#ifndef SIKRADIO_H
#define SIKRADIO_H

#include <stddef.h>
#include <sys/types.h>

#define DEFAULT_TIMEOUT_MS 5000
#define MIN_TIMEOUT_MS 100
#define MAX_TIMEOUT_MS 100000

#define DEFAULT_VERBOSITY 2
#define MIN_VERBOSITY 0
#define MAX_VERBOSITY 4

#define LOG_COMMUNICATION 1
#define LOG_CRITICAL 2
#define LOG_NONCRITICAL 3
#define LOG_DEBUG 4

#define MAX_REDIRECTS 10
#define MAX_HEADER_BYTES 65536
#define IO_BUFFER_SIZE 8192
#define ERRBUF_SIZE 512

typedef enum {
    IP_MODE_AUTO,
    IP_MODE_IPV4,
    IP_MODE_IPV6
} ip_mode_t;

typedef struct {
    char *url;
    int want_metadata;
    int timeout_ms;
    int verbosity;
    ip_mode_t ip_mode;
} config_t;

typedef enum {
    URL_SCHEME_HTTP,
    URL_SCHEME_HTTPS
} url_scheme_t;

typedef struct {
    url_scheme_t scheme;
    char *host;
    char *port;
    char *path_query;
    int use_tls;
} url_t;

typedef struct {
    int fd;
    int use_tls;
    void *ssl_ctx;
    void *ssl;
} conn_t;

typedef struct {
    char *name;
    char *value;
} header_t;

typedef struct {
    char *status_line;
    int status_code;
    int is_icy;
    header_t *headers;
    size_t header_count;
    unsigned char *body_prefix;
    size_t body_prefix_len;
} http_response_t;

typedef enum {
    ICY_AUDIO,
    ICY_METADATA_LENGTH,
    ICY_METADATA
} icy_state_t;

typedef struct {
    size_t metaint;
    size_t audio_left;
    size_t metadata_left;
    unsigned char *metadata_buf;
    size_t metadata_len;
    size_t metadata_cap;
    icy_state_t state;
} icy_demuxer_t;

typedef enum {
    STREAM_SERVER_CLOSED,
    STREAM_QUIT,
    STREAM_TIMEOUT,
    STREAM_ERROR
} stream_result_t;

/* Implemented in config.c */
int config_parse(int argc, char **argv, config_t *out, char *errbuf, size_t errlen);
void config_free(config_t *config);

/* Implemented in url.c */
int url_parse(const char *text, url_t *out, char *errbuf, size_t errlen);
int url_resolve_redirect(const url_t *base, const char *location, url_t *out,
                         char *errbuf, size_t errlen);
void url_free(url_t *url);

/* Implemented in io.c */
int write_all_fd(int fd, const unsigned char *buf, size_t len);
int write_stdout_audio(const unsigned char *buf, size_t len);
int write_stderr_bytes(const unsigned char *buf, size_t len);
int write_stderr_line(const char *s);
void log_set_verbosity(int verbosity);
void log_msg(int level, const char *fmt, ...);
void log_timestamp(void);
void log_metadata(const unsigned char *buf, size_t len);

/* Implemented in net.c */
int conn_open(conn_t *conn, const url_t *url, ip_mode_t ip_mode, char *errbuf, size_t errlen);
ssize_t conn_read(conn_t *conn, unsigned char *buf, size_t len);
int conn_write_all(conn_t *conn, const unsigned char *buf, size_t len);
int conn_fd(const conn_t *conn);
void conn_close(conn_t *conn);

/* Implemented in http.c */
int http_build_request(const url_t *url, int want_metadata, const char *cookie,
                       unsigned char **out, size_t *out_len);
int http_read_response(conn_t *conn, int timeout_ms, http_response_t *out,
                       char *errbuf, size_t errlen);
const char *http_header_get(const http_response_t *response, const char *name);
int http_is_redirect(int status_code);
int http_extract_cookie(const http_response_t *response, char **cookie_in_out);
void http_response_free(http_response_t *response);

/* Implemented in icy.c */
int icy_init(icy_demuxer_t *d, size_t metaint);
int icy_feed(icy_demuxer_t *d, const unsigned char *buf, size_t len);
void icy_free(icy_demuxer_t *d);

/* Implemented in stream.c */
stream_result_t stream_receive(conn_t *conn, const unsigned char *body_prefix,
                               size_t body_prefix_len, ssize_t metaint,
                               const config_t *config);

/* Implemented in client.c */
int client_run(const config_t *config);

#endif
