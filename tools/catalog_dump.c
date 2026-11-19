/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 FM-1 Battle contributors */
/* FM-1 Battle: the catalogue of SLOOP's factory sounds and kits, as JSON on stdout, from the firmware's own tables
 * (ui.c BANK: the PRESETS list by kind; the engines' preset tables; drums.c DRUM_KIT_NAMES / _STYLES), built on the
 * editor's host harness. Every preset name is checked against what the editor's NAMES command answers (the emulator),
 * every kit against DRUM_KIT_DESC: any difference fails (exit 1). The monorepo's tools/gen_catalog.py runs it.
 *   cc ... -o catalog_dump tools/catalog_dump.c -lm && ./catalog_dump > catalog/sloop-<version>.json */
#include "../tests/editor_host.c"

static void js(const char *s)                  /* a JSON string */
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}
static void slug(char *d, const char *s, size_t max)   /* "808 BOOM" -> "808-boom" */
{
    size_t n = 0;
    int dash = 0;
    for (; *s && n + 1 < max; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (dash && n) d[n++] = '-';
            dash = 0;
            d[n++] = c;
        } else if (c == '+') {
            if (n) d[n++] = '-';
            if (n + 5 < max) { memcpy(d + n, "plus", 4); n += 4; }
            dash = 1;
        } else {
            dash = 1;
        }
    }
    d[n] = 0;
}
static const char *const KIND[] = {"Bass", "Keys", "Organ", "Pad", "Lead", "Pluck & bell", "Stab", "FX"};

/* the names NAMES answers for engine e, as the editor sees them */
static uint32_t names_of(uint32_t e, char out[][32], uint32_t max)
{
    uint8_t f[5] = {0x7D, 0x46, 0x4C, 10, (uint8_t)e};
    uint32_t i = 7, n = 0, k, cnt;
    ed_inject(f, 5);
    if (rx_n < 8u || rx_last[4] != 10u) return 0;
    cnt = rx_last[6];
    for (k = 0; k < cnt && k < max; k++) {
        uint32_t j = 0;
        while (i < rx_n && rx_last[i] && rx_last[i] != 0xF7u) { if (j < 31u) out[k][j++] = (char)rx_last[i]; i++; }
        out[k][j] = 0;
        i++;
        n++;
    }
    return n;
}

int main(void)
{
    static char got[NENGINES][128][32];
    uint32_t got_n[NENGINES], i, e, bad = 0, first = 1;
    char key[64], s[48];
    harness_init();
    bank_resolve();
    for (e = 0; e < NENGINES; e++)
        got_n[e] = names_of(e, got[e], 128);
    printf("{\n  \"format\": \"fm1-battle-catalog\",\n  \"version\": 1,\n  \"sloop\": ");
    js(FELUCCA_VERSION);
    printf(",\n  \"engines\": [");
    for (e = 0; e < NENGINES; e++) {
        printf("%s\n    {\"index\": %u, \"name\": ", e ? "," : "", e);
        js(ENGINES[e]->name);
        printf(", \"presets\": %u}", ENGINES[e]->npresets);
    }
    printf("\n  ],\n  \"presets\": [");
    for (i = 0; i < NBANK; i++) {
        const engine_t *en = ENGINES[BANK[i].e % NENGINES];
        uint32_t p = bank_pi[i];
        if (p == 0xFFu) {
            fprintf(stderr, "catalog: BANK %u (%s) is in no preset table of %s\n", i, BANK[i].name, en->name);
            bad++;
            continue;
        }
        if (p >= got_n[BANK[i].e] || strcmp(got[BANK[i].e][p], BANK[i].name)) {
            fprintf(stderr, "catalog: %s preset %u: the table says %s, NAMES says %s\n", en->name, p, BANK[i].name,
                    p < got_n[BANK[i].e] ? got[BANK[i].e][p] : "(nothing)");
            bad++;
        }
        slug(s, en->name, sizeof s);
        snprintf(key, sizeof key, "%s/", s);
        slug(s, BANK[i].name, sizeof s);
        strncat(key, s, sizeof key - strlen(key) - 1);
        printf("%s\n    {\"key\": ", first ? "" : ",");
        first = 0;
        js(key);
        printf(", \"engine\": %u, \"engine_name\": ", BANK[i].e);
        js(en->name);
        printf(", \"preset\": %u, \"list\": %u, \"name\": ", p, i);
        js(BANK[i].name);
        printf(", \"type\": ");
        js(KIND[BANK[i].kind & 7u]);
        printf("}");
    }
    printf("\n  ],\n  \"kits\": [");
    for (i = 0; i < DRUM_USR; i++) {
        const char *nm = DRUM_KIT_NAMES[i];
        if (!DRUM_KIT_DESC.names || strcmp(DRUM_KIT_DESC.names[i], nm)) {
            fprintf(stderr, "catalog: kit %u: %s, the parameter says %s\n", i, nm, DRUM_KIT_DESC.names ? DRUM_KIT_DESC.names[i] : "?");
            bad++;
        }
        slug(s, nm, sizeof s);
        snprintf(key, sizeof key, "kit/%s", s);
        printf("%s\n    {\"key\": ", i ? "," : "");
        js(key);
        printf(", \"index\": %u, \"name\": ", i);
        js(nm);
        printf(", \"family\": \"%s\", \"style\": ", i < DRUM_SAMPLED ? "sampled" : "synthesized");
        js(DRUM_KIT_STYLES[i]);
        printf("}");
    }
    printf("\n  ]\n}\n");
    if (bad)
        fprintf(stderr, "catalog: %u differences\n", bad);
    return bad ? 1 : 0;
}
