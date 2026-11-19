/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors */
/* FM-1 Battle: a simulated FM-1 running the battle firmware (SLOOP and the battle mode, on the editor's host
 * harness: the real editor SysEx, storage on a RAM image of the NOR, UI, sequencer and audio), in real time, for the
 * web app's development and tests without a machine. One line per message on stdin / stdout:
 *   in   S <hex>            a SysEx frame (the bytes between F0 and F7), as USB-MIDI packets
 *        B <button> <0|1>   a button (0..13: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+) up / down
 *        K <key> <0|1>      a note key (0 = F3 .. 26 = G5)
 *        E <knob> <steps>   a knob turned (0 SELECT, 1 ALGO, 2 PRESET, 3..6 KNOB 1..4)
 *        U <0|1>            the USB cable out / in (the site sees nothing while out)
 *        P                  a power cycle (the RAM lost, the flash kept)
 *        R                  the whole screen again
 *   out  S <hex>            a SysEx frame from the FM-1 (a reply or a push)
 *        F <base64>         the screen, 240 x 240 RGB565 big-endian, when it changed (at most 12 a second)
 *        L <level>          the master level 0..100 (a meter for the page), with each screen
 *        I <text>           what the simulator says (started, power cycle...)
 *   fm1_sim [--flash FILE]  the NOR image, kept between runs (written every 5 s when it changed) */
#define BATTLE_MODE 1
#define BT_FL_ERASE(off) ({ memset(nor + (off), 0xFF, 0x1000u); 0; })
#define BT_FL_WRITE(off, p, n) ({ const uint8_t *s_ = (const uint8_t *)(p); uint32_t i_; \
                                 for (i_ = 0; i_ < (uint32_t)(n); i_++) nor[(off) + i_] &= s_[i_]; 0; })
#include "../tests/editor_host.c"
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static const char *flash_path;
static uint32_t flash_hash_saved, usb_in = 1;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static uint32_t hash_bytes(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* ---- out */
static void out_sysex(const uint8_t *f, uint32_t n)        /* f: F0 .. F7 */
{
    static const char HX[] = "0123456789abcdef";
    uint32_t i;
    if (!usb_in || n < 2u) return;
    fputs("S ", stdout);
    for (i = 1; i + 1u < n; i++) { putchar(HX[f[i] >> 4]); putchar(HX[f[i] & 15u]); }
    putchar('\n');
}
static uint8_t txf[2048];
static uint32_t txn;
static void drain(void)                                    /* the SysEx ring, as frames (rx_take's walk) */
{
    while (so_r != so_w) {
        uint32_t pkt = sx_out_q[so_r % SXQ], cin = pkt & 15u, k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        so_r++;
        for (k = 0; k < nb; k++) {
            uint8_t b = (uint8_t)(pkt >> (8u * (k + 1u)));
            if (b == 0xF0u) txn = 0;
            if (txn < sizeof txf) txf[txn++] = b;
            if (b == 0xF7u) { out_sysex(txf, txn); txn = 0; }
        }
    }
}
/* a reply longer than the SysEx ring (64 packets) waits in ota_wire_send, whose ota_idle is the harness's rx_take:
 * that part of the frame is in rx_last; the rest is still in the ring for drain() */
static void take_rx(void)
{
    if (!rx_n) return;
    if (rx_last[0] == 0xF0u && rx_last[rx_n - 1u] == 0xF7u) {
        out_sysex(rx_last, rx_n);
    } else if (rx_last[0] == 0xF0u && rx_n <= sizeof txf) {
        memcpy(txf, rx_last, rx_n);
        txn = rx_n;
    }
    rx_n = 0;
}
static void out_screen(int force)
{
    static uint32_t last;
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static char line[240 * 240 * 2 / 3 * 4 + 16];
    const uint8_t *p = (const uint8_t *)screen;
    uint32_t h = hash_bytes(p, sizeof screen), i, n = 0, lvl;
    if (!force && h == last) return;
    last = h;
    for (i = 0; i + 2u < sizeof screen; i += 3u) {
        uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
        line[n++] = B64[v >> 18]; line[n++] = B64[(v >> 12) & 63u]; line[n++] = B64[(v >> 6) & 63u]; line[n++] = B64[v & 63u];
    }
    if (i < sizeof screen) {                               /* 115200 % 3 == 0: never; kept for clarity */
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1u < sizeof screen ? (uint32_t)p[i + 1] << 8 : 0u);
        line[n++] = B64[v >> 18]; line[n++] = B64[(v >> 12) & 63u];
        line[n++] = i + 1u < sizeof screen ? B64[(v >> 6) & 63u] : '='; line[n++] = '=';
    }
    line[n] = 0;
    printf("F %s\n", line);
    lvl = (uint32_t)(audio_peak > 32767 ? 100 : audio_peak * 100 / 32768);
    printf("L %u\n", lvl);
    audio_peak = 0;
}

/* ---- the flash, kept in a file */
static void flash_load(void)
{
    FILE *f;
    if (!flash_path || !(f = fopen(flash_path, "rb"))) return;
    if (fread(nor, 1, sizeof nor, f) != sizeof nor) memset(nor, 0xFF, sizeof nor);
    fclose(f);
}
static void flash_save(int force)
{
    FILE *f;
    uint32_t h = hash_bytes(nor, sizeof nor);
    if (!flash_path || (!force && h == flash_hash_saved)) return;
    if ((f = fopen(flash_path, "wb"))) {
        fwrite(nor, 1, sizeof nor, f);
        fclose(f);
        flash_hash_saved = h;
    }
}

/* ---- boot: SLOOP's (as harness_init, without erasing the flash), then the battle's */
static void boot(void)
{
    uint32_t i;
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    memset(&btr, 0, sizeof btr);
    persist_boot();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    autosave_resume();
    battle_boot();
    go_home();
    usb.config = usb_in ? 1u : 0u;
    ui.force = 1;
}

/* ---- in: a frame through USB-MIDI packets (usb.c midi_in_event), as the harness's usb_sysex, the replies left in
 * the ring for drain() */
static void sim_sysex(const uint8_t *f, uint32_t n)
{
    uint8_t b[704];
    uint32_t m = 0, i;
    b[m++] = 0xF0;
    for (i = 0; i < n && m < sizeof b - 1; i++) b[m++] = f[i];
    b[m++] = 0xF7;
    for (i = 0; i < m; i += 3) {
        uint32_t k = m - i >= 3 ? 3 : m - i, cin = k == 3 && i + 3 < m ? 4u : k == 3 ? 7u : 4u + k;
        uint32_t pkt = cin | (uint32_t)b[i] << 8 | (k > 1 ? (uint32_t)b[i + 1] << 16 : 0) | (k > 2 ? (uint32_t)b[i + 2] << 24 : 0);
        midi_in_event(pkt);
    }
    ed_service();
    sx_ready = 0;
}
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void handle(char *l)
{
    int a = 0, b = 0;
    if (l[0] == 'S' && l[1] == ' ') {
        static uint8_t f[700];
        uint32_t n = 0;
        char *p = l + 2;
        while (p[0] && p[1] && n < sizeof f) {
            int hi = hexval(p[0]), lo = hexval(p[1]);
            if (hi < 0 || lo < 0) break;
            f[n++] = (uint8_t)(hi << 4 | lo);
            p += 2;
        }
        if (usb_in && n >= 4u) { rx_n = 0; sim_sysex(f, n); take_rx(); drain(); }
    } else if (l[0] == 'B' && sscanf(l + 1, "%d %d", &a, &b) == 2 && a >= 0 && a < (int)NB) {
        uint32_t bit = 1u << panel.btn[a];
        if (b) { fm1_in.buttons |= bit; edges_btn |= bit; } else fm1_in.buttons &= ~bit;
    } else if (l[0] == 'K' && sscanf(l + 1, "%d %d", &a, &b) == 2 && a >= 0 && a < 27) {
        if (b) fm1_in.notes |= 1u << a; else fm1_in.notes &= ~(1u << a);
    } else if (l[0] == 'E' && sscanf(l + 1, "%d %d", &a, &b) == 2 && a >= 0 && a < (int)NE) {
        encs[a] += b;
    } else if (l[0] == 'U' && sscanf(l + 1, "%d", &a) == 1) {
        usb_in = a != 0;
        usb.config = usb_in ? 1u : 0u;
        printf("I usb %s\n", usb_in ? "in" : "out");
    } else if (l[0] == 'P') {
        flash_save(1);
        memset(proj_slot, 0xA5, sizeof proj_slot);         /* (.noinit after a power-off) */
        memset(&bt, 0, sizeof bt);
        fm1_in.buttons = fm1_in.notes = 0;
        boot();
        printf("I power cycle\n");
    } else if (l[0] == 'R') {
        ui.force = 1;
        out_screen(1);
    }
}

int main(int argc, char **argv)
{
    static char buf[1 << 16];
    size_t have = 0;
    uint64_t t_next, t_screen = 0, t_flash;
    int i;
    for (i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--flash")) flash_path = argv[i + 1];
    setvbuf(stdout, 0, _IOFBF, 1 << 20);
    memset(nor, 0xFF, sizeof nor);
    flash_load();
    flash_hash_saved = hash_bytes(nor, sizeof nor);
    boot();
    printf("I FM-1 Battle simulator: %s, %s\n", BT_FW_NAME " " BT_FW_VERSION, FELUCCA_VERSION);
    fflush(stdout);
    t_next = now_ms();
    t_flash = t_next + 5000u;
    for (;;) {
        struct pollfd pf = {0, POLLIN, 0};
        uint64_t now = now_ms();
        int wait = t_next > now ? (int)(t_next - now) : 0;
        if (poll(&pf, 1, wait) > 0 && (pf.revents & (POLLIN | POLLHUP))) {
            ssize_t r = read(0, buf + have, sizeof buf - have - 1);
            if (r <= 0) { if (r < 0 && errno == EINTR) continue; break; }
            have += (size_t)r;
            buf[have] = 0;
            for (;;) {
                char *nl = memchr(buf, '\n', have);
                size_t k;
                if (!nl) { if (have == sizeof buf - 1) have = 0; break; }
                *nl = 0;
                if (nl > buf && nl[-1] == '\r') nl[-1] = 0;
                handle(buf);
                k = (size_t)(nl - buf) + 1u;
                memmove(buf, nl + 1, have - k);
                have -= k;
            }
        }
        now = now_ms();
        while (now >= t_next) {                            /* the FM-1's frame: 16 ms of audio, the UI, the battle */
            rx_n = 0;
            ui_frame();
            battle_poll();
            take_rx();
            drain();
            t_next += 16u;
            if (now > t_next + 500u) t_next = now;         /* (a stall: no catching up for seconds) */
        }
        if (now >= t_screen) { out_screen(0); t_screen = now + 83u; }
        if (now >= t_flash) { flash_save(0); t_flash = now + 5000u; }
        fflush(stdout);
    }
    flash_save(1);
    return 0;
}
