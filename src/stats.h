// Character stats panel (ported from BH's Drawing/Stats/StatsDisplay): resistances, absorb, damage reduction, masteries, pierce, AR/defense, rates,
// leech, elemental damage, find. Toggled with [keys] stats_key (default B); Esc closes. Shows the mercenary while the mercenary inventory is open,
// or after pressing stats_merc_key (flips between you and your mercenary). View only. Drawn from the hud hook (hud.h).
#ifndef UI_STATS_H
#define UI_STATS_H

typedef DWORD (__stdcall *st_stat_t)(DWORD unit, DWORD stat, DWORD layer);   // D2Common #10550
typedef DWORD (__stdcall *st_merc_t)(void);                                  // D2Client +0x9C0A0
static st_stat_t p_sstat; static st_merc_t p_smerc; static void (*p_sframe)(RECT *);
static am_rect_t p_srect;
static volatile LONG g_stats_open, g_stats_merc;                             // set by the key thread
static int g_stats_right = 1;
#define ST_FONT 6

static void st_text(const wchar_t *w, int x, int y, int color) {
    DWORD old = p_hsize(ST_FONT);
    p_hdraw(w, x, y + 7, (DWORD)color, 0);                                    // font 6 is 7 high, BH adds that to y
    p_hsize(old);
}
static void st_right(const wchar_t *w, int xr, int y, int color) {
    DWORD old = p_hsize(ST_FONT), wd = 0, fn = 0;
    p_hwidth(w, &wd, &fn);
    p_hdraw(w, xr - (int)wd, y + 7, (DWORD)color, 0);
    p_hsize(old);
}
#define SS(u, id) ((int)p_sstat((u), (id), 0))

static void stats_draw(int screen_w) {
    if (!p_sstat || !p_hdraw || !p_srect || !p_sframe) return;
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x30)) return;
    DWORD unit = pl; int is_merc = 0;
    int mercui = *(DWORD *)(g_hud_cl + 0x11C890 + 4 * 0x24) != 0;
    if ((mercui || g_stats_merc) && p_smerc) {
        DWORD m = p_smerc();
        if (m && readable((void *)m, 0x30)) { unit = m; is_merc = 1; }
    }
    const int W = 240, H = 462;
    int x = g_stats_right ? screen_w - 10 - W : 10, y = 10;
    if (x < 0) x = 0;
    int c1 = x + 5, c2 = c1 + W / 2, r = x + W;
    p_srect(x, y, x + W, y + H, 0, 4);                                        // translucent black
    RECT rc = {x, y, r, y + H}; p_sframe(&rc);
    wchar_t t[160];
    char *nm = (!is_merc && readable((void *)*(DWORD *)(unit + 0x14), 16)) ? (char *)*(DWORD *)(unit + 0x14) : NULL;
    if (nm) swprintf(t, 160, L"Name:\377c0 %.15hs", nm); else swprintf(t, 160, L"Name:\377c0 \377c;Mercenary");
    st_text(t, c1, y += 8, 4);
    swprintf(t, 160, L"Level:\377c0 %d", SS(unit, 12)); st_right(t, r - 5, y, 4);
    swprintf(t, 160, L"Additional XP:\377c: %d%%", SS(unit, 85)); st_right(t, r - 5, y + 12, 4);
    y += 8;
    int diff = p_diff ? (int)p_diff() : 0; if (diff < 0 || diff > 2) diff = 0;
    static const int pen[3] = {0, -20, -50};
    int penalty = pen[diff] * 2;                                              // expansion character
    swprintf(t, 160, L"\377c4Fire Resist:\377c1 %d \377c0/ %d", SS(unit, 39) + penalty, SS(unit, 40) + 75); st_text(t, c1, y += 16, 1);
    swprintf(t, 160, L"\377c4Cold Resist:\377c3 %d \377c0/ %d", SS(unit, 43) + penalty, SS(unit, 44) + 75); st_text(t, c1, y += 16, 3);
    swprintf(t, 160, L"\377c4Lightning Resist:\377c9 %d \377c0/ %d", SS(unit, 41) + penalty, SS(unit, 42) + 75); st_text(t, c1, y += 16, 9);
    swprintf(t, 160, L"Poison Resist:\377c2 %d \377c0/ %d  \377c4Length:\377c: %d%%", SS(unit, 45) + penalty, SS(unit, 46) + 75, 100 - penalty - SS(unit, 110)); st_text(t, c1, y += 16, 4);
    y += 8;
    swprintf(t, 160, L"\377c4Absorption: \377c1%d\377c0/\377c1%d%%\377c3 %d\377c0/\377c3%d%%\377c9 %d\377c0/\377c9%d%%\377c8 %d\377c0/\377c8%d%%",
             SS(unit, 143), SS(unit, 142), SS(unit, 149), SS(unit, 148), SS(unit, 145), SS(unit, 144), SS(unit, 147), SS(unit, 146));
    st_text(t, c1, y += 16, 1);
    swprintf(t, 160, L"\377c4Damage Reduction: \377c7%d\377c0/\377c7%d%%\377c8 %d\377c0/\377c8%d%%", SS(unit, 34), SS(unit, 36), SS(unit, 35), SS(unit, 37));
    st_text(t, c1, y += 16, 7);
    y += 8;
    swprintf(t, 160, L"Elemental Mastery:\377c1 %d%%\377c3 %d%%\377c9 %d%%\377c2 %d%%\377c8 %d%%", SS(unit, 329), SS(unit, 331), SS(unit, 330), SS(unit, 332), SS(unit, 357));
    st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Elemental Pierce:\377c1 %d%%\377c3 %d%%\377c9 %d%%\377c2 %d%%\377c8 %d%%", SS(unit, 333), SS(unit, 335), SS(unit, 334), SS(unit, 336), SS(unit, 358));
    st_text(t, c1, y += 16, 4);
    static const int tohit[7] = {5, 15, 15, 20, 20, 5, 15};                   // charstats ToHitFactor per class
    int cls = is_merc ? -1 : (int)*(DWORD *)(unit + 4);
    int dex = SS(unit, 2), dexAR = dex * 5 + ((cls >= 0 && cls < 7) ? tohit[cls] - 35 : 0), gearAR = SS(unit, 19);
    swprintf(t, 160, L"Base AR:\377c5 dex:\377c0 %d\377c5 equip:\377c0 %d\377c5 total:\377c0 %d", dexAR, gearAR, dexAR + gearAR); st_text(t, c1, y += 16, 4);
    int dexDef = dex / 4, gearDef = SS(unit, 31);
    swprintf(t, 160, L"Base Def:\377c5 dex:\377c0 %d\377c5 equip:\377c0 %d\377c5 total:\377c0 %d", dexDef, gearDef, dexDef + gearDef); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Base Damage:\377c5 1h:\377c0 %d-%d\377c5 2h:\377c0 %d-%d", SS(unit, 21), SS(unit, 22), SS(unit, 23), SS(unit, 24)); st_text(t, c1, y += 16, 4);
    y += 8;
    swprintf(t, 160, L"Cast Rate:\377c0 %d", SS(unit, 105)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Block Rate:\377c0 %d", SS(unit, 102)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Hit Recovery:\377c0 %d", SS(unit, 99)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Run/Walk:\377c0 %d", SS(unit, 96)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Attack Rate:\377c0 %d", SS(unit, 68)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"IAS:\377c0 %d", SS(unit, 93)); st_text(t, c2, y, 4);
    y += 8;
    swprintf(t, 160, L"Crushing Blow:\377c0 %d", SS(unit, 136)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Open Wounds: \377c0%d", SS(unit, 135)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Deadly Strike:\377c0 %d", SS(unit, 141)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Critical Strike: \377c0%d", SS(unit, 337)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Life Leech:\377c1 %d", SS(unit, 60)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Mana Leech:\377c3 %d", SS(unit, 62)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Projectile Pierce:\377c0 %d", SS(unit, 156) + SS(unit, 166)); st_text(t, c1, y += 16, 4);
    y += 8;
    int plen = SS(unit, 59), pov = SS(unit, 101); if (pov > 0) plen = pov;
    swprintf(t, 160, L"Added Damage:\377c0 %d", SS(unit, 111)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"%d-%d", SS(unit, 52), SS(unit, 53)); st_text(t, c2, y, 8);
    swprintf(t, 160, L"%d-%d", SS(unit, 48), SS(unit, 49)); st_text(t, c1, y += 16, 1);
    swprintf(t, 160, L"%d-%d", SS(unit, 54), SS(unit, 55)); st_text(t, c2, y, 3);
    swprintf(t, 160, L"%d-%d", SS(unit, 50), SS(unit, 51)); st_text(t, c1, y += 16, 9);
    swprintf(t, 160, L"%d-%d over %.1fs", (int)(SS(unit, 57) / 256.0 * plen), (int)(SS(unit, 58) / 256.0 * plen), plen / 25.0); st_text(t, c2, y, 2);
    y += 8;
    swprintf(t, 160, L"Magic Find:\377c3 %d", SS(unit, 80)); st_text(t, c1, y += 16, 4);
    swprintf(t, 160, L"Gold Find:\377c9 %d", SS(unit, 79)); st_text(t, c2, y, 4);
    swprintf(t, 160, L"Stash Gold:\377c9 %d", SS(unit, 15)); st_text(t, c1, y += 16, 4);
}
#endif
