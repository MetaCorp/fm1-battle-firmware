/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors */
/* FM-1 Battle: the battle mode (firmware/src/battle.c, battle_ui.c and SLOOP's BATTLE hooks) on the editor's host
 * harness (tests/editor_host.c: the real storage, projects, editor SysEx, UI and audio on a RAM image of the NOR).
 *
 * The rule it holds above all: a whole battle, a reset in the middle included, leaves every byte of the flash outside
 * the battle sector as it was, and the player's song (working loop, sections A..D, song order) comes back exactly.
 * Then every command (good, refused, malformed), the locks, the PRESETS list, the PLAY hold, the clock and its end,
 * the pushes, a fuzz of battle frames with the UI and the audio running.
 *   battle_test [ITER]          (from the repo root, after ./build.sh) */
#define BATTLE_MODE 1
/* the battle sector, straight into the RAM image (the harness's FL_RANGE_OK is SLOOP's, without that sector) */
#define BT_FL_ERASE(off) ({ memset(nor + (off), 0xFF, 0x1000u); bt_erases++; 0; })
#define BT_FL_WRITE(off, p, n) ({ const uint8_t *s_ = (const uint8_t *)(p); uint32_t i_; \
                                 for (i_ = 0; i_ < (uint32_t)(n); i_++) nor[(off) + i_] &= s_[i_]; 0; })
static unsigned bt_erases;
#include "editor_host.c"

static unsigned fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- frames */
static uint8_t fb[700];
static uint32_t fn;
static void f_begin(uint32_t cmd) { fb[0] = 0x7D; fb[1] = 0x46; fb[2] = 0x4C; fb[3] = (uint8_t)cmd; fn = 4; }
static void f_b(uint32_t v) { fb[fn++] = (uint8_t)(v & 127u); }
static void f_v14(int32_t v) { uint32_t u = (uint32_t)(v + 8192); f_b(u); f_b(u >> 7); }
static void f_u35(uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) f_b(v >> (7u * i)); }
static void f_str(const char *s) { while (*s) f_b((uint8_t)*s++); f_b(0); }
static void f_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++) m |= (uint32_t)(p[i] >> 7) << i;
        f_b(m);
        for (i = 0; i < k; i++) f_b(p[i]);
        p += k; n -= k;
    }
}
/* send; the reply's arguments (after F0 7D 46 4C cmd), their count in *n */
static const uint8_t *f_send(uint32_t *n)
{
    ed_inject(fb, fn);
    *n = rx_n >= 6u ? rx_n - 6u : 0u;
    return rx_last + 5;
}
static uint32_t r35(const uint8_t *a) { return a[0] | a[1] << 7 | a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28; }

static const uint8_t BID[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 1, 2, 3, 4, 5, 6, 7, 0x88};
static uint32_t rc_of(uint32_t cmd)            /* a reply's rc (the first byte) */
{
    uint32_t n;
    const uint8_t *r = f_send(&n);
    CHECK(rx_last[4] == cmd, "reply to %u carries cmd %u", cmd, rx_last[4]);
    return n ? r[0] : 0xFFu;
}
static uint32_t arm(uint32_t secs, uint32_t flags, int32_t bpm)
{
    f_begin(101); f_pack7(BID, 16); f_u35(secs); f_b(flags); f_v14(bpm);
    return rc_of(101);
}
static uint32_t setup(int32_t bpm, const uint8_t *ep, uint32_t kit, uint32_t *crc)
{
    uint32_t n, k;
    const uint8_t *r;
    f_begin(103); f_v14(bpm);
    for (k = 0; k < 6u; k++) f_b(ep[k]);
    f_b(kit);
    r = f_send(&n);
    if (crc) *crc = n >= 7u ? r35(r + 2) : 0u;
    return n ? r[0] : 0xFFu;
}
static uint32_t commit(uint32_t crc) { f_begin(104); f_u35(crc); return rc_of(104); }
static uint32_t start(uint32_t off)
{
    static const uint8_t tok[40] = "signed-start-token-from-the-server-01234";
    f_begin(105); f_u35(off); f_pack7(tok, sizeof tok);
    return rc_of(105);
}
static uint32_t ack(uint32_t refused) { f_begin(108); f_b(refused); return rc_of(108); }
static uint32_t abort_(void) { f_begin(110); return rc_of(110); }

/* the frame loop: the harness's UI frame (16 ms of audio, the editor, the UI, the autosave) and the battle's poll */
static void frames(uint32_t n) { while (n--) { ui_frame(); battle_poll(); rx_take(); } }
static void press(uint32_t b)                  /* a button tapped: down for a frame, then up */
{
    uint32_t bit = 1u << panel.btn[b];
    fm1_in.buttons |= bit; edges_btn |= bit;
    frames(1);
    fm1_in.buttons &= ~bit;
    frames(1);
}

/* the flash outside the battle sector, as one hash */
static uint32_t nor_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof nor; i++) {
        if (i >= 0xFB000u && i < 0xFC000u) continue;
        h = (h ^ nor[i]) * 16777619u;
    }
    return h;
}

/* the player's song: some steps, sections A and C, a song order, all in flash */
static project_t before_work, before_slot[4];
static arr_config_t before_arr;
static void player_song(void)
{
    uint32_t i;
    for (i = 0; i < 16u; i += 4u) { trk[0].step[i].n = 1; trk[0].step[i].note[0] = 36 + (uint8_t)i; trk[0].step[i].time = ST_NOTE; trk[0].step[i].vel = 100; }
    section_store(0);
    trk[1].step[3].n = 1; trk[1].step[3].note[0] = 60; trk[1].step[3].time = ST_NOTE; trk[1].step[3].vel = 90;
    section_store(2);
    trk[0].step[1].n = 1; trk[0].step[1].note[0] = 40; trk[0].step[1].time = ST_NOTE; trk[0].step[1].vel = 80;  /* only in the loop */
    song.g[G_BPM] = 97;
    arrangement.count = 2; arrangement.entry[0].scene = 0; arrangement.entry[0].bars = 2;
    arrangement.entry[1].scene = 2; arrangement.entry[1].bars = 4;
    song_dirty = 1;
    persist_flush_now();
    proj_capture(&before_work);
    memcpy(before_slot, proj_slot, sizeof before_slot);
    before_arr = arrangement;
}
static void player_check(const char *when)
{
    project_t w;
    uint32_t s;
    proj_capture(&w);
    CHECK(!memcmp(&w, &before_work, sizeof w), "%s: the working loop is the player's again", when);
    for (s = 0; s < 4u; s++)
        CHECK(!memcmp(&proj_slot[s], &before_slot[s], sizeof proj_slot[s]), "%s: section %c is the player's again", when, 'A' + s);
    CHECK(!memcmp(&arrangement, &before_arr, sizeof arrangement), "%s: the song order is the player's again", when);
    CHECK(song.g[G_BPM] == 97, "%s: the player's tempo (%d)", when, song.g[G_BPM]);
}

/* a reset: RAM kept (warm) or lost (power-off), then SLOOP's boot and the battle's */
static void reboot(int power_off)
{
    uint32_t i;
    if (power_off) {
        memset(proj_slot, 0xA5, sizeof proj_slot);   /* (.noinit after a power-off: garbage) */
        memset(&bt, 0x5A, sizeof bt);
    }
    memset(&btr, 0, sizeof btr);
    for (i = 0; i < NTRK; i++) steps_clear(&trk[i]);
    persist_boot();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    autosave_resume();
    battle_boot();
}

static const uint8_t PACK[6] = {0, 0, 1, 0, 0, 1};   /* engine, preset: ANALOG 808 BOOM, DIGITAL 0, ANALOG 1 */
#define KIT 8u

static void t_info(void)
{
    uint32_t n, i;
    const uint8_t *r;
    f_begin(1);
    r = f_send(&n);
    for (i = 0; i + 7u < n; i++)
        if (!memcmp(r + i, "BATTLE\0", 7)) break;
    CHECK(i + 7u < n && r[i + 7] == BT_PROTO, "INFO ends with the battle tag");
    f_begin(100);
    r = f_send(&n);
    CHECK(n > 40u && r[0] == BT_PROTO && r[1] == BT_IDLE, "BT_INFO: protocol %u, state %u (%u bytes)", r[0], r[1], n);
    CHECK(bt.dev[0] | bt.dev[1] | bt.dev[2], "BT_INFO: a device id");
}

static void t_battle(void)
{
    uint32_t h0, crc, n, k, e0;
    const uint8_t *r;
    player_song();
    h0 = nor_hash();
    /* ARM: refused while playing, then armed */
    song.playing = 1;
    CHECK(arm(600, BTF_TEMPO | BTF_EDIT, 92) == BT_ESTOP, "ARM while playing: stop first");
    song.playing = 0;
    transport_req = 0;
    CHECK(arm(600, BTF_TEMPO | BTF_EDIT, 92) == BT_OK && bt.state == BT_ARMED, "ARM");
    CHECK(arm(600, BTF_TEMPO | BTF_EDIT, 92) == BT_OK, "ARM again: still armed");
    for (k = 0; k < 4u; k++) CHECK(!proj_ok(&proj_slot[k]), "ARM: section %c empty for the battle", 'A' + k);
    CHECK(trk[0].step[0].n == 0, "ARM: the loop empty");
    CHECK(battle_muted(), "ARMED: silent");
    frames(3);
    CHECK(btr.scr == 1u, "ARMED: the waiting room is on screen");
    /* the editor's writes are refused, its reads go through */
    f_begin(3); f_b(0); f_b(P_E0); f_v14(5);
    r = f_send(&n);
    CHECK(n >= 2u && r[0] == 0x7F && r[1] == 0x7F, "SET refused in a battle");
    f_begin(25);
    r = f_send(&n);
    CHECK(rx_last[4] == 25 && !(n >= 2u && r[0] == 0x7F && r[1] == 0x7F), "PING answered in a battle");
    /* LOBBY, SETUP, COMMIT */
    f_begin(102); f_b(5); f_b(3); f_b(15); f_v14(92); f_str("HIP HOP"); f_str("BATTLE #001");
    CHECK(rc_of(102) == BT_OK && !strcmp(btr.title, "BATTLE #001") && btr.lobby_n == 5, "LOBBY");
    {
        static const uint8_t bad[6] = {99, 0, 0, 0, 0, 0};
        CHECK(setup(92, bad, KIT, 0) == BT_EARGS, "SETUP: no such engine");
        CHECK(setup(92, PACK, 60, 0) == BT_EARGS, "SETUP: a user kit is refused");
        CHECK(setup(300, PACK, KIT, 0) == BT_EARGS, "SETUP: tempo out of range");
    }
    CHECK(setup(92, PACK, KIT, &crc) == BT_OK && crc == bt.setup_crc, "SETUP");
    {
        uint8_t raw[9] = {92, 0, 0, 0, 1, 0, 0, 1, KIT};
        CHECK(st_crc32(raw, 9) == crc, "SETUP: the hash is CRC-32 of the canonical 9 bytes");
    }
    CHECK(trk[0].eng_req == 0 && trk[0].preset == 0 && trk[1].eng_req == 1 && trk[2].preset == 1 && TDRUM->p[P_E0] == KIT,
          "SETUP: the pack on the tracks");
    CHECK(song.g[G_BPM] == 92, "SETUP: the tempo");
    CHECK(commit(crc ^ 1u) == BT_EHASH && bt.state == BT_ARMED, "COMMIT: a wrong hash");
    CHECK(commit(crc) == BT_OK && bt.state == BT_READY, "COMMIT");
    {
        uint32_t total, kk;
        CHECK(battle_list() && (preset_pos(&total), total == 3u), "READY: the PRESETS list is the pack (%u)", total);
        CHECK(preset_at(1, &kk) == 1u && kk == 0u, "the list's second sound is the pack's second");
        CHECK(!strcmp(preset_kind(0), "PACK"), "the list says PACK");
    }
    CHECK(battle_muted(), "READY: silent until START");
    /* START */
    f_begin(105); f_u35(0);
    CHECK(rc_of(105) == BT_EARGS, "START without a token");
    CHECK(start(0) == BT_OK && bt.state == BT_RUNNING && !battle_muted(), "START");
    CHECK(start(1000) == BT_OK && bt_elapsed() >= 1000u, "START again: resynced to the server's clock");
    e0 = bt_elapsed();
    frames(63);
    CHECK(bt_elapsed() >= e0 + 1000u, "the clock runs (%u -> %u)", e0, bt_elapsed());
    /* the locks */
    song.g[G_BPM] = 140; frames(1);
    CHECK(song.g[G_BPM] == 92, "the tempo is locked");
    TDRUM->p[P_E0] = 3; frames(1);
    CHECK(TDRUM->p[P_E0] == KIT, "the kit is locked");
    song.sel = 0;
    select_engine(5);
    CHECK(trk[0].eng_req == 0, "another engine is refused");
    set_engine_of(&trk[0], 7); frames(1);
    CHECK(battle_engine_ok(trk[0].eng_req), "an engine switched behind the UI's back is put back");
    song.g[G_TXSRC] = 8; frames(1);
    CHECK(song.g[G_TXSRC] == 0, "the texture cannot play a user sample");
    preset_go(1);
    CHECK(trk[0].eng_req == 1, "a pack sound from another track loads on track 1");
    /* the battle's beat: steps, a section, a song order */
    trk[0].step[2].n = 1; trk[0].step[2].note[0] = 50; trk[0].step[2].time = ST_NOTE; trk[0].step[2].vel = 110;
    section_store(1);
    project_save(3);
    arrangement.count = 1; arrangement.entry[0].scene = 1; arrangement.entry[0].bars = 1;
    song_dirty = 1;
    frames(10);
    persist_flush_now();
    CHECK(nor_hash() == h0, "in the battle: nothing written outside the battle sector");
    /* the battle's autosave, then a warm reset and a power-off */
    ui_input_ms = fm1_ms - 3000u;
    btr.save_ms = fm1_ms - BT_SAVE_GAP;
    frames(2);
    CHECK(btr.proj_sum != 0u, "the battle's autosave ran");
    reboot(0);
    CHECK(bt.state == BT_RUNNING && proj_ok(&proj_slot[1]) && trk[0].step[2].n == 1, "warm reset: the battle goes on, its section and loop kept");
    CHECK(!proj_ok(&proj_slot[0]), "warm reset: the player's section A did not come back into the battle");
    reboot(1);
    CHECK(bt.state == BT_RUNNING && trk[0].step[2].n == 1, "power-off: the battle and its loop come back from the battle sector");
    for (k = 0; k < 4u; k++) CHECK(!proj_ok(&proj_slot[k]), "power-off: no player's section %c in the battle", 'A' + k);
    CHECK(trk[0].step[0].n == 0, "power-off: the player's loop did not come back into the battle");
    CHECK(battle_list() && song.g[G_BPM] == 92, "power-off: the pack and its tempo are still on");
    section_store(1);
    /* PLAY held 2 s: the submit screen; another key: back; PLAY: submitted */
    {
        uint32_t bit = 1u << panel.btn[B_PLAY];
        fm1_in.buttons |= bit; edges_btn |= bit;
        frames(1);
        frames(130);
        CHECK(btr.confirm == 1u, "PLAY held 2 s: the submit screen");
        fm1_in.buttons &= ~bit;
        frames(2);
        CHECK(btr.scr == 2u, "the submit screen is drawn");
        press(B_REC);
        CHECK(!btr.confirm && bt.state == BT_RUNNING, "another key: back to the battle");
        fm1_in.buttons |= bit; edges_btn |= bit;
        frames(131);
        fm1_in.buttons &= ~bit;
        frames(1);
        CHECK(btr.confirm == 1u, "held again");
        press(B_PLAY);
        CHECK(bt.state == BT_SUBMITTED && !(bt.flags & BTF_TIMEUP), "PLAY: submitted");
    }
    rx_n = 0;
    frames(70);
    CHECK(rx_n > 0u, "SUBMITTED: pushes go out");
    /* the site reads the battle's song: section B is the battle's */
    f_begin(34); f_send(&n);
    f_begin(35); f_b(3); f_u35(0); f_b(16); f_b(0);
    r = f_send(&n);
    CHECK(n > 10u && r[1] == 0 && proj_ok(&proj_slot[1]), "BK_GET of section B in the battle");
    CHECK(nor_hash() == h0, "submitted: nothing written outside the battle sector");
    /* ACK: the player's song back, the flash as it was */
    CHECK(ack(0) == BT_OK && bt.state == BT_DONE && !battle_holds_flash(), "ACK");
    player_check("after ACK");
    CHECK(nor_hash() == h0, "after the battle: the flash outside the battle sector as before, byte for byte");
    {
        uint32_t total;
        preset_pos(&total);
        CHECK(total > 3u && !battle_list(), "after: the whole PRESETS list (%u)", total);
    }
    frames(2);
    CHECK(btr.scr == 4u, "the SENT screen");
    press(B_REC);
    CHECK(bt.state == BT_IDLE, "a key: IDLE");
    /* RESULT */
    f_begin(109); f_b(2); f_b(5); f_v14(420); f_str("NOVA");
    CHECK(rc_of(109) == BT_OK && bt.state == BT_DONE && (bt.flags & BTF_RESULT) && !strcmp(bt.res_win, "NOVA"), "RESULT");
    frames(2);
    CHECK(btr.scr == 5u, "the result screen");
    press(B_SAVE);
    CHECK(bt.state == BT_IDLE, "a key: the result screen goes");
    player_check("after the result");
}

static void t_timeup_abort(void)
{
    uint32_t h0 = nor_hash(), crc;
    CHECK(arm(60, 0, 120) == BT_OK && setup(120, PACK, KIT, &crc) == BT_OK && commit(crc) == BT_OK, "a second battle");
    CHECK(start(59000) == BT_OK, "START near the end");
    frames(80);
    CHECK(bt.state == BT_SUBMITTED && (bt.flags & BTF_TIMEUP) && bt.elapsed_ms == 60000u, "time up: submitted by itself");
    CHECK(ack(1) == BT_OK && (bt.flags & BTF_REFUSED), "ACK refused");
    player_check("after a refused one");
    press(B_REC);
    CHECK(arm(600, 0, 100) == BT_OK && setup(100, PACK, KIT, &crc) == BT_OK && commit(crc) == BT_OK && start(0) == BT_OK,
          "a third battle");
    f_begin(107);
    CHECK(rc_of(107) == BT_OK && bt.state == BT_SUBMITTED, "SUBMIT from the site");
    f_begin(107);
    CHECK(rc_of(107) == BT_OK, "SUBMIT again: still submitted");
    CHECK(abort_() == BT_OK && bt.state == BT_IDLE, "ABORT once submitted");
    player_check("after ABORT");
    /* REC + PLAY held on the waiting screen: the player leaves */
    CHECK(arm(600, 0, 100) == BT_OK, "armed again");
    fm1_in.buttons |= 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC];
    frames(200);
    fm1_in.buttons = 0;
    CHECK(bt.state == BT_IDLE, "REC + PLAY held 3 s: out of the battle");
    player_check("after leaving");
    CHECK(nor_hash() == h0, "three battles: the flash outside the battle sector untouched");
    /* the wrong states */
    CHECK(commit(crc) == BT_ESTATE && start(0) == BT_ESTATE && ack(0) == BT_ESTATE, "COMMIT, START, ACK need their state");
}

static uint32_t sd = 7;
static uint32_t rn(uint32_t n) { sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; return n ? sd % n : 0; }
static void t_fuzz(uint32_t iter)
{
    uint32_t i, k, n;
    char why[160];
    for (i = 0; i < iter; i++) {
        uint32_t len = rn(5) == 0 ? rn(300) : rn(40);
        f_begin(100 + rn(13));
        for (k = 0; k < len && fn < sizeof fb; k++) fb[fn++] = (uint8_t)rn(128);
        (void)f_send(&n);
        if (rn(4) == 0) frames(1 + rn(3));
        if (rn(50) == 0) { uint32_t crc; arm(60 + rn(600), rn(8), 40 + (int32_t)rn(200)); setup(90, PACK, KIT, &crc); commit(crc); start(rn(1000)); }
        if (!params_check(why, sizeof why)) { CHECK(0, "fuzz %u: %s", i, why); break; }
    }
    abort_();
    CHECK(bt.state == BT_IDLE, "fuzz: ABORT leaves no battle");
}

int main(int argc, char **argv)
{
    uint32_t iter = argc > 1 ? (uint32_t)atoi(argv[1]) : 3000u;
    harness_init();
    t_info();
    t_battle();
    t_timeup_abort();
    t_fuzz(iter);
    printf("battle: %u checks, %u failed (battle sector erases %u)\n", checks, fails, bt_erases);
    return fails ? 1 : 0;
}
