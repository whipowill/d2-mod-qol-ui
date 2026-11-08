// Buff / aura / debuff icons along the bottom of the screen (ported from BH ScreenInfo::OnDraw, plus the Druid states BH has no icon for).
// Buffs and auras are drawn from the left (blue tint), debuffs from the right (red tint); hovering an icon shows its name. The 45 states BH knows use the
// 24x24 pictures from `qol_ui.mpq` (next to the game: BH's 45 icons plus 7 Druid icons, built by tools/make_qol_ui_mpq.py); BH's own `buffs.mpq` also works
// (45 pictures, the Druid extras then become boxes). A state without a picture, and every state if neither file is found, is drawn as a small box with a
// two-letter label. Included by hud.h. Switched by qol_ui.ini [display] buffs.
#ifndef UI_BUFFS_H
#define UI_BUFFS_H

typedef struct { int state; const wchar_t *name, *tag; int frame; int debuff; } bf_state;
// frame = the picture number in buffs24.dc6 (BH's order, then ours); a frame the file does not have is drawn as a box with `tag`
static const bf_state bf_states[] = {
    {157, L"Burst of Speed", L"BS", 0, 0}, {159, L"Fade", L"Fd", 1, 0}, {156, L"Cloak of Shadows", L"CS", 2, 0}, {31, L"Venom", L"Vn", 3, 0},
    {26, L"Shout", L"Sh", 4, 0}, {32, L"Battle Orders", L"BO", 5, 0}, {51, L"Battle Command", L"BC", 6, 0}, {149, L"Oak Sage", L"OS", 7, 0},
    {151, L"Cyclone Armor", L"CA", 8, 0}, {144, L"Hurricane", L"Hu", 9, 0}, {14, L"Bone Armor", L"BA", 10, 0}, {101, L"Holy Shield", L"HS", 11, 0},
    {10, L"Frozen Armor", L"FA", 12, 0}, {88, L"Shiver Armor", L"SA", 13, 0}, {20, L"Chilling Armor", L"CA", 14, 0}, {16, L"Enchant", L"En", 15, 0},
    {30, L"Energy Shield", L"ES", 16, 0}, {38, L"Thunder Storm", L"TS", 17, 0}, {137, L"Experience Shrine", L"XP", 18, 0},
    {33, L"Might", L"Mi", 19, 0}, {34, L"Prayer", L"Pr", 20, 0}, {3, L"Resist Fire", L"RF", 21, 0}, {35, L"Holy Fire", L"HF", 22, 0},
    {36, L"Thorns", L"Th", 23, 0}, {37, L"Defiance", L"Df", 24, 0}, {4, L"Resist Cold", L"RC", 25, 0}, {40, L"Blessed Aim", L"BA", 26, 0},
    {41, L"Vigor", L"Vi", 27, 0}, {5, L"Resist Lightning", L"RL", 28, 0}, {42, L"Concentration", L"Co", 29, 0}, {43, L"Holy Freeze", L"HF", 30, 0},
    {45, L"Cleansing", L"Cl", 31, 0}, {46, L"Holy Shock", L"HS", 32, 0}, {47, L"Sanctuary", L"Sa", 33, 0}, {48, L"Meditation", L"Me", 34, 0},
    {49, L"Fanaticism", L"Fa", 35, 0}, {50, L"Redemption", L"Re", 36, 0}, {28, L"Conviction", L"Cv", 37, 0}, {8, L"Salvation", L"Sv", 38, 0},
    {9, L"Amplify Damage", L"AD", 39, 1}, {19, L"Weaken", L"We", 40, 1}, {60, L"Decrepify", L"De", 41, 1}, {61, L"Lower Resist", L"LR", 42, 1},
    {2, L"Poisoned", L"Po", 43, 1}, {11, L"Frozen", L"Fz", 44, 1},
    // Druid states BH has no icon for (pictures 45..51 exist only in qol_ui.mpq)
    {147, L"Spirit of Barbs", L"SB", 45, 0}, {148, L"Heart of Wolverine", L"HW", 46, 0}, {145, L"Armageddon", L"Ar", 47, 0},
    {139, L"Werewolf", L"Wf", 48, 0}, {140, L"Werebear", L"Wb", 49, 0}, {120, L"Feral Rage", L"FR", 50, 0}, {117, L"Maul", L"Ma", 51, 0},
};
#define BF_N (int)(sizeof bf_states / sizeof *bf_states)

typedef int (__stdcall *bf_state_t)(DWORD unit, DWORD state);                        // D2Common #10706
typedef BYTE *(__fastcall *bf_load_t)(const char *path, int type);                   // D2Win #10023, returns a CellFile
typedef void (__stdcall *bf_draw_t)(void *ctx, int x, int y, int dwl, int mode, int color);   // D2gfx #10067
typedef BOOL (__stdcall *bf_open_t)(const char *name, DWORD priority, DWORD flags, HANDLE *out);   // Storm #266 SFileOpenArchive
static bf_state_t p_bstate; static bf_load_t p_bload; static bf_draw_t p_bdraw; static bf_open_t p_bopen;
static int g_buffs = 1, g_bbottom = 5, g_bside = 0, g_bgap = 5, g_btint = 3, g_dtint = 1;       // pixel gap between icons; colour shift of buff / debuff pictures
static BYTE *g_bcf;                                              // the loaded buffs24 cell file, NULL = use boxes
static int g_binit, g_bw = 24, g_bh = 24, g_bcells;
static int g_bactive[BF_N], g_bn; static DWORD g_bscan;

static void bf_init(void) {                                      // once, on the game thread: add the picture archive to the game's archives and load the pictures
    g_binit = 1;
    if (!p_bload || !p_bdraw || !p_bopen) return;
    static const char *names[] = {"qol_ui.mpq", "buffs.mpq"};
    for (int n = 0; n < 2 && !g_bcf; n++) {
        char path[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, path); lstrcatA(path, "\\"); lstrcatA(path, names[n]);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        HANDLE h = NULL;
        if (!p_bopen(path, 5000, 0, &h)) { logf_("buffs: %s could not be opened", names[n]); continue; }
        BYTE *cf = p_bload("data\\global\\ui\\spells\\buffs24", 0);
        if (!cf || !readable(cf, 0x1C) || *(DWORD *)(cf + 0x14) < 45) { logf_("buffs: buffs24 not found in %s", names[n]); continue; }
        DWORD c0 = *(DWORD *)(cf + 0x18);                        // cells[0]
        if (c0 && readable((void *)c0, 12)) { g_bw = (int)*(DWORD *)(c0 + 4); g_bh = (int)*(DWORD *)(c0 + 8); }
        if (g_bw < 4 || g_bw > 64 || g_bh < 4 || g_bh > 64) { g_bw = g_bh = 24; }
        g_bcells = (int)*(DWORD *)(cf + 0x14); g_bcf = cf;
        logf_("buffs: %d pictures loaded from %s (%dx%d)", g_bcells, names[n], g_bw, g_bh);
    }
    if (!g_bcf) logf_("buffs: no picture archive (qol_ui.mpq / buffs.mpq) next to the game; icons drawn as boxes");
}

static void bf_box(const bf_state *s, int x, int y) {            // a picture-less icon: translucent box, frame, two letters
    if (p_srect) p_srect(x, y - g_bh, x + g_bw, y, 0, 4);
    RECT rc = {x, y - g_bh, x + g_bw, y}; if (p_sframe) p_sframe(&rc);
    DWORD old = p_hsize(6), wd = 0, fn = 0;
    p_hwidth(s->tag, &wd, &fn);
    p_hdraw(s->tag, x + (g_bw - (int)wd) / 2, y - g_bh / 2 + 3, s->debuff ? 1 : 3, 0);
    p_hsize(old);
}

static void buffs_draw(int sw, int sh) {
    if (!g_buffs || !p_bstate || !p_player) return;
    if (!g_binit) bf_init();
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x30)) return;
    DWORD now = GetTickCount();
    if (now - g_bscan > 250) {                                   // states change rarely: look 4 times a second
        g_bscan = now; g_bn = 0;
        for (int i = 0; i < BF_N; i++) if (p_bstate(pl, (DWORD)bf_states[i].state)) g_bactive[g_bn++] = i;
    }
    if (!g_bn) return;
    int mx = (int)*(DWORD *)(g_hud_cl + 0x11C950), my = (int)*(DWORD *)(g_hud_cl + 0x11C94C);
    int bx = 117 + g_bside, by = sh - 49 - g_bbottom, dx = sw - 117 - g_bside - g_bw, dy = sh - 49 - g_bbottom, nb = 0;
    for (int k = 0; k < g_bn; k++) {
        const bf_state *s = &bf_states[g_bactive[k]];
        if (nb % 9 == 0 && nb != 0 && !s->debuff) { bx = 117 + g_bside; by -= g_bh + g_bgap; }
        int x = s->debuff ? dx : bx, y = s->debuff ? dy : by;
        if (g_bcf && s->frame >= 0 && s->frame < g_bcells) {
            static BYTE ctx[0x100];                              // the game may use more of a CellContext than BH's 0x38-byte struct shows: keep it off the stack and roomy
            memset(ctx, 0, sizeof ctx); *(DWORD *)ctx = (DWORD)s->frame; *(BYTE **)(ctx + 0x34) = g_bcf;
            p_bdraw(ctx, x, y, -1, 5, s->debuff ? g_dtint : g_btint);
        }
        else bf_box(s, x, y);
        if (mx > x && mx < x + g_bw && my > y - g_bh && my < y) {   // hover: the name above the icon
            DWORD old = p_hsize(0), wd = 0, fn = 0; p_hwidth(s->name, &wd, &fn);
            int tx = x + g_bw / 2 - (int)wd / 2, ty = y - g_bh - 2;
            if (p_srect) p_srect(tx - 3, ty - 12, tx + (int)wd + 3, ty + 3, 0, 4);
            p_hdraw(s->name, tx, ty, 0, 0); p_hsize(old);
        }
        if (s->debuff) dx -= g_bw + g_bgap; else { bx += g_bw + g_bgap; nb++; }
    }
}
#endif
