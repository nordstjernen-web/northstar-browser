/* audio/audio.c: Asynchronous in-process audio decoding and SDL2 playback.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _GNU_SOURCE
#define SDL_MAIN_HANDLED
#include "audio.h"

#include <glib.h>
#include <SDL.h>
#ifdef main
#undef main
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <curl/curl.h>

#include "pl_mpeg.h"
#include "net.h"

#ifndef MINIMP3_FLOAT_OUTPUT
#define MINIMP3_FLOAT_OUTPUT
#endif
#include "minimp3.h"


#ifdef NS_HAVE_VORBISFILE
#include <vorbis/vorbisfile.h>
#endif

#ifdef NS_HAVE_OPUSFILE
#include <opusfile.h>
#endif

#define NS_AUDIO_MAX_PLAYERS 16
#define NS_AUDIO_MAX_SECONDS 1800
#define NS_AUDIO_DEVICE_RATE 44100
#define NS_AUDIO_MAX_BYTES   (256u * 1024u * 1024u)
#define NS_AUDIO_MAX_FLOATS  ((size_t)200u * 1024u * 1024u)

typedef struct {
    NsAudioContext *owner;
    char    token[64];
    int     used;
    int     playing;
    int     loop;
    float  *pcm;
    size_t  frames;
    size_t  pcm_cap;
    size_t  cursor;
    float   volume;
    int     reached_end;
    char   *tmp_path;
    long    reload_size;
    Uint32  reload_ticks;
} ns_audio_player;

struct NsAudioContext {
    gint generation;
    gint destroyed;
};

typedef enum {
    NS_AUDIO_COMMAND_LINE,
    NS_AUDIO_COMMAND_BLOB,
    NS_AUDIO_COMMAND_RESET,
    NS_AUDIO_COMMAND_DESTROY,
    NS_AUDIO_COMMAND_QUIT,
} ns_audio_command_type;

typedef struct {
    ns_audio_command_type type;
    NsAudioContext       *context;
    int                   generation;
    char                 *line;
    char                 *token;
    GBytes               *bytes;
    gboolean              reload;
} ns_audio_command;

static SDL_AudioDeviceID g_dev;
static int               g_dev_ok;
static SDL_mutex        *g_null_lock;
static SDL_Thread       *g_null_thread;
static SDL_atomic_t      g_null_quit;
static ns_audio_player   g_players[NS_AUDIO_MAX_PLAYERS];
static GAsyncQueue      *g_commands;
static GThread          *g_worker;
static gint              g_shutting_down;
static char             *g_tmp_dir;

#if defined(__GNUC__)
#define NS_AUDIO_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define NS_AUDIO_PRINTF(a, b)
#endif

static void emit(const char *fmt, ...) NS_AUDIO_PRINTF(1, 2);

static void
emit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *line = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    if (g_str_has_prefix(line, "error ") || g_getenv("NS_DBG_AUDIO"))
        g_printerr("[audio] %s\n", line);
    g_free(line);
}

static void
audio_lock(void)
{
    if (g_dev_ok) SDL_LockAudioDevice(g_dev);
    else if (g_null_lock) SDL_LockMutex(g_null_lock);
}

static void
audio_unlock(void)
{
    if (g_dev_ok) SDL_UnlockAudioDevice(g_dev);
    else if (g_null_lock) SDL_UnlockMutex(g_null_lock);
}

static ns_audio_player *
player_find(NsAudioContext *owner, const char *token)
{
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used && g_players[i].owner == owner &&
            strcmp(g_players[i].token, token) == 0)
            return &g_players[i];
    return NULL;
}

static ns_audio_player *
player_alloc(NsAudioContext *owner, const char *token)
{
    ns_audio_player *p = player_find(owner, token);
    if (p) return p;
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        if (!g_players[i].used) {
            memset(&g_players[i], 0, sizeof g_players[i]);
            g_players[i].owner = owner;
            g_players[i].used = 1;
            g_players[i].volume = 1.0f;
            snprintf(g_players[i].token, sizeof g_players[i].token, "%s", token);
            return &g_players[i];
        }
    }
    return NULL;
}


static void
player_release(ns_audio_player *p)
{
    if (!p || !p->used) return;
    char *tmp = p->tmp_path;
    free(p->pcm);
    memset(p, 0, sizeof *p);
    if (tmp) {
        remove(tmp);
        free(tmp);
    }
}

static void
audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    float *out = (float *)(void *)stream;
    int frame_bytes = (int)(2 * sizeof(float));
    int nframes = len / frame_bytes;
    SDL_memset(stream, 0, (size_t)len);

    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        ns_audio_player *p = &g_players[i];
        if (!p->used || !p->playing || !p->pcm) continue;
        for (int f = 0; f < nframes; f++) {
            if (p->cursor >= p->frames) {
                if (p->loop && p->frames > 0) {
                    p->cursor = 0;
                } else {
                    p->playing = 0;
                    p->reached_end = 1;
                    break;
                }
            }
            out[f * 2 + 0] += p->pcm[p->cursor * 2 + 0] * p->volume;
            out[f * 2 + 1] += p->pcm[p->cursor * 2 + 1] * p->volume;
            p->cursor++;
        }
    }

    int total = nframes * 2;
    for (int i = 0; i < total; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
}

#define NS_AUDIO_NULL_FRAMES 1024

static int
null_audio_thread(void *arg)
{
    (void)arg;
    Uint8 buf[NS_AUDIO_NULL_FRAMES * 2 * sizeof(float)];
    Uint64 start = SDL_GetTicks64();
    Uint64 total_frames = 0;
    while (!SDL_AtomicGet(&g_null_quit)) {
        SDL_LockMutex(g_null_lock);
        audio_cb(NULL, buf, (int)sizeof buf);
        SDL_UnlockMutex(g_null_lock);
        total_frames += NS_AUDIO_NULL_FRAMES;
        Uint64 target = start + total_frames * 1000u / NS_AUDIO_DEVICE_RATE;
        Uint64 now = SDL_GetTicks64();
        if (target > now) {
            SDL_Delay((Uint32)(target - now));
        } else if (now - target > 1000) {
            start = now;
            total_frames = 0;
        }
    }
    return 0;
}

static void
null_audio_start(void)
{
    g_null_lock = SDL_CreateMutex();
    if (!g_null_lock) return;
    SDL_AtomicSet(&g_null_quit, 0);
    g_null_thread = SDL_CreateThread(null_audio_thread, "ns-null-audio", NULL);
    if (!g_null_thread) {
        SDL_DestroyMutex(g_null_lock);
        g_null_lock = NULL;
    }
}

static void
null_audio_stop(void)
{
    if (!g_null_thread) return;
    SDL_AtomicSet(&g_null_quit, 1);
    SDL_WaitThread(g_null_thread, NULL);
    g_null_thread = NULL;
    if (g_null_lock) {
        SDL_DestroyMutex(g_null_lock);
        g_null_lock = NULL;
    }
}

static unsigned char *
read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || (size_t)n > NS_AUDIO_MAX_BYTES) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *bytes = malloc((size_t)n);
    if (!bytes) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        free(bytes); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return bytes;
}

static int
bytes_are_mpeg1(const unsigned char *b, size_t n)
{
    return n >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 &&
           (b[3] == 0xBA || b[3] == 0xB3);
}

static int
decode_mpeg(const unsigned char *bytes, size_t n,
            float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    plm_t *plm = plm_create_with_memory((uint8_t *)bytes, n, 0);
    if (!plm) return 0;
    plm_set_video_enabled(plm, 0);
    plm_set_audio_enabled(plm, 1);
    int rate = plm_get_samplerate(plm);
    if (plm_get_num_audio_streams(plm) < 1 || rate <= 0 || rate > 48000) {
        plm_destroy(plm);
        return 0;
    }

    size_t cap = (size_t)rate * 2u * 8u;
    size_t len = 0;
    float *pcm = malloc(cap * sizeof(float));
    if (!pcm) { plm_destroy(plm); return 0; }
    size_t max_floats = (size_t)rate * 2u * NS_AUDIO_MAX_SECONDS;
    if (max_floats > NS_AUDIO_MAX_FLOATS) max_floats = NS_AUDIO_MAX_FLOATS;
    plm_samples_t *s;
    while ((s = plm_decode_audio(plm)) != NULL) {
        size_t add = (size_t)s->count * 2u;
        if (len + add > cap) {
            while (len + add > cap) {
                if (cap > SIZE_MAX / (2u * sizeof(float))) {
                    free(pcm); plm_destroy(plm); return 0;
                }
                cap *= 2;
            }
            float *grown = realloc(pcm, cap * sizeof(float));
            if (!grown) { free(pcm); plm_destroy(plm); return 0; }
            pcm = grown;
        }
        memcpy(pcm + len, s->interleaved, add * sizeof(float));
        len += add;
        if (len >= max_floats) break;
    }
    plm_destroy(plm);
    if (len == 0) { free(pcm); return 0; }

    *out_pcm = pcm;
    *out_frames = len / 2;
    *out_rate = rate;
    *out_ch = 2;
    return 1;
}

static int
decode_mp3(const unsigned char *bytes, size_t n,
           float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    if (n > (size_t)INT_MAX) return 0;

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t *p = bytes;
    int rem = (int)n;
    int rate = 0, ch = 0;
    size_t cap = 0, len = 0;
    float *pcm = NULL;
    size_t max_floats = 0;

    while (rem > 0) {
        int samples = mp3dec_decode_frame(&dec, p, rem, frame, &info);
        if (info.frame_bytes <= 0) break;
        if (samples > 0) {
            if (rate == 0) {
                rate = info.hz;
                ch = info.channels;
                if (rate <= 0 || ch < 1) { free(pcm); return 0; }
                max_floats = (size_t)rate * (size_t)ch * NS_AUDIO_MAX_SECONDS;
                if (max_floats > NS_AUDIO_MAX_FLOATS)
                    max_floats = NS_AUDIO_MAX_FLOATS;
            }
            if (info.channels == ch && info.hz == rate) {
                size_t add = (size_t)samples * (size_t)ch;
                if (len + add > cap) {
                    size_t want = cap ? cap : (size_t)rate * (size_t)ch;
                    while (len + add > want) {
                        if (want > SIZE_MAX / (2u * sizeof(float))) {
                            free(pcm); return 0;
                        }
                        want *= 2;
                    }
                    float *grown = realloc(pcm, want * sizeof(float));
                    if (!grown) { free(pcm); return 0; }
                    pcm = grown;
                    cap = want;
                }
                memcpy(pcm + len, frame, add * sizeof(float));
                len += add;
                if (len >= max_floats) break;
            }
        }
        p += info.frame_bytes;
        rem -= info.frame_bytes;
    }

    if (rate == 0 || len == 0) { free(pcm); return 0; }

    *out_pcm = pcm;
    *out_frames = len / (size_t)ch;
    *out_rate = rate;
    *out_ch = ch;
    return 1;
}

#ifdef NS_HAVE_VORBISFILE
typedef struct { const unsigned char *data; size_t size; size_t pos; } ns_ogg_mem;

static size_t
ns_ogg_read(void *ptr, size_t size, size_t nmemb, void *src)
{
    ns_ogg_mem *m = src;
    size_t want = size * nmemb;
    size_t avail = m->size - m->pos;
    if (want > avail) want = avail;
    memcpy(ptr, m->data + m->pos, want);
    m->pos += want;
    return (size != 0) ? want / size : 0;
}

static int
ns_ogg_seek(void *src, ogg_int64_t offset, int whence)
{
    ns_ogg_mem *m = src;
    ogg_int64_t base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (ogg_int64_t)m->pos;
    else if (whence == SEEK_END) base = (ogg_int64_t)m->size;
    else return -1;
    ogg_int64_t np = base + offset;
    if (np < 0 || np > (ogg_int64_t)m->size) return -1;
    m->pos = (size_t)np;
    return 0;
}

static long
ns_ogg_tell(void *src)
{
    ns_ogg_mem *m = src;
    return (long)m->pos;
}

static int
bytes_are_ogg(const unsigned char *b, size_t n)
{
    return n >= 4 && b[0] == 'O' && b[1] == 'g' && b[2] == 'g' && b[3] == 'S';
}

static int
decode_vorbis(const unsigned char *bytes, size_t n,
              float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    ns_ogg_mem mem = { bytes, n, 0 };
    ov_callbacks cb = { ns_ogg_read, ns_ogg_seek, NULL, ns_ogg_tell };
    OggVorbis_File vf;
    if (ov_open_callbacks(&mem, &vf, NULL, 0, cb) < 0) return 0;

    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || vi->rate <= 0 || vi->channels < 1) { ov_clear(&vf); return 0; }
    int rate = (int)vi->rate;
    int ch = vi->channels;

    size_t max_floats = (size_t)rate * (size_t)ch * NS_AUDIO_MAX_SECONDS;
    if (max_floats > NS_AUDIO_MAX_FLOATS) max_floats = NS_AUDIO_MAX_FLOATS;

    float *pcm = NULL;
    size_t cap = 0, len = 0;
    int bitstream = 0;
    for (;;) {
        float **chans = NULL;
        long got = ov_read_float(&vf, &chans, 4096, &bitstream);
        if (got == 0) break;
        if (got < 0) continue;
        size_t add = (size_t)got * (size_t)ch;
        if (len + add > cap) {
            size_t want = cap ? cap : (size_t)rate * (size_t)ch;
            while (len + add > want) {
                if (want > SIZE_MAX / (2u * sizeof(float))) { free(pcm); ov_clear(&vf); return 0; }
                want *= 2;
            }
            float *grown = realloc(pcm, want * sizeof(float));
            if (!grown) { free(pcm); ov_clear(&vf); return 0; }
            pcm = grown;
            cap = want;
        }
        for (long i = 0; i < got; i++)
            for (int c = 0; c < ch; c++)
                pcm[len + (size_t)i * (size_t)ch + (size_t)c] = chans[c][i];
        len += add;
        if (len >= max_floats) break;
    }
    ov_clear(&vf);

    if (len == 0) { free(pcm); return 0; }
    *out_pcm = pcm;
    *out_frames = len / (size_t)ch;
    *out_rate = rate;
    *out_ch = ch;
    return 1;
}
#endif

#ifdef NS_HAVE_OPUSFILE
static int
decode_opus(const unsigned char *bytes, size_t n,
            float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    OggOpusFile *of = op_open_memory(bytes, n, NULL);
    if (!of) return 0;
    int ch = op_channel_count(of, -1);
    if (ch < 1 || ch > 8) { op_free(of); return 0; }
    int rate = 48000;

    size_t max_floats = (size_t)rate * (size_t)ch * NS_AUDIO_MAX_SECONDS;
    if (max_floats > NS_AUDIO_MAX_FLOATS) max_floats = NS_AUDIO_MAX_FLOATS;

    int frame_cap = 5760 * ch;
    float *frame = malloc((size_t)frame_cap * sizeof(float));
    if (!frame) { op_free(of); return 0; }

    float *pcm = NULL;
    size_t cap = 0, len = 0;
    for (;;) {
        int got = op_read_float(of, frame, frame_cap, NULL);
        if (got == 0) break;
        if (got < 0) { free(frame); free(pcm); op_free(of); return 0; }
        size_t add = (size_t)got * (size_t)ch;
        if (len + add > cap) {
            size_t want = cap ? cap : (size_t)rate * (size_t)ch;
            while (len + add > want) {
                if (want > SIZE_MAX / (2u * sizeof(float))) {
                    free(frame); free(pcm); op_free(of); return 0;
                }
                want *= 2;
            }
            float *grown = realloc(pcm, want * sizeof(float));
            if (!grown) { free(frame); free(pcm); op_free(of); return 0; }
            pcm = grown;
            cap = want;
        }
        memcpy(pcm + len, frame, add * sizeof(float));
        len += add;
        if (len >= max_floats) break;
    }
    free(frame);
    op_free(of);

    if (len == 0) { free(pcm); return 0; }
    *out_pcm = pcm;
    *out_frames = len / (size_t)ch;
    *out_rate = rate;
    *out_ch = ch;
    return 1;
}
#endif


static int
resample_to_device(const float *src, size_t src_frames, int src_rate, int src_ch,
                   float **out_pcm, size_t *out_frames)
{
    if (src_ch < 1 || src_rate <= 0 || src_frames == 0) return 0;
    size_t src_bytes = src_frames * (size_t)src_ch * sizeof(float);
    if (src_bytes > (size_t)INT_MAX) return 0;

    SDL_AudioStream *st = SDL_NewAudioStream(
        AUDIO_F32SYS, (Uint8)src_ch, src_rate,
        AUDIO_F32SYS, 2, NS_AUDIO_DEVICE_RATE);
    if (!st) return 0;
    if (SDL_AudioStreamPut(st, src, (int)src_bytes) < 0) {
        SDL_FreeAudioStream(st); return 0;
    }
    SDL_AudioStreamFlush(st);
    int avail = SDL_AudioStreamAvailable(st);
    if (avail <= 0) { SDL_FreeAudioStream(st); return 0; }
    float *buf = malloc((size_t)avail);
    if (!buf) { SDL_FreeAudioStream(st); return 0; }
    int got = SDL_AudioStreamGet(st, buf, avail);
    SDL_FreeAudioStream(st);
    if (got <= 0) { free(buf); return 0; }

    *out_pcm = buf;
    *out_frames = (size_t)got / (2 * sizeof(float));
    return 1;
}

static int
load_audio_bytes(ns_audio_player *p, const unsigned char *bytes, size_t n)
{
    if (!bytes || n == 0 || n > NS_AUDIO_MAX_BYTES) return 0;

    float *src = NULL;
    size_t src_frames = 0;
    int src_rate = 0, src_ch = 0;
    int ok = 0;
    if (bytes_are_mpeg1(bytes, n))
        ok = decode_mpeg(bytes, n, &src, &src_frames, &src_rate, &src_ch);
    if (!ok)
        ok = decode_mp3(bytes, n, &src, &src_frames, &src_rate, &src_ch);
#ifdef NS_HAVE_VORBISFILE
    if (!ok && bytes_are_ogg(bytes, n))
        ok = decode_vorbis(bytes, n, &src, &src_frames, &src_rate, &src_ch);
#endif
#ifdef NS_HAVE_OPUSFILE
    if (!ok)
        ok = decode_opus(bytes, n, &src, &src_frames, &src_rate, &src_ch);
#endif
    if (!ok) return 0;

    float *dev = NULL;
    size_t dev_frames = 0;
    ok = resample_to_device(src, src_frames, src_rate, src_ch, &dev, &dev_frames);
    free(src);
    if (!ok) return 0;

    audio_lock();
    p->pcm = dev;
    p->frames = dev_frames;
    p->pcm_cap = dev_frames * 2;
    p->cursor = 0;
    p->reached_end = 0;
    p->playing = 0;
    audio_unlock();
    return 1;
}

static int
load_audio(ns_audio_player *p, const char *path)
{
    size_t n = 0;
    unsigned char *bytes = read_file(path, &n);
    if (!bytes) return 0;
    int ok = load_audio_bytes(p, bytes, n);
    free(bytes);
    return ok;
}

typedef struct {
    FILE   *file;
    size_t  len;
    NsAudioContext *context;
    int generation;
} ns_audio_download;

static size_t
curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ns_audio_download *d = userdata;
    if (!d || !d->file) return 0;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t bytes = size * nmemb;
    if (d->len > NS_AUDIO_MAX_BYTES) return 0;
    if (bytes > NS_AUDIO_MAX_BYTES - d->len) return 0;
    size_t wrote = fwrite(ptr, 1, bytes, d->file);
    if (wrote != bytes) return wrote;
    d->len += wrote;
    return wrote;
}

static const char *
audio_tmp_dir(void)
{
    if (!g_tmp_dir) {
        g_tmp_dir = g_build_filename(g_get_user_cache_dir(), "northstar",
                                     "msaudio", NULL);
        if (g_mkdir_with_parents(g_tmp_dir, 0700) != 0)
            return NULL;
    }
    return g_tmp_dir;
}

static int
curl_progress_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                 curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    ns_audio_download *d = userdata;
    return g_atomic_int_get(&g_shutting_down) ||
           g_atomic_int_get(&d->context->destroyed) ||
           g_atomic_int_get(&d->context->generation) != d->generation;
}

static char *
write_temp_from_url(NsAudioContext *context, const char *url)
{
    char tmpl[PATH_MAX];
    const char *dir = audio_tmp_dir();
    if (!dir) return NULL;
    snprintf(tmpl, sizeof tmpl, "%s/nsaudio-XXXXXX", dir);
#if defined(_WIN32)
    int fd = mkstemp(tmpl);
#else
    int fd = mkostemp(tmpl, O_CLOEXEC);
#endif
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); remove(tmpl); return NULL; }

    int ok = 0;
    CURL *c = curl_easy_init();
    if (c) {
        ns_audio_download dl = {
            f, 0, context, g_atomic_int_get(&context->generation)
        };
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &dl);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
#ifdef CURLOPT_PROTOCOLS_STR
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https,data");
#endif
#ifdef CURLOPT_REDIR_PROTOCOLS_STR
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR,
                         strncmp(url, "https://", 8) == 0 ? "https" :
                         strncmp(url, "data:", 5) == 0 ? "data" :
                         "http,https");
#endif
#ifdef CURLOPT_MAXFILESIZE_LARGE
        curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE,
                         (curl_off_t)NS_AUDIO_MAX_BYTES);
#endif
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "Northstar-Audio");
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &dl);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        const char *proxy = ns_net_proxy_override();
        if (!proxy || !*proxy)
            proxy = strncmp(url, "https://", 8) == 0
                ? ns_net_https_proxy() : ns_net_http_proxy();
        if (proxy && *proxy)
            curl_easy_setopt(c, CURLOPT_PROXY, proxy);
        const char *no_proxy = ns_net_no_proxy();
        if (no_proxy && *no_proxy)
            curl_easy_setopt(c, CURLOPT_NOPROXY, no_proxy);
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
        curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
        const char *ca = ns_net_ca_bundle_path();
        if (!ca || !*ca) ca = getenv("CURL_CA_BUNDLE");
        if (!ca || !*ca) ca = getenv("SSL_CERT_FILE");
        if (ca && *ca)
            curl_easy_setopt(c, CURLOPT_CAINFO, ca);
        ok = curl_easy_perform(c) == CURLE_OK;
        curl_easy_cleanup(c);
    }
    fclose(f);
    if (!ok) { remove(tmpl); return NULL; }
    return strdup(tmpl);
}

static const char *
local_path_for(NsAudioContext *context, const char *url, char **tmp_out)
{
    *tmp_out = NULL;
    if (strncmp(url, "file://", 7) == 0) {
        const char *p = url + 7;
        if (strncmp(p, "localhost", 9) == 0) p += 9;
        return p;
    }
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "data:", 5) == 0) {
        char *t = write_temp_from_url(context, url);
        if (!t) return NULL;
        *tmp_out = t;
        return t;
    }
    return url;
}


static void
cmd_open(NsAudioContext *context, const char *token, const char *url)
{
    ns_audio_player *p = player_alloc(context, token);
    if (!p) { emit("error %s too-many-players", token); return; }
    audio_lock();
    player_release(p);
    audio_unlock();
    p = player_alloc(context, token);

    if (!g_dev_ok && !g_null_thread) {
        emit("error %s no-audio-device", token);
        return;
    }

    char *tmp = NULL;
    const char *path = local_path_for(context, url, &tmp);
    if (!path) { emit("error %s fetch-failed", token); return; }
    p->tmp_path = tmp;


    if (!load_audio(p, path)) {
        emit("error %s decode-failed", token);
        audio_lock();
        player_release(p);
        audio_unlock();
        return;
    }

    double len = (double)p->frames / NS_AUDIO_DEVICE_RATE;
    emit("meta %s %.3f", token, len);
}

static void
cmd_reload(NsAudioContext *context, const char *token, const char *url)
{
    ns_audio_player *p = player_find(context, token);
    if (!p) { cmd_open(context, token, url); return; }

    char *tmp = NULL;
    const char *path = local_path_for(context, url, &tmp);
    if (!path) { emit("error %s fetch-failed", token); free(tmp); return; }


    struct stat st;
    long size_now = stat(path, &st) == 0 ? (long)st.st_size : -1;
    Uint32 now = SDL_GetTicks();
    int behind = p->reached_end || p->cursor + NS_AUDIO_DEVICE_RATE >= p->frames;
    Uint32 min_gap = behind ? 1500 : 6000;
    if (size_now >= 0 && size_now == p->reload_size &&
        p->reload_ticks && now - p->reload_ticks < 30000) {
        if (tmp) { unlink(tmp); free(tmp); }
        return;
    }
    if (p->reload_ticks && now - p->reload_ticks < min_gap) {
        if (tmp) { unlink(tmp); free(tmp); }
        return;
    }

    ns_audio_player fresh;
    memset(&fresh, 0, sizeof fresh);
    if (!load_audio(&fresh, path)) {
        emit("error %s decode-failed", token);
        if (tmp) { unlink(tmp); free(tmp); }
        return;
    }

    audio_lock();
    float *old_pcm = p->pcm;
    char *old_tmp = p->tmp_path;
    p->pcm = fresh.pcm;
    p->frames = fresh.frames;
    p->pcm_cap = fresh.frames * 2;
    if (p->cursor > p->frames) p->cursor = p->frames;
    if (p->reached_end && p->cursor < p->frames) {
        p->reached_end = 0;
        p->playing = 1;
    }
    p->tmp_path = tmp;
    p->reload_size = size_now;
    p->reload_ticks = now;
    audio_unlock();
    free(old_pcm);
    if (old_tmp) { unlink(old_tmp); free(old_tmp); }

    double len = (double)p->frames / NS_AUDIO_DEVICE_RATE;
    emit("meta %s %.3f", token, len);
}

static void
cmd_open_bytes(NsAudioContext *context, const char *token, GBytes *bytes)
{
    ns_audio_player *p = player_alloc(context, token);
    if (!p) { emit("error %s too-many-players", token); return; }
    audio_lock();
    player_release(p);
    audio_unlock();
    p = player_alloc(context, token);

    if (!g_dev_ok && !g_null_thread) {
        emit("error %s no-audio-device", token);
        return;
    }

    gsize size = 0;
    const guint8 *data = g_bytes_get_data(bytes, &size);
    if (!load_audio_bytes(p, data, size)) {
        emit("error %s decode-failed", token);
        audio_lock();
        player_release(p);
        audio_unlock();
        return;
    }
    p->reload_size = (long)size;
    p->reload_ticks = SDL_GetTicks();
    emit("meta %s %.3f", token,
         (double)p->frames / NS_AUDIO_DEVICE_RATE);
}

static void
cmd_reload_bytes(NsAudioContext *context, const char *token, GBytes *bytes)
{
    ns_audio_player *p = player_find(context, token);
    if (!p) { cmd_open_bytes(context, token, bytes); return; }

    gsize size = 0;
    const guint8 *data = g_bytes_get_data(bytes, &size);
    if (size == 0) {
        audio_lock();
        player_release(p);
        audio_unlock();
        return;
    }
    Uint32 now = SDL_GetTicks();
    if ((gsize)p->reload_size == size) return;

    ns_audio_player fresh;
    memset(&fresh, 0, sizeof fresh);
    if (!load_audio_bytes(&fresh, data, size)) {
        emit("error %s decode-failed", token);
        return;
    }

    audio_lock();
    float *old_pcm = p->pcm;
    p->pcm = fresh.pcm;
    p->frames = fresh.frames;
    p->pcm_cap = fresh.frames * 2;
    if (p->cursor > p->frames) p->cursor = p->frames;
    if (p->reached_end && p->cursor < p->frames) {
        p->reached_end = 0;
        p->playing = 1;
    }
    p->reload_size = (long)size;
    p->reload_ticks = now;
    audio_unlock();
    free(old_pcm);
    emit("meta %s %.3f", token,
         (double)p->frames / NS_AUDIO_DEVICE_RATE);
}

static void
cmd_play(NsAudioContext *context, const char *token)
{
    ns_audio_player *p = player_find(context, token);
    if (!p || !p->pcm) return;
    audio_lock();
    if (p->cursor >= p->frames) p->cursor = 0;
    p->reached_end = 0;
    p->playing = 1;
    audio_unlock();
    emit("playing %s", token);
}

static void
cmd_pause(NsAudioContext *context, const char *token)
{
    ns_audio_player *p = player_find(context, token);
    if (!p || !p->pcm) return;
    audio_lock();
    p->playing = 0;
    audio_unlock();
    emit("paused %s", token);
}

static void
cmd_seek(NsAudioContext *context, const char *token, double seconds)
{
    ns_audio_player *p = player_find(context, token);
    if (!p || !p->pcm) return;
    if (seconds < 0) seconds = 0;
    size_t frame = (size_t)(seconds * NS_AUDIO_DEVICE_RATE);
    audio_lock();
    if (frame > p->frames) frame = p->frames;
    p->cursor = frame;
    p->reached_end = 0;
    audio_unlock();
}

static void
cmd_volume(NsAudioContext *context, const char *token, double vol)
{
    ns_audio_player *p = player_find(context, token);
    if (!p || !p->pcm) return;
    if (vol < 0) vol = 0;
    if (vol > 1) vol = 1;
    audio_lock();
    p->volume = (float)vol;
    audio_unlock();
}

static void
cmd_loop(NsAudioContext *context, const char *token, int on)
{
    ns_audio_player *p = player_find(context, token);
    if (!p) return;
    audio_lock();
    p->loop = on ? 1 : 0;
    audio_unlock();
}

static void
cmd_stop(NsAudioContext *context, const char *token)
{
    ns_audio_player *p = player_find(context, token);
    if (!p) return;
    audio_lock();
    player_release(p);
    audio_unlock();
}

static void
poll_players(NsAudioContext *context)
{
    struct { char token[64]; double pos; int ended; int active; }
        snap[NS_AUDIO_MAX_PLAYERS];
    int m = 0;
    audio_lock();
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        ns_audio_player *p = &g_players[i];
        if (!p->used || p->owner != context || !p->pcm) continue;
        if (p->playing || p->reached_end) {
            memcpy(snap[m].token, p->token, sizeof snap[m].token);
            snap[m].pos = (double)p->cursor / NS_AUDIO_DEVICE_RATE;
            snap[m].ended = p->reached_end;
            snap[m].active = p->playing;
            p->reached_end = 0;
            m++;
        }
    }
    audio_unlock();

    for (int i = 0; i < m; i++) {
        if (snap[i].active) emit("pos %s %.3f", snap[i].token, snap[i].pos);
        if (snap[i].ended) emit("ended %s", snap[i].token);
    }
}

static char *
next_token(char **cursor)
{
    char *s = *cursor;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') { *cursor = s; return NULL; }
    char *start = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) { *s = '\0'; s++; }
    *cursor = s;
    return start;
}

static void
release_context_players(NsAudioContext *context)
{
    audio_lock();
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used && g_players[i].owner == context)
            player_release(&g_players[i]);
    audio_unlock();
}

static void
process_line(NsAudioContext *context, char *line)
{
    char *cur = line;
    char *op = next_token(&cur);
    if (!op || strcmp(op, "poll") == 0) {
        poll_players(context);
        return;
    }

    char *token = next_token(&cur);
    if (!token) return;

    if (strcmp(op, "open") == 0) {
        while (*cur == ' ') cur++;
        cmd_open(context, token, cur);
    } else if (strcmp(op, "play") == 0) {
        cmd_play(context, token);
    } else if (strcmp(op, "pause") == 0) {
        cmd_pause(context, token);
    } else if (strcmp(op, "seek") == 0) {
        char *value = next_token(&cur);
        cmd_seek(context, token, value ? atof(value) : 0.0);
    } else if (strcmp(op, "volume") == 0) {
        char *value = next_token(&cur);
        cmd_volume(context, token, value ? atof(value) : 1.0);
    } else if (strcmp(op, "loop") == 0) {
        char *value = next_token(&cur);
        cmd_loop(context, token, value ? atoi(value) : 0);
    } else if (strcmp(op, "stop") == 0) {
        cmd_stop(context, token);
    } else if (strcmp(op, "reload") == 0) {
        while (*cur == ' ') cur++;
        cmd_reload(context, token, cur);
    }
}

static gpointer
audio_worker(gpointer data)
{
    (void)data;
    for (;;) {
        ns_audio_command *command = g_async_queue_pop(g_commands);
        if (command->type == NS_AUDIO_COMMAND_QUIT) {
            g_free(command);
            break;
        }
        if (command->type == NS_AUDIO_COMMAND_RESET) {
            release_context_players(command->context);
        } else if (command->type == NS_AUDIO_COMMAND_DESTROY) {
            release_context_players(command->context);
            g_free(command->context);
        } else if (!g_atomic_int_get(&g_shutting_down) &&
                   !g_atomic_int_get(&command->context->destroyed) &&
                   command->generation ==
                       g_atomic_int_get(&command->context->generation)) {
            if (command->type == NS_AUDIO_COMMAND_BLOB) {
                if (command->reload)
                    cmd_reload_bytes(command->context, command->token,
                                     command->bytes);
                else
                    cmd_open_bytes(command->context, command->token,
                                   command->bytes);
            } else {
                process_line(command->context, command->line);
            }
        }
        g_free(command->line);
        g_free(command->token);
        if (command->bytes) g_bytes_unref(command->bytes);
        g_free(command);
    }
    return NULL;
}

static gboolean
audio_start(void)
{
    if (g_worker) return TRUE;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return FALSE;
    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof want);
        want.freq = NS_AUDIO_DEVICE_RATE;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_cb;
        g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (g_dev) {
            g_dev_ok = 1;
            SDL_PauseAudioDevice(g_dev, 0);
        }
    }
    if (!g_dev_ok) null_audio_start();
    g_atomic_int_set(&g_shutting_down, 0);
    g_commands = g_async_queue_new();
    g_worker = g_thread_new("ns-audio", audio_worker, NULL);
    emit("ready %s", g_dev_ok || g_null_thread ? "1" : "0");
    return TRUE;
}

static void
queue_command(ns_audio_command_type type, NsAudioContext *context,
              const char *line)
{
    ns_audio_command *command = g_new0(ns_audio_command, 1);
    command->type = type;
    command->context = context;
    command->generation = context
        ? g_atomic_int_get(&context->generation) : 0;
    command->line = g_strdup(line);
    g_async_queue_push(g_commands, command);
}

NsAudioContext *
ns_audio_context_new(void)
{
    return g_new0(NsAudioContext, 1);
}

void
ns_audio_context_dispatch(NsAudioContext *context, const char *command)
{
    if (!context || !command || !*command ||
        g_atomic_int_get(&context->destroyed))
        return;
    if (!audio_start()) {
        g_printerr("northstar: audio playback could not initialize\n");
        return;
    }
    queue_command(NS_AUDIO_COMMAND_LINE, context, command);
}

void
ns_audio_context_dispatch_blob(NsAudioContext *context,
                               const char *token, GBytes *bytes,
                               gboolean reload)
{
    if (!context || !token || !*token || !bytes ||
        g_atomic_int_get(&context->destroyed))
        return;
    if (!audio_start()) {
        g_printerr("northstar: audio playback could not initialize\n");
        return;
    }
    ns_audio_command *command = g_new0(ns_audio_command, 1);
    command->type = NS_AUDIO_COMMAND_BLOB;
    command->context = context;
    command->generation = g_atomic_int_get(&context->generation);
    command->token = g_strdup(token);
    command->bytes = g_bytes_ref(bytes);
    command->reload = reload;
    g_async_queue_push(g_commands, command);
}

void
ns_audio_context_reset(NsAudioContext *context)
{
    if (!context || g_atomic_int_get(&context->destroyed)) return;
    g_atomic_int_inc(&context->generation);
    if (g_worker)
        queue_command(NS_AUDIO_COMMAND_RESET, context, NULL);
}

void
ns_audio_context_destroy(NsAudioContext *context)
{
    if (!context ||
        !g_atomic_int_compare_and_exchange(&context->destroyed, 0, 1))
        return;
    g_atomic_int_inc(&context->generation);
    if (g_worker)
        queue_command(NS_AUDIO_COMMAND_DESTROY, context, NULL);
    else
        g_free(context);
}

void
ns_audio_shutdown(void)
{
    if (!g_worker) return;
    g_atomic_int_set(&g_shutting_down, 1);
    queue_command(NS_AUDIO_COMMAND_QUIT, NULL, NULL);
    g_thread_join(g_worker);
    g_worker = NULL;
    null_audio_stop();
    if (g_dev_ok) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
        g_dev_ok = 0;
    }
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used) player_release(&g_players[i]);
    ns_audio_command *command;
    while ((command = g_async_queue_try_pop(g_commands))) {
        if (command->type == NS_AUDIO_COMMAND_DESTROY)
            g_free(command->context);
        g_free(command->line);
        g_free(command);
    }
    g_async_queue_unref(g_commands);
    g_commands = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    curl_global_cleanup();
    g_clear_pointer(&g_tmp_dir, g_free);
}
