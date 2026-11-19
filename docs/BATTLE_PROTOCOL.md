# FM-1 Battle: the battle protocol (SysEx)

The battle mode extends SLOOP's editor protocol ([web/EDITOR_PROTOCOL.md](../web/EDITOR_PROTOCOL.md)): the same
frames `F0 7D 46 4C cmd args… F7`, the same 7-bit encodings, one reply per request (the same `cmd`), plus two pushes.
Commands **100–111**, free in SLOOP 2.8.5 (it uses 1–42 and 68–77). Code: `firmware/src/battle.c` (commands, state,
locks), `battle_ui.c` (screens), `battle.h` (what SLOOP's files see). Tests: `tests/battle_test.c`.

Encodings, as the editor's: **v14** = value + 8192 in two 7-bit bytes, LSB first; **u35** = five 7-bit bytes, LSB
first; **pack7** = groups of up to 7 bytes, each after one byte of their top bits (16 bytes → 19 on the wire);
**str** = 7-bit ASCII ended by 0.

`INFO` (1) ends, after the protocol version 12, with `"BATTLE"` (str) and the battle protocol version (1): the way a
page tells FM-1 Battle from SLOOP.

## States

| # | State | The FM-1 |
|---|---|---|
| 0 | IDLE | SLOOP as usual |
| 1 | ARMED | waiting room: silent, the panel is the screen's; the player's song is in flash, untouched, the RAM wiped |
| 2 | READY | the pack is on the tracks (SETUP + COMMIT), still silent: waiting for START |
| 3 | RUNNING | the clock runs; PRESETS lists the pack only; the locks hold |
| 4 | SUBMITTED | the beat is frozen; pushes until the site has it |
| 5 | DONE | the player's song is back; SENT / REFUSED or the result on screen until a key |

Flags: 1 (unused), 2 tempo locked, 4 sounds editable, 8 time up (submitted by itself), 16 refused, 32 a result to
show, 64 the pack is on the tracks.

**The player's song is never written during a battle.** At ARM, what was only in RAM is flushed; from then on the
autosave, the sections and the settings are held (the `project.c` hooks), so the player's flash stays as it was
byte for byte. The battle plays in RAM; the battle sector `0xFB000` keeps the 256-byte record and the battle's
working project (written at each state change and, when stopped and idle, at most every 15 s), so a battle survives a
reset; its sections A–D live in `.noinit` RAM (a reset keeps them, a power-off does not). At the end the player's
song is read back from flash, as at power-on.

On the FM-1: **PLAY held 2 s** while running opens the submit screen (PLAY submits, any other key goes back; the
first press of PLAY still plays or stops, as always). **REC + PLAY held 3 s** on a waiting screen (ARMED, READY,
SUBMITTED) leaves the battle (as ABORT).

## Commands

Every reply starts with `rc` then the state, unless said otherwise. rc: 0 ok, 1 arguments, 2 not in this state,
3 stop the song first, 4 flash, 5 hash mismatch, 6 another battle (or another token).

| cmd | Name | Request | Reply | Notes |
|---|---|---|---|---|
| 100 | BT_INFO | — | proto, state, flags, elapsed ms (u35), left ms (u35), battle id (pack7 16), device id (pack7 8), duration s (u35), bpm (v14), firmware (str), SLOOP version (str) | The device id is made once (random) and kept |
| 101 | BT_ARM | battle id (pack7 16), duration s (u35, 60..3600), flags (2 tempo, 4 editable), bpm (v14) | rc, state | IDLE or DONE only (rc 3 while playing). Flushes the player's RAM work, wipes the RAM, writes the record. Again with the same id: rc 0 |
| 102 | BT_LOBBY | players, ready, countdown s (127: none), bpm (v14), genre (str), title (str) | rc, state | ARMED or READY. The waiting room only |
| 103 | BT_SETUP | bpm (v14), engine + preset × 3 (tracks 1..3), kit | rc, state, hash (u35) | ARMED. The presets must be factory sounds of the PRESETS list, the kit a factory kit (0..36). RAM only (fast) |
| 104 | BT_COMMIT | hash (u35) | rc, state | ARMED after SETUP → READY. The hash is CRC-32 (zlib) of 9 bytes: bpm (u16 LE), e1, p1, e2, p2, e3, p3, kit |
| 105 | BT_START | offset ms (u35), token (pack7, ≤ 96 bytes) | rc, state | READY → RUNNING, the clock at `offset`. Again while running with the same token: the clock resynced (rc 6 for another token). The token is the server's, kept, not checked here |
| 106 | BT_STATE | — | state, flags, elapsed ms (u35), left ms (u35) | Also **pushed** every second while RUNNING or SUBMITTED |
| 107 | BT_SUBMIT | (push) | battle id (pack7 16), elapsed ms (u35), flags | Pushed every second while SUBMITTED; the site then reads the song (`BK_LIST`, `BK_GET` 0, 1, 2..5) |
| 107 | BT_SUBMIT | — (request, the site's Submit button) | rc, state | RUNNING → SUBMITTED, as PLAY held + PLAY; again while SUBMITTED: rc 0 |
| 108 | BT_ACK | result (0 received, 1 refused) | rc, state | SUBMITTED → DONE, the player's song back |
| 109 | BT_RESULT | rank (0: not ranked), players, score × 100 (v14), winner (str) | rc, state | Not during a battle. → DONE with the result on screen |
| 110 | BT_ABORT | — | rc, state | Anywhere → IDLE, the player's song back |
| 111 | BT_LINK | pseudo (str) | rc, device id (pack7 8) | "LINKED TO …" for 4 s |

## The editor during a battle

From ARMED to SUBMITTED the editor's reads go through (INFO, GET, DUMP, DESC, STEP_GET, NAMES, SMP_INFO, UP_LIST,
UP_GET, WATCH, PING, the TRACK* reads, BK_LIST, BK_GET, LOCK_GET, MICRO_GET, FILL_GET, FM6_LIST, DSYN_LIST, DSYN_GET,
LIVE op 0); every other command is answered `cmd, 7F, 7F`: the beat is played on the FM-1.

## The locks (RUNNING)

The PRESETS list is the pack's three sounds on every synth track ("PACK"); another engine (ENGINE, KNOB 2) says
PACK SOUNDS ONLY; no favourites, no Program Change. Put back each frame with a message: the tempo and SYNC INT (tempo
locked), an engine that is not the pack's, a user preset, a SAMPLE / GRAIN / SLICE sound on a user slot, FM6's PTCH,
the kit, a TEXTURE on a user slot. Everything else of the sound, the sequencer, the sections, the song, the effects
and the mix is free.

## A battle, from the site

1. `BT_INFO` (is it FM-1 Battle? a battle left over?), a full backup (`BK_LIST`, `BK_GET`), stop the transport
   (`LIVE` op 1, 0), `BT_ARM`, then `BT_LOBBY` on every lobby change.
2. On "Launch": `BT_SETUP` with the pack, `BT_COMMIT` with its hash, `BT_LOBBY` with the countdown, then
   `BT_START` with the server's token at the start time.
3. `BT_STATE` pushes; on `BT_SUBMIT`: `BK_LIST`, `BK_GET` of 0 (the loop), 1 (the song order is bytes 48..83),
   2..5 (the sections), the submission to the server, then `BT_ACK`.
4. Later, `BT_RESULT`. A reconnection: `BT_INFO` says where it is; `BT_START` again resyncs the clock.
