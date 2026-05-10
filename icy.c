#include "sikradio.h"

#include <stdlib.h>

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
    (void)d;
    (void)buf;
    (void)len;

    return -1;
}

void icy_free(icy_demuxer_t *d)
{
    if (d == NULL) {
        return;
    }

    free(d->metadata_buf);
    icy_reset(d);
}
