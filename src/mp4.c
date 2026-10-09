/* Northstar — incremental fragmented-MP4 (ISO BMFF) demuxing for Media Source Extensions.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mp4.h"

#include <string.h>

#define NS_MP4_FOURCC(a, b, c, d)                                     \
    (((guint32)(guint8)(a) << 24) | ((guint32)(guint8)(b) << 16) |    \
     ((guint32)(guint8)(c) << 8) | (guint32)(guint8)(d))

enum {
    NS_MP4_MAX_HEADER_BOX_BYTES = 64 * 1024 * 1024,
    NS_MP4_MAX_MDAT_BUFFER_BYTES = 512 * 1024 * 1024,
    NS_MP4_MAX_TRACKS = 64,
    NS_MP4_MAX_PENDING_SAMPLES = 1 << 18,
    NS_MP4_MAX_DESCRIPTOR_DEPTH = 8,
};

static const guint64 NS_MP4_MAX_STREAM_POSITION = G_GUINT64_CONSTANT(1) << 62;
static const gint64 NS_MP4_MAX_TIMESTAMP = G_GINT64_CONSTANT(1) << 62;

typedef enum {
    PARSE_BOX_HEADER,
    PARSE_SKIP,
    PARSE_MDAT,
    PARSE_ERROR,
} parse_state;

typedef enum {
    PEEK_NEED_MORE,
    PEEK_COMPLETE,
    PEEK_INVALID,
} peek_result;

typedef struct {
    const guint8 *data;
    gsize         length;
    gsize         offset;
    gboolean      overrun;
} byte_cursor;

typedef struct {
    guint32       type;
    const guint8 *payload;
    gsize         payload_length;
} child_box;

typedef struct {
    ns_mp4_track public;
    gboolean     has_trex;
    guint32      default_sample_description_index;
    guint32      default_sample_duration;
    guint32      default_sample_size;
    guint32      default_sample_flags;
    gboolean     has_next_decode_time;
    gint64       next_decode_time;
} demux_track;

typedef struct {
    guint32 track_id;
    guint32 sample_description_index;
    guint32 default_duration;
    guint32 default_size;
    guint32 default_flags;
} track_extends;

typedef struct {
    guint32  track_id;
    gint64   dts;
    gint64   pts;
    guint32  duration;
    gboolean keyframe;
    guint64  position;
    guint32  size;
} pending_sample;

typedef struct {
    demux_track *track;
    guint64      base_data_offset;
    guint32      default_duration;
    guint32      default_size;
    guint32      default_flags;
    gint64       decode_time;
    guint64      next_data_position;
    gboolean     seen_trun;
} fragment_run_context;

struct ns_mp4_demuxer {
    GByteArray *input;
    guint64     input_position;
    parse_state state;
    guint64     skip_remaining;
    gboolean    skip_forever;
    guint64     mdat_payload_position;
    guint64     mdat_payload_length;
    gboolean    mdat_unbounded;
    GPtrArray  *tracks;
    gboolean    has_init;
    guint32     movie_timescale;
    GArray     *pending;
    guint       pending_head;
    GQueue     *ready;
};

static void
cursor_init(byte_cursor *c, const guint8 *data, gsize length)
{
    c->data = data;
    c->length = data ? length : 0;
    c->offset = 0;
    c->overrun = FALSE;
}

static gsize
cursor_remaining(const byte_cursor *c)
{
    return c->overrun ? 0 : c->length - c->offset;
}

static gboolean
cursor_has(const byte_cursor *c, gsize n)
{
    return cursor_remaining(c) >= n;
}

static const guint8 *
cursor_position(const byte_cursor *c)
{
    return c->data ? c->data + c->offset : NULL;
}

static gboolean
cursor_skip(byte_cursor *c, gsize n)
{
    if (!cursor_has(c, n)) {
        c->overrun = TRUE;
        c->offset = c->length;
        return FALSE;
    }
    c->offset += n;
    return TRUE;
}

static guint64
cursor_read_be(byte_cursor *c, gsize n)
{
    if (!cursor_has(c, n)) {
        c->overrun = TRUE;
        c->offset = c->length;
        return 0;
    }
    guint64 value = 0;
    for (gsize i = 0; i < n; i++)
        value = (value << 8) | c->data[c->offset + i];
    c->offset += n;
    return value;
}

static guint8
cursor_u8(byte_cursor *c)
{
    return (guint8)cursor_read_be(c, 1);
}

static guint16
cursor_u16(byte_cursor *c)
{
    return (guint16)cursor_read_be(c, 2);
}

static guint32
cursor_u24(byte_cursor *c)
{
    return (guint32)cursor_read_be(c, 3);
}

static guint32
cursor_u32(byte_cursor *c)
{
    return (guint32)cursor_read_be(c, 4);
}

static guint64
cursor_u64(byte_cursor *c)
{
    return cursor_read_be(c, 8);
}

static int
cursor_next_box(byte_cursor *c, child_box *out)
{
    gsize remaining = cursor_remaining(c);
    if (remaining < 8) {
        cursor_skip(c, remaining);
        return 0;
    }
    const guint8 *start = cursor_position(c);
    byte_cursor header;
    cursor_init(&header, start, remaining);
    guint64 size = cursor_u32(&header);
    guint32 type = cursor_u32(&header);
    if (size == 1) {
        size = cursor_u64(&header);
        if (header.overrun) return -1;
    } else if (size == 0) {
        size = remaining;
    }
    if (type == NS_MP4_FOURCC('u', 'u', 'i', 'd') && !cursor_skip(&header, 16))
        return -1;
    if (size < header.offset || size > remaining) return -1;
    out->type = type;
    out->payload = start + header.offset;
    out->payload_length = (gsize)size - header.offset;
    cursor_skip(c, (gsize)size);
    return 1;
}

static peek_result
peek_top_level_box(const guint8 *data, gsize available, guint32 *type,
                   guint64 *total_size, gsize *header_length,
                   gboolean *extends_to_end)
{
    byte_cursor c;
    cursor_init(&c, data, available);
    if (!cursor_has(&c, 8)) return PEEK_NEED_MORE;
    guint64 size = cursor_u32(&c);
    *type = cursor_u32(&c);
    *extends_to_end = FALSE;
    if (size == 1) {
        if (!cursor_has(&c, 8)) return PEEK_NEED_MORE;
        size = cursor_u64(&c);
    } else if (size == 0) {
        *extends_to_end = TRUE;
    }
    if (*type == NS_MP4_FOURCC('u', 'u', 'i', 'd')) {
        if (!cursor_has(&c, 16)) return PEEK_NEED_MORE;
        cursor_skip(&c, 16);
    }
    *header_length = c.offset;
    if (!*extends_to_end && size < c.offset) return PEEK_INVALID;
    if (size >= NS_MP4_MAX_STREAM_POSITION) return PEEK_INVALID;
    *total_size = size;
    return PEEK_COMPLETE;
}

static void
fourcc_to_string(guint32 fourcc, char out[5])
{
    for (int i = 0; i < 4; i++) {
        char ch = (char)((fourcc >> (24 - 8 * i)) & 0xFF);
        out[i] = g_ascii_isprint(ch) ? ch : '?';
    }
    out[4] = '\0';
}

static void
demux_track_free(gpointer data)
{
    demux_track *track = data;
    if (!track) return;
    if (track->public.codec_config) g_bytes_unref(track->public.codec_config);
    if (track->public.config_obus) g_bytes_unref(track->public.config_obus);
    g_free(track);
}

static void
ready_sample_free(gpointer data)
{
    ns_mp4_sample *sample = data;
    if (!sample) return;
    if (sample->data) g_bytes_unref(sample->data);
    g_free(sample);
}

static demux_track *
find_track(GPtrArray *tracks, guint32 id)
{
    if (!tracks) return NULL;
    for (guint i = 0; i < tracks->len; i++) {
        demux_track *track = g_ptr_array_index(tracks, i);
        if (track->public.id == id) return track;
    }
    return NULL;
}

static gboolean
sample_flags_mean_sync(guint32 flags)
{
    guint32 depends_on = (flags >> 24) & 0x3;
    gboolean is_non_sync = (flags >> 16) & 0x1;
    return !is_non_sync && depends_on != 1;
}

static gboolean
read_descriptor_header(byte_cursor *c, guint8 *tag, gsize *length)
{
    *tag = cursor_u8(c);
    gsize value = 0;
    for (int i = 0; i < 4; i++) {
        guint8 part = cursor_u8(c);
        value = (value << 7) | (part & 0x7F);
        if (!(part & 0x80)) break;
    }
    if (c->overrun) return FALSE;
    *length = MIN(value, cursor_remaining(c));
    return TRUE;
}

static gboolean
find_descriptor(byte_cursor *c, guint8 wanted_tag, byte_cursor *found)
{
    while (cursor_remaining(c) >= 2) {
        guint8 tag = 0;
        gsize length = 0;
        if (!read_descriptor_header(c, &tag, &length)) return FALSE;
        const guint8 *body = cursor_position(c);
        cursor_skip(c, length);
        if (tag == wanted_tag) {
            cursor_init(found, body, length);
            return TRUE;
        }
    }
    return FALSE;
}

static void
parse_decoder_config_descriptor(demux_track *track, byte_cursor *c)
{
    track->public.object_type_indication = cursor_u8(c);
    if (!cursor_skip(c, 12)) return;
    byte_cursor specific_info;
    if (!find_descriptor(c, 0x05, &specific_info)) return;
    if (track->public.codec_config) g_bytes_unref(track->public.codec_config);
    track->public.codec_config =
        g_bytes_new(cursor_position(&specific_info),
                    cursor_remaining(&specific_info));
}

static void
parse_esds(demux_track *track, const guint8 *payload, gsize length)
{
    byte_cursor c;
    cursor_init(&c, payload, length);
    if (!cursor_skip(&c, 4)) return;
    byte_cursor es_descriptor;
    byte_cursor scan = c;
    if (!find_descriptor(&scan, 0x03, &es_descriptor)) {
        byte_cursor decoder_config;
        if (find_descriptor(&c, 0x04, &decoder_config))
            parse_decoder_config_descriptor(track, &decoder_config);
        return;
    }
    cursor_skip(&es_descriptor, 2);
    guint8 es_flags = cursor_u8(&es_descriptor);
    if (es_flags & 0x80) cursor_skip(&es_descriptor, 2);
    if (es_flags & 0x40) {
        guint8 url_length = cursor_u8(&es_descriptor);
        cursor_skip(&es_descriptor, url_length);
    }
    if (es_flags & 0x20) cursor_skip(&es_descriptor, 2);
    if (es_descriptor.overrun) return;
    byte_cursor decoder_config;
    if (find_descriptor(&es_descriptor, 0x04, &decoder_config))
        parse_decoder_config_descriptor(track, &decoder_config);
}

static void
store_codec_config(demux_track *track, const child_box *box)
{
    if (track->public.codec_config) g_bytes_unref(track->public.codec_config);
    track->public.codec_config = g_bytes_new(box->payload, box->payload_length);
}

static gboolean
box_type_is_codec_config(guint32 type)
{
    switch (type) {
    case NS_MP4_FOURCC('a', 'v', 'c', 'C'):
    case NS_MP4_FOURCC('h', 'v', 'c', 'C'):
    case NS_MP4_FOURCC('v', 'v', 'c', 'C'):
    case NS_MP4_FOURCC('v', 'p', 'c', 'C'):
    case NS_MP4_FOURCC('d', 'O', 'p', 's'):
    case NS_MP4_FOURCC('d', 'f', 'L', 'a'):
    case NS_MP4_FOURCC('d', 'a', 'c', '3'):
    case NS_MP4_FOURCC('d', 'e', 'c', '3'):
    case NS_MP4_FOURCC('a', 'v', '1', 'C'):
    case NS_MP4_FOURCC('e', 's', 'd', 's'):
        return TRUE;
    default:
        return FALSE;
    }
}

static void
parse_sample_entry_children(demux_track *track, guint32 entry_type,
                            byte_cursor *c, int depth)
{
    child_box child;
    while (cursor_next_box(c, &child) > 0) {
        if (child.type == NS_MP4_FOURCC('a', 'v', '1', 'C')) {
            store_codec_config(track, &child);
            if (track->public.config_obus)
                g_bytes_unref(track->public.config_obus);
            track->public.config_obus = child.payload_length >= 4
                ? g_bytes_new(child.payload + 4, child.payload_length - 4)
                : NULL;
            return;
        }
        if (child.type == NS_MP4_FOURCC('e', 's', 'd', 's')) {
            parse_esds(track, child.payload, child.payload_length);
            return;
        }
        if (child.type == NS_MP4_FOURCC('w', 'a', 'v', 'e') && depth == 0) {
            byte_cursor wave;
            cursor_init(&wave, child.payload, child.payload_length);
            parse_sample_entry_children(track, entry_type, &wave, depth + 1);
            if (track->public.codec_config) return;
            continue;
        }
        if (box_type_is_codec_config(child.type)) {
            store_codec_config(track, &child);
            return;
        }
    }
}

static void
parse_sample_entry(demux_track *track, guint32 handler, const child_box *entry)
{
    fourcc_to_string(entry->type, track->public.codec);
    byte_cursor c;
    cursor_init(&c, entry->payload, entry->payload_length);
    if (!cursor_skip(&c, 8)) return;
    if (handler == NS_MP4_FOURCC('v', 'i', 'd', 'e')) {
        cursor_skip(&c, 16);
        track->public.width = cursor_u16(&c);
        track->public.height = cursor_u16(&c);
        if (!cursor_skip(&c, 50)) return;
    } else if (handler == NS_MP4_FOURCC('s', 'o', 'u', 'n')) {
        guint16 version = cursor_u16(&c);
        cursor_skip(&c, 6);
        track->public.channels = cursor_u16(&c);
        cursor_skip(&c, 6);
        track->public.sample_rate = (int)(cursor_u32(&c) >> 16);
        if (c.overrun) return;
        if (version == 1) {
            if (!cursor_skip(&c, 16)) return;
        } else if (version == 2) {
            cursor_skip(&c, 4);
            guint64 rate_bits = cursor_u64(&c);
            guint32 channels = cursor_u32(&c);
            if (!cursor_skip(&c, 20)) return;
            double rate = 0.0;
            memcpy(&rate, &rate_bits, sizeof rate);
            if (rate > 0.0 && rate < 1e7) track->public.sample_rate = (int)rate;
            if (channels > 0 && channels < 1024)
                track->public.channels = (int)channels;
        }
    } else {
        return;
    }
    parse_sample_entry_children(track, entry->type, &c, 0);
}

static void
parse_stsd(demux_track *track, guint32 handler, const child_box *stsd)
{
    byte_cursor c;
    cursor_init(&c, stsd->payload, stsd->payload_length);
    cursor_skip(&c, 4);
    guint32 entry_count = cursor_u32(&c);
    if (c.overrun || entry_count == 0) return;
    child_box entry;
    if (cursor_next_box(&c, &entry) > 0)
        parse_sample_entry(track, handler, &entry);
}

static gboolean
parse_container_for(const child_box *parent, guint32 wanted, child_box *found)
{
    byte_cursor c;
    cursor_init(&c, parent->payload, parent->payload_length);
    child_box child;
    while (cursor_next_box(&c, &child) > 0) {
        if (child.type == wanted) {
            *found = child;
            return TRUE;
        }
    }
    return FALSE;
}

static void
parse_minf(demux_track *track, guint32 handler, const child_box *minf)
{
    child_box stbl;
    child_box stsd;
    if (parse_container_for(minf, NS_MP4_FOURCC('s', 't', 'b', 'l'), &stbl) &&
        parse_container_for(&stbl, NS_MP4_FOURCC('s', 't', 's', 'd'), &stsd))
        parse_stsd(track, handler, &stsd);
}

static gboolean
parse_mdhd(demux_track *track, const child_box *box)
{
    byte_cursor c;
    cursor_init(&c, box->payload, box->payload_length);
    guint8 version = cursor_u8(&c);
    cursor_skip(&c, 3);
    cursor_skip(&c, version == 1 ? 16 : 8);
    track->public.timescale = cursor_u32(&c);
    return !c.overrun;
}

static guint32
parse_hdlr(const child_box *box)
{
    byte_cursor c;
    cursor_init(&c, box->payload, box->payload_length);
    cursor_skip(&c, 8);
    guint32 handler = cursor_u32(&c);
    return c.overrun ? 0 : handler;
}

static gboolean
parse_mdia(demux_track *track, const child_box *mdia)
{
    byte_cursor c;
    cursor_init(&c, mdia->payload, mdia->payload_length);
    child_box child;
    int status;
    guint32 handler = 0;
    while ((status = cursor_next_box(&c, &child)) > 0) {
        if (child.type == NS_MP4_FOURCC('m', 'd', 'h', 'd')) {
            if (!parse_mdhd(track, &child)) return FALSE;
        } else if (child.type == NS_MP4_FOURCC('h', 'd', 'l', 'r')) {
            handler = parse_hdlr(&child);
        }
    }
    if (status < 0) return FALSE;
    fourcc_to_string(handler, track->public.handler);
    child_box minf;
    if (parse_container_for(mdia, NS_MP4_FOURCC('m', 'i', 'n', 'f'), &minf))
        parse_minf(track, handler, &minf);
    return TRUE;
}

static gboolean
parse_tkhd(demux_track *track, const child_box *box, int *width, int *height)
{
    byte_cursor c;
    cursor_init(&c, box->payload, box->payload_length);
    guint8 version = cursor_u8(&c);
    cursor_skip(&c, 3);
    cursor_skip(&c, version == 1 ? 16 : 8);
    track->public.id = cursor_u32(&c);
    if (c.overrun) return FALSE;
    cursor_skip(&c, 4 + (version == 1 ? 8 : 4) + 8 + 2 + 2 + 2 + 2 + 36);
    guint32 fixed_width = cursor_u32(&c);
    guint32 fixed_height = cursor_u32(&c);
    if (!c.overrun) {
        *width = (int)(fixed_width >> 16);
        *height = (int)(fixed_height >> 16);
    }
    return TRUE;
}

static void
parse_elst(demux_track *track, const child_box *box)
{
    byte_cursor c;
    cursor_init(&c, box->payload, box->payload_length);
    guint8 version = cursor_u8(&c);
    cursor_skip(&c, 3);
    guint32 entry_count = cursor_u32(&c);
    gsize entry_size = version == 1 ? 20 : 12;
    if (c.overrun) return;
    entry_count = (guint32)MIN(entry_count, cursor_remaining(&c) / entry_size);
    for (guint32 i = 0; i < entry_count; i++) {
        gint64 media_time;
        if (version == 1) {
            cursor_skip(&c, 8);
            media_time = (gint64)cursor_u64(&c);
        } else {
            cursor_skip(&c, 4);
            media_time = (gint32)cursor_u32(&c);
        }
        cursor_skip(&c, 4);
        if (c.overrun) return;
        if (media_time >= 0) {
            track->public.edit_media_time = media_time;
            return;
        }
    }
}

static demux_track *
parse_trak(const child_box *trak)
{
    demux_track *track = g_new0(demux_track, 1);
    track->public.edit_media_time = -1;
    fourcc_to_string(0, track->public.handler);
    fourcc_to_string(0, track->public.codec);
    int header_width = 0;
    int header_height = 0;
    gboolean has_tkhd = FALSE;
    gboolean has_mdia = FALSE;
    byte_cursor c;
    cursor_init(&c, trak->payload, trak->payload_length);
    child_box child;
    int status;
    while ((status = cursor_next_box(&c, &child)) > 0) {
        if (child.type == NS_MP4_FOURCC('t', 'k', 'h', 'd')) {
            if (!parse_tkhd(track, &child, &header_width, &header_height))
                break;
            has_tkhd = TRUE;
        } else if (child.type == NS_MP4_FOURCC('m', 'd', 'i', 'a')) {
            if (!parse_mdia(track, &child)) break;
            has_mdia = TRUE;
        } else if (child.type == NS_MP4_FOURCC('e', 'd', 't', 's')) {
            child_box elst;
            if (parse_container_for(&child, NS_MP4_FOURCC('e', 'l', 's', 't'),
                                    &elst))
                parse_elst(track, &elst);
        }
    }
    if (status < 0 || !has_tkhd || !has_mdia || track->public.id == 0 ||
        track->public.timescale == 0) {
        demux_track_free(track);
        return NULL;
    }
    if (track->public.width <= 0) track->public.width = header_width;
    if (track->public.height <= 0) track->public.height = header_height;
    return track;
}

static gboolean
parse_mvex(GArray *extends, const child_box *mvex)
{
    byte_cursor c;
    cursor_init(&c, mvex->payload, mvex->payload_length);
    child_box child;
    int status;
    while ((status = cursor_next_box(&c, &child)) > 0) {
        if (child.type != NS_MP4_FOURCC('t', 'r', 'e', 'x')) continue;
        byte_cursor body;
        cursor_init(&body, child.payload, child.payload_length);
        cursor_skip(&body, 4);
        track_extends entry;
        entry.track_id = cursor_u32(&body);
        entry.sample_description_index = cursor_u32(&body);
        entry.default_duration = cursor_u32(&body);
        entry.default_size = cursor_u32(&body);
        entry.default_flags = cursor_u32(&body);
        if (body.overrun) return FALSE;
        if (extends->len < NS_MP4_MAX_TRACKS)
            g_array_append_val(extends, entry);
    }
    return status == 0;
}

static gboolean
parse_moov(ns_mp4_demuxer *d, const guint8 *payload, gsize length)
{
    GPtrArray *tracks = g_ptr_array_new_with_free_func(demux_track_free);
    GArray *extends = g_array_new(FALSE, FALSE, sizeof(track_extends));
    guint32 movie_timescale = 0;
    gboolean ok = TRUE;
    byte_cursor c;
    cursor_init(&c, payload, length);
    child_box child;
    int status = 0;
    while (ok && (status = cursor_next_box(&c, &child)) > 0) {
        if (child.type == NS_MP4_FOURCC('m', 'v', 'h', 'd')) {
            byte_cursor body;
            cursor_init(&body, child.payload, child.payload_length);
            guint8 version = cursor_u8(&body);
            cursor_skip(&body, 3 + (version == 1 ? 16 : 8));
            movie_timescale = cursor_u32(&body);
            ok = !body.overrun;
        } else if (child.type == NS_MP4_FOURCC('t', 'r', 'a', 'k')) {
            demux_track *track = parse_trak(&child);
            ok = track && tracks->len < NS_MP4_MAX_TRACKS &&
                 !find_track(tracks, track->public.id);
            if (ok)
                g_ptr_array_add(tracks, track);
            else
                demux_track_free(track);
        } else if (child.type == NS_MP4_FOURCC('m', 'v', 'e', 'x')) {
            ok = parse_mvex(extends, &child);
        }
    }
    if (status < 0 || tracks->len == 0) ok = FALSE;
    if (ok) {
        for (guint i = 0; i < extends->len; i++) {
            track_extends *entry = &g_array_index(extends, track_extends, i);
            demux_track *track = find_track(tracks, entry->track_id);
            if (!track) continue;
            track->has_trex = TRUE;
            track->default_sample_description_index =
                entry->sample_description_index;
            track->default_sample_duration = entry->default_duration;
            track->default_sample_size = entry->default_size;
            track->default_sample_flags = entry->default_flags;
        }
        if (d->tracks) g_ptr_array_unref(d->tracks);
        d->tracks = tracks;
        d->movie_timescale = movie_timescale;
        d->has_init = TRUE;
    } else {
        g_ptr_array_unref(tracks);
    }
    g_array_unref(extends);
    return ok;
}

static gboolean
checked_position(guint64 base, gint64 offset, guint64 *out)
{
    if (base >= NS_MP4_MAX_STREAM_POSITION) return FALSE;
    if (offset < 0 && (guint64)(-offset) > base) return FALSE;
    guint64 position = offset < 0 ? base - (guint64)(-offset)
                                  : base + (guint64)offset;
    if (position >= NS_MP4_MAX_STREAM_POSITION) return FALSE;
    *out = position;
    return TRUE;
}

static gboolean
parse_trun(ns_mp4_demuxer *d, fragment_run_context *run, const child_box *box)
{
    byte_cursor c;
    cursor_init(&c, box->payload, box->payload_length);
    guint8 version = cursor_u8(&c);
    guint32 flags = cursor_u24(&c);
    guint32 sample_count = cursor_u32(&c);
    gint32 data_offset = (flags & 0x1) ? (gint32)cursor_u32(&c) : 0;
    gboolean has_first_flags = (flags & 0x4) != 0;
    guint32 first_sample_flags = has_first_flags ? cursor_u32(&c) : 0;
    if (c.overrun) return FALSE;
    gsize bytes_per_sample = ((flags & 0x100) ? 4 : 0) +
                             ((flags & 0x200) ? 4 : 0) +
                             ((flags & 0x400) ? 4 : 0) +
                             ((flags & 0x800) ? 4 : 0);
    if (bytes_per_sample > 0 &&
        sample_count > cursor_remaining(&c) / bytes_per_sample)
        return FALSE;
    guint pending_count = d->pending->len - d->pending_head;
    if (sample_count > NS_MP4_MAX_PENDING_SAMPLES - pending_count)
        return FALSE;
    guint64 data_position;
    if (flags & 0x1) {
        if (!checked_position(run->base_data_offset, data_offset,
                              &data_position))
            return FALSE;
    } else {
        data_position = run->seen_trun ? run->next_data_position
                                       : run->base_data_offset;
    }
    run->seen_trun = TRUE;
    for (guint32 i = 0; i < sample_count; i++) {
        guint32 duration = (flags & 0x100) ? cursor_u32(&c)
                                           : run->default_duration;
        guint32 size = (flags & 0x200) ? cursor_u32(&c) : run->default_size;
        guint32 sample_flags = (flags & 0x400) ? cursor_u32(&c)
                                               : run->default_flags;
        if (i == 0 && has_first_flags) sample_flags = first_sample_flags;
        gint64 composition_offset = 0;
        if (flags & 0x800) {
            guint32 raw = cursor_u32(&c);
            composition_offset = version == 0 ? (gint64)raw
                                              : (gint64)(gint32)raw;
        }
        if (c.overrun) return FALSE;
        if (data_position + size >= NS_MP4_MAX_STREAM_POSITION) return FALSE;
        if (run->decode_time > NS_MP4_MAX_TIMESTAMP ||
            run->decode_time < -NS_MP4_MAX_TIMESTAMP)
            return FALSE;
        if (run->track) {
            pending_sample sample;
            sample.track_id = run->track->public.id;
            sample.dts = run->decode_time;
            sample.pts = run->decode_time + composition_offset;
            sample.duration = duration;
            sample.keyframe = sample_flags_mean_sync(sample_flags);
            sample.position = data_position;
            sample.size = size;
            g_array_append_val(d->pending, sample);
        }
        run->decode_time += duration;
        data_position += size;
    }
    run->next_data_position = data_position;
    return TRUE;
}

static gboolean
parse_traf(ns_mp4_demuxer *d, const child_box *traf, guint64 moof_position,
           gboolean first_traf, guint64 *previous_traf_data_end)
{
    child_box tfhd;
    if (!parse_container_for(traf, NS_MP4_FOURCC('t', 'f', 'h', 'd'), &tfhd))
        return FALSE;
    byte_cursor c;
    cursor_init(&c, tfhd.payload, tfhd.payload_length);
    cursor_skip(&c, 1);
    guint32 flags = cursor_u24(&c);
    guint32 track_id = cursor_u32(&c);
    guint64 base_data_offset = (flags & 0x1) ? cursor_u64(&c) : 0;
    if (flags & 0x2) cursor_skip(&c, 4);
    fragment_run_context run;
    memset(&run, 0, sizeof run);
    run.track = find_track(d->tracks, track_id);
    if (run.track) {
        run.default_duration = run.track->default_sample_duration;
        run.default_size = run.track->default_sample_size;
        run.default_flags = run.track->default_sample_flags;
    }
    if (flags & 0x8) run.default_duration = cursor_u32(&c);
    if (flags & 0x10) run.default_size = cursor_u32(&c);
    if (flags & 0x20) run.default_flags = cursor_u32(&c);
    if (c.overrun) return FALSE;
    if (flags & 0x1)
        run.base_data_offset = base_data_offset;
    else if ((flags & 0x20000) || first_traf)
        run.base_data_offset = moof_position;
    else
        run.base_data_offset = *previous_traf_data_end;
    if (run.base_data_offset >= NS_MP4_MAX_STREAM_POSITION) return FALSE;
    if (run.track && run.track->has_next_decode_time)
        run.decode_time = run.track->next_decode_time;
    child_box tfdt;
    if (parse_container_for(traf, NS_MP4_FOURCC('t', 'f', 'd', 't'), &tfdt)) {
        byte_cursor body;
        cursor_init(&body, tfdt.payload, tfdt.payload_length);
        guint8 version = cursor_u8(&body);
        cursor_skip(&body, 3);
        guint64 base_time = version == 1 ? cursor_u64(&body)
                                         : cursor_u32(&body);
        if (body.overrun || base_time > (guint64)NS_MP4_MAX_TIMESTAMP)
            return FALSE;
        run.decode_time = (gint64)base_time;
    }
    run.next_data_position = run.base_data_offset;
    cursor_init(&c, traf->payload, traf->payload_length);
    child_box child;
    int status;
    while ((status = cursor_next_box(&c, &child)) > 0) {
        if (child.type == NS_MP4_FOURCC('t', 'r', 'u', 'n') &&
            !parse_trun(d, &run, &child))
            return FALSE;
    }
    if (status < 0) return FALSE;
    if (run.track) {
        run.track->has_next_decode_time = TRUE;
        run.track->next_decode_time = run.decode_time;
    }
    *previous_traf_data_end = run.next_data_position;
    return TRUE;
}

static gboolean
parse_moof(ns_mp4_demuxer *d, const guint8 *payload, gsize length,
           guint64 moof_position)
{
    if (!d->has_init) return FALSE;
    guint pending_before = d->pending->len;
    byte_cursor c;
    cursor_init(&c, payload, length);
    child_box child;
    int status;
    gboolean first_traf = TRUE;
    guint64 previous_traf_data_end = moof_position;
    while ((status = cursor_next_box(&c, &child)) > 0) {
        if (child.type != NS_MP4_FOURCC('t', 'r', 'a', 'f')) continue;
        if (!parse_traf(d, &child, moof_position, first_traf,
                        &previous_traf_data_end)) {
            g_array_set_size(d->pending, pending_before);
            return FALSE;
        }
        first_traf = FALSE;
    }
    if (status < 0) {
        g_array_set_size(d->pending, pending_before);
        return FALSE;
    }
    return TRUE;
}

static void
drop_input(ns_mp4_demuxer *d, gsize count)
{
    count = MIN(count, (gsize)d->input->len);
    if (count == 0) return;
    g_byte_array_remove_range(d->input, 0, (guint)count);
    d->input_position += count;
}

static void
compact_pending(ns_mp4_demuxer *d)
{
    if (d->pending_head == 0) return;
    if (d->pending_head >= d->pending->len) {
        g_array_set_size(d->pending, 0);
    } else {
        g_array_remove_range(d->pending, 0, d->pending_head);
    }
    d->pending_head = 0;
}

static void
emit_sample(ns_mp4_demuxer *d, const pending_sample *pending)
{
    ns_mp4_sample *sample = g_new0(ns_mp4_sample, 1);
    sample->track_id = pending->track_id;
    sample->dts = pending->dts;
    sample->pts = pending->pts;
    sample->duration = pending->duration;
    sample->keyframe = pending->keyframe;
    gsize offset = (gsize)(pending->position - d->input_position);
    sample->data = g_bytes_new(d->input->data + offset, pending->size);
    g_queue_push_tail(d->ready, sample);
}

static gboolean
sample_fits_in_mdat(const ns_mp4_demuxer *d, const pending_sample *sample)
{
    if (sample->position < d->mdat_payload_position) return FALSE;
    if (d->mdat_unbounded) return TRUE;
    guint64 relative = sample->position - d->mdat_payload_position;
    return relative <= d->mdat_payload_length &&
           sample->size <= d->mdat_payload_length - relative;
}

static void
emit_available_mdat_samples(ns_mp4_demuxer *d)
{
    guint64 available_end = d->input_position + d->input->len;
    while (d->pending_head < d->pending->len) {
        pending_sample *sample =
            &g_array_index(d->pending, pending_sample, d->pending_head);
        if (!sample_fits_in_mdat(d, sample)) break;
        if (sample->position + sample->size > available_end) break;
        emit_sample(d, sample);
        d->pending_head++;
    }
}

static gboolean
finish_mdat(ns_mp4_demuxer *d)
{
    guint64 mdat_end = d->mdat_payload_position + d->mdat_payload_length;
    GArray *remaining = g_array_new(FALSE, FALSE, sizeof(pending_sample));
    gboolean ok = TRUE;
    for (guint i = d->pending_head; i < d->pending->len; i++) {
        pending_sample *sample = &g_array_index(d->pending, pending_sample, i);
        if (sample_fits_in_mdat(d, sample)) {
            emit_sample(d, sample);
        } else if (sample->position >= mdat_end) {
            g_array_append_val(remaining, *sample);
        } else {
            ok = FALSE;
            break;
        }
    }
    g_array_unref(d->pending);
    d->pending = remaining;
    d->pending_head = 0;
    if (!ok) g_array_set_size(d->pending, 0);
    drop_input(d, (gsize)d->mdat_payload_length);
    d->state = PARSE_BOX_HEADER;
    return ok;
}

static void
enter_skip(ns_mp4_demuxer *d, guint64 bytes, gboolean forever)
{
    d->state = PARSE_SKIP;
    d->skip_remaining = bytes;
    d->skip_forever = forever;
}

static gboolean
process_mdat(ns_mp4_demuxer *d)
{
    emit_available_mdat_samples(d);
    if (!d->mdat_unbounded && d->input->len >= d->mdat_payload_length)
        return finish_mdat(d);
    if (d->pending_head >= d->pending->len) {
        compact_pending(d);
        guint64 left = d->mdat_unbounded
            ? 0 : d->mdat_payload_length - d->input->len;
        drop_input(d, d->input->len);
        enter_skip(d, left, d->mdat_unbounded);
        return TRUE;
    }
    if (d->pending_head > 1024 && d->pending_head * 2 > d->pending->len)
        compact_pending(d);
    return d->input->len <= NS_MP4_MAX_MDAT_BUFFER_BYTES;
}

static gboolean
process_top_level_box(ns_mp4_demuxer *d, gboolean *need_more)
{
    guint32 type = 0;
    guint64 total_size = 0;
    gsize header_length = 0;
    gboolean extends_to_end = FALSE;
    peek_result peek = peek_top_level_box(d->input->data, d->input->len, &type,
                                          &total_size, &header_length,
                                          &extends_to_end);
    if (peek == PEEK_NEED_MORE) {
        *need_more = TRUE;
        return TRUE;
    }
    if (peek == PEEK_INVALID) return FALSE;
    gboolean is_moov = type == NS_MP4_FOURCC('m', 'o', 'o', 'v');
    gboolean is_moof = type == NS_MP4_FOURCC('m', 'o', 'o', 'f');
    if (type == NS_MP4_FOURCC('m', 'd', 'a', 't')) {
        guint64 payload_length =
            extends_to_end ? 0 : total_size - header_length;
        drop_input(d, header_length);
        if (d->pending_head >= d->pending->len) {
            compact_pending(d);
            enter_skip(d, payload_length, extends_to_end);
            return TRUE;
        }
        if (!extends_to_end && payload_length > NS_MP4_MAX_MDAT_BUFFER_BYTES)
            return FALSE;
        d->state = PARSE_MDAT;
        d->mdat_payload_position = d->input_position;
        d->mdat_payload_length = payload_length;
        d->mdat_unbounded = extends_to_end;
        return TRUE;
    }
    if (is_moov || is_moof) {
        if (extends_to_end || total_size > NS_MP4_MAX_HEADER_BOX_BYTES)
            return FALSE;
        if (d->input->len < total_size) {
            *need_more = TRUE;
            return TRUE;
        }
        const guint8 *payload = d->input->data + header_length;
        gsize payload_length = (gsize)total_size - header_length;
        gboolean ok = is_moov
            ? parse_moov(d, payload, payload_length)
            : parse_moof(d, payload, payload_length, d->input_position);
        drop_input(d, (gsize)total_size);
        return ok;
    }
    if (extends_to_end) {
        drop_input(d, d->input->len);
        enter_skip(d, 0, TRUE);
        return TRUE;
    }
    enter_skip(d, total_size, FALSE);
    return TRUE;
}

static gboolean
process_input(ns_mp4_demuxer *d)
{
    for (;;) {
        switch (d->state) {
        case PARSE_ERROR:
            return FALSE;
        case PARSE_SKIP: {
            if (d->skip_forever) {
                drop_input(d, d->input->len);
                return TRUE;
            }
            gsize count = (gsize)MIN(d->skip_remaining, (guint64)d->input->len);
            drop_input(d, count);
            d->skip_remaining -= count;
            if (d->skip_remaining > 0) return TRUE;
            d->state = PARSE_BOX_HEADER;
            break;
        }
        case PARSE_MDAT:
            if (!process_mdat(d)) return FALSE;
            if (d->state == PARSE_MDAT) return TRUE;
            break;
        case PARSE_BOX_HEADER: {
            gboolean need_more = FALSE;
            if (!process_top_level_box(d, &need_more)) return FALSE;
            if (need_more) return TRUE;
            break;
        }
        }
    }
}

static void
clear_parser_state(ns_mp4_demuxer *d)
{
    g_byte_array_set_size(d->input, 0);
    d->input_position = 0;
    d->state = PARSE_BOX_HEADER;
    d->skip_remaining = 0;
    d->skip_forever = FALSE;
    d->mdat_payload_position = 0;
    d->mdat_payload_length = 0;
    d->mdat_unbounded = FALSE;
    g_array_set_size(d->pending, 0);
    d->pending_head = 0;
}

ns_mp4_demuxer *
ns_mp4_demuxer_new(void)
{
    ns_mp4_demuxer *d = g_new0(ns_mp4_demuxer, 1);
    d->input = g_byte_array_new();
    d->tracks = g_ptr_array_new_with_free_func(demux_track_free);
    d->pending = g_array_new(FALSE, FALSE, sizeof(pending_sample));
    d->ready = g_queue_new();
    clear_parser_state(d);
    return d;
}

void
ns_mp4_demuxer_free(ns_mp4_demuxer *d)
{
    if (!d) return;
    g_byte_array_unref(d->input);
    g_ptr_array_unref(d->tracks);
    g_array_unref(d->pending);
    g_queue_free_full(d->ready, ready_sample_free);
    g_free(d);
}

gboolean
ns_mp4_demuxer_append(ns_mp4_demuxer *d, const guint8 *data, gsize len)
{
    if (!d || d->state == PARSE_ERROR) return FALSE;
    if (len > 0 && !data) return FALSE;
    while (len > 0) {
        guint chunk = (guint)MIN(len, (gsize)(16 * 1024 * 1024));
        g_byte_array_append(d->input, data, chunk);
        data += chunk;
        len -= chunk;
        if (!process_input(d)) {
            d->state = PARSE_ERROR;
            g_byte_array_set_size(d->input, 0);
            g_array_set_size(d->pending, 0);
            d->pending_head = 0;
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
ns_mp4_demuxer_has_init(const ns_mp4_demuxer *d)
{
    return d && d->has_init;
}

gboolean
ns_mp4_demuxer_has_error(const ns_mp4_demuxer *d)
{
    return !d || d->state == PARSE_ERROR;
}

guint
ns_mp4_demuxer_n_tracks(const ns_mp4_demuxer *d)
{
    return d && d->has_init ? d->tracks->len : 0;
}

const ns_mp4_track *
ns_mp4_demuxer_track(const ns_mp4_demuxer *d, guint index)
{
    if (!d || !d->has_init || index >= d->tracks->len) return NULL;
    demux_track *track = g_ptr_array_index(d->tracks, index);
    return &track->public;
}

const ns_mp4_track *
ns_mp4_demuxer_track_by_id(const ns_mp4_demuxer *d, guint32 id)
{
    if (!d || !d->has_init) return NULL;
    demux_track *track = find_track(d->tracks, id);
    return track ? &track->public : NULL;
}

gboolean
ns_mp4_demuxer_pop_sample(ns_mp4_demuxer *d, ns_mp4_sample *out)
{
    if (!d || !out) return FALSE;
    ns_mp4_sample *sample = g_queue_pop_head(d->ready);
    if (!sample) return FALSE;
    *out = *sample;
    g_free(sample);
    return TRUE;
}

void
ns_mp4_demuxer_reset_parser(ns_mp4_demuxer *d)
{
    if (!d) return;
    clear_parser_state(d);
    for (guint i = 0; i < d->tracks->len; i++) {
        demux_track *track = g_ptr_array_index(d->tracks, i);
        track->has_next_decode_time = FALSE;
        track->next_decode_time = 0;
    }
}

void
ns_mp4_sample_clear(ns_mp4_sample *sample)
{
    if (!sample) return;
    if (sample->data) g_bytes_unref(sample->data);
    memset(sample, 0, sizeof *sample);
}

double
ns_mp4_time_to_seconds(gint64 time, guint32 timescale)
{
    return timescale ? (double)time / (double)timescale : 0.0;
}
