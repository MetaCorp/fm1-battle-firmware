# FM-1 Battle: what is changed in SLOOP's own files

The battle mode lives in `firmware/src/battle.h`, `battle.c`, `battle_ui.c` and `tests/battle_test.c`. Every line
added to one of SLOOP's files says `BATTLE`. Without `BATTLE_MODE` (every SLOOP test) `battle.h` turns the hooks
into stubs that do nothing. To take a new SLOOP release: merge `upstream/main`, keep these hooks, run
`tests/run_tests.sh` and `tests/battle_test.c`.

Base: SLOOP 2.8.5 (`v2.8.5`, bac9c05).

| File | Hook | Why |
|---|---|---|
| `firmware/src/felucca.c` | `#define BATTLE_MODE 1` | the battle mode in the firmware |
| `firmware/src/felucca.c` | `FL_RANGE_OK` defined before `fm1_flash.h`, with `0xFB000..0xFBFFF` | the battle sector (left free by SLOOP) |
| `firmware/src/editor.c` | `#include "battle.h"`; `battle_editor()` first in `ed_handle`; `battle_info_tag()` in INFO; `battle.c`, `battle_ui.c` included after `editor_live.c` | commands 100–111, the editor's writes refused in a battle, the INFO tag |
| `firmware/src/ui.c` | `#include "battle.h"`; `preset_pos`, `preset_at`, `preset_kind` | the PRESETS list = the pack |
| `firmware/src/ui.c` | `fav_toggle`, `pc_poll`, `select_engine` | no favourites, no Program Change, the pack's engines only |
| `firmware/src/ui_input.c` | `battle_input()` at the top of `ui_input`; `battle_play_down()` in `B_PLAY` | the battle screens take the panel; PLAY held 2 s |
| `firmware/src/ui_draw.c` | `battle_draw()` at the top of `ui_draw`; `battle_head_sig()`, `battle_head()` in `draw_head` | the screens; the clock in the top bar |
| `firmware/src/project.c` | `#include "battle.h"`; `proj_fetch`, `project_save`, `project_erase`, `autosave_tick`, `sections_write`, `persist_flush_now`, `settings_save` | the player's song is neither written nor read during a battle |
| `firmware/src/project.c` | `battle_load_record()` in `persist_boot` | the record known before the slots are filled |
| `firmware/src/main.c` | `battle_boot()` after `felucca_init`; `battle_poll()` after `ed_service`; `song.master_q12 = 0` while `battle_muted()` | the battle after a reset; the clock and pushes; the silent waiting room |
| `tests/editor_host.c` | `#include "fm1_flash_id.h"` | **a fix of SLOOP 2.8.5**: `persist_boot` uses `FL_JEDEC_OK`, the harness did not include it and `editor_fuzz` did not link (`run_tests.sh` ended in HOST TESTS FAILED) |
| `tests/run_tests.sh` | the battle test | |
