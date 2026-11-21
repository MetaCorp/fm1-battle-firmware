/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors */
/* FM-1 Battle: a submitted song rendered by SLOOP's own engine (the sequencer, the sections, the song order, the
 * synths, the drums, the effects: the editor's host harness), as a WAV, for the listening (CDC, Rendu audio; D21,
 * D32). It also checks the song uses the pack's sounds only.
 *   fm1_render IN OUT.wav ENGINES KIT       ENGINES "e1,e2,e3" (the pack's engines), KIT the pack's kit
 * IN: "FBSG", u8 count, then per object: u8 id (0 the loop, 2..5 the sections A..D), u32 LE length, the bytes as
 * BK_GET read them; then the 36 bytes of the song order (settings bytes 48..83).
 * Prints one JSON line: {"ok":true,"mode":"song"|"loop","ms":N,"peak":P} or {"ok":false,"reason":"..."}.
 * Length (D21): an arranged song played once, else the loop repeated up to at least 30 s; always 30 s .. 3 min, with
 * a 3 s fade when it is cut. */
#include "../tests/editor_host.c"

#define MAX_MS 180000u
#define MIN_MS 30000u
#define LOOP_MS 30000u                          /* (60 s until Léopold, 21 nov. 2026) */
#define FADE_MS 3000u

static int fail(const char *why)
{
    printf("{\"ok\":false,\"reason\":\"%s\"}\n", why);
    return 2;
}
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* the pack's sounds only (D32): each part on one of the pack's engines, no user sample slot, the pack's kit */
static const char *check_project(const project_t *p, const int *eng, int kit)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        uint32_t e = p->t[k].engine;
        if (p->t[k].preset == 0xFFu) continue;
        if ((int)e != eng[0] && (int)e != eng[1] && (int)e != eng[2]) return "an engine that is not the pack's";
    }
    if (pt_get(&p->t[TRK_DRUM], P_E0) != kit) return "a kit that is not the pack's";
    return 0;
}

int main(int argc, char **argv)
{
    static uint8_t in[64 * 1024];
    static int32_t out[CTL * 2];
    int eng[3] = {-1, -1, -1}, kit;
    uint32_t n, i, pos, count, frames, total, fade_from, f;
    int song_mode = 0, have_work = 0;
    int32_t peak = 0;
    FILE *fi, *fo;
    const char *bad;
    if (argc < 5) return fail("usage: fm1_render IN OUT.wav e1,e2,e3 KIT");
    if (sscanf(argv[3], "%d,%d,%d", &eng[0], &eng[1], &eng[2]) != 3) return fail("engines");
    kit = atoi(argv[4]);
    if (!(fi = fopen(argv[1], "rb"))) return fail("no input");
    n = (uint32_t)fread(in, 1, sizeof in, fi);
    fclose(fi);
    if (n < 5u + 36u || memcmp(in, "FBSG", 4)) return fail("not a song file");
    harness_init();
    count = in[4];
    pos = 5;
    for (i = 0; i < count; i++) {
        uint32_t id, len;
        if (pos + 5u > n) return fail("short");
        id = in[pos];
        len = rd32(in + pos + 1);
        pos += 5;
        if (pos + len > n) return fail("short object");
        if (len) {
            project_t q;
            if (!proj_import(&q, in + pos, (int)len)) return fail("an object is not a SLOOP project");
            if ((bad = check_project(&q, eng, kit))) return fail(bad);
            if (id == 0u) {
                project_apply(&q);
                have_work = 1;
            } else if (id >= 2u && id <= 5u) {
                proj_slot[id - 2u] = q;
            }
        }
        pos += len;
    }
    if (pos + 36u > n) return fail("no song order");
    memcpy(&arrangement, in + pos, sizeof arrangement);
    if (!have_work) return fail("no working loop");
    transport_req = 2;
    events_block(CTL);
    song.master_q12 = 4096;
    song_mode = arr_valid(&arrangement, arrangement_ready()) && arrangement_ready() != 0u;
    if (song_mode) {                                      /* the song once: how long is it? (a dry run) */
        uint32_t t = 0;
        arrangement_enabled = 1;
        transport_req = 1;
        events_block(CTL);
        while (song.playing && t < FS * (MAX_MS / 1000u + 1u)) {
            mix_block(out, CTL);
            t += CTL;
        }
        transport_req = 2;
        events_block(CTL);
        total = t;
        if (total < FS * (MIN_MS / 1000u)) total = (FS * (MIN_MS / 1000u) + total - 1u) / total * total;
        harness_init();                                   /* the same again, from the top, for real */
        for (pos = 5, i = 0; i < count; i++) {
            uint32_t id = in[pos], len = rd32(in + pos + 1);
            project_t q;
            pos += 5;
            if (len && proj_import(&q, in + pos, (int)len)) {
                if (id == 0u) project_apply(&q);
                else if (id >= 2u && id <= 5u) proj_slot[id - 2u] = q;
            }
            pos += len;
        }
        memcpy(&arrangement, in + pos, sizeof arrangement);
        transport_req = 2;
        events_block(CTL);
        song.master_q12 = 4096;
        arrangement_enabled = 1;
    } else {                                              /* the loop, in whole bars, up to at least 30 s */
        uint32_t bar = 4u * 60u * FS / (uint32_t)(song.g[G_BPM] > 0 ? song.g[G_BPM] : 120);
        total = (FS * (LOOP_MS / 1000u) + bar - 1u) / bar * bar;
        arrangement_enabled = 0;
    }
    if (total > FS * (MAX_MS / 1000u)) total = FS * (MAX_MS / 1000u);
    frames = total / CTL * CTL;
    fade_from = frames > FS * (FADE_MS / 1000u) ? frames - FS * (FADE_MS / 1000u) : 0u;
    if (!(fo = fopen(argv[2], "wb"))) return fail("no output");
    wav_hdr(fo, frames);
    transport_req = 1;
    for (f = 0; f < frames; f += CTL) {
        uint32_t k;
        if (song_mode && !song.playing && f) transport_req = 1;   /* (shorter than 30 s: again) */
        mix_block(out, CTL);
        for (k = 0; k < CTL; k++) {
            int32_t l = out[2 * k], r = out[2 * k + 1];
            if (f + k >= fade_from && frames > fade_from) {       /* the last 3 s fade (D21) */
                int64_t g = (int64_t)(frames - (f + k)) * 1024 / (frames - fade_from);
                l = (int32_t)(l * g / 1024);
                r = (int32_t)(r * g / 1024);
            }
            if (l > peak) peak = l;
            if (-l > peak) peak = -l;
            if (r > peak) peak = r;
            if (-r > peak) peak = -r;
            wav_put(fo, l, r);
        }
    }
    fclose(fo);
    printf("{\"ok\":true,\"mode\":\"%s\",\"ms\":%u,\"peak\":%d}\n", song_mode ? "song" : "loop",
           (uint32_t)((uint64_t)frames * 1000u / FS), peak);
    return 0;
}
