#include "sikradio.h"

#include <stdlib.h>
#include <string.h>

static void icy_reset(icy_demuxer_t *d)
{
    if (d == NULL) {
        return;
    }

    d->metaint = 0;
    d->audio_left = 0;
    d->metadata_left = 0;
    d->metadata_buf = NULL;
    d->metadata_len = 0;
    d->metadata_cap = 0;
    d->state = ICY_AUDIO;
}

int icy_init(icy_demuxer_t *d, size_t metaint)
{
    if (d == NULL || metaint == 0) {
        return -1;
    }

    icy_reset(d);
    d->metaint = metaint;
    d->audio_left = metaint;
    d->state = ICY_AUDIO;
    return 0;
}

int icy_feed(icy_demuxer_t *d, const unsigned char *buf, size_t len)
{
    size_t pos = 0;

    if (d == NULL || (buf == NULL && len != 0)) {
        return -1;
    }

    while (pos < len) {
        if (d->state == ICY_AUDIO) {
            size_t chunk;

            if (d->audio_left == 0) {
                d->state = ICY_METADATA_LENGTH;
                continue;
            }

            chunk = d->audio_left;
            if (chunk > len - pos) {
                chunk = len - pos;
            }

            if (write_stdout_audio(buf + pos, chunk) != 0) {
                return -1;
            }

            pos += chunk;
            d->audio_left -= chunk;
            if (d->audio_left == 0) {
                d->state = ICY_METADATA_LENGTH;
            }
            continue;
        }

        if (d->state == ICY_METADATA_LENGTH) {
            size_t metadata_len = (size_t)buf[pos] * 16;

            pos += 1;
            d->metadata_left = metadata_len;
            d->metadata_len = 0;

            if (metadata_len == 0) {
                d->audio_left = d->metaint;
                d->state = ICY_AUDIO;
                continue;
            }

            if (d->metadata_cap < metadata_len) {
                unsigned char *new_buf = realloc(d->metadata_buf, metadata_len);

                if (new_buf == NULL) {
                    return -1;
                }

                d->metadata_buf = new_buf;
                d->metadata_cap = metadata_len;
            }

            d->state = ICY_METADATA;
            continue;
        }

        if (d->state == ICY_METADATA) {
            size_t chunk;

            chunk = d->metadata_left;
            if (chunk > len - pos) {
                chunk = len - pos;
            }

            memcpy(d->metadata_buf + d->metadata_len, buf + pos, chunk);
            pos += chunk;
            d->metadata_len += chunk;
            d->metadata_left -= chunk;

            if (d->metadata_left == 0) {
                size_t metadata_len = d->metadata_len;

                while (metadata_len > 0 && d->metadata_buf[metadata_len - 1] == '\0') {
                    metadata_len -= 1;
                }

                if (metadata_len > 0) {
                    log_metadata(d->metadata_buf, metadata_len);
                }

                d->metadata_len = 0;
                d->audio_left = d->metaint;
                d->state = ICY_AUDIO;
            }
        }
    }

    return 0;
}

void icy_free(icy_demuxer_t *d)
{
    if (d == NULL) {
        return;
    }

    free(d->metadata_buf);
    icy_reset(d);
}
