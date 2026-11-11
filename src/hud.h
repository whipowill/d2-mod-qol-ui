// HUD text: a clock in the top-right corner while the automap is closed, and (map open) the area name, difficulty and area level there instead.
// Included into qol_ui.c. Hooks the end of the in-game draw function (BH's GameDraw patch, D2Client +0x1D7B4). Stock 1.13d only.
#ifndef UI_HUD_H
#define UI_HUD_H
#include "sig.h"

typedef void (__fastcall *hud_draw_t)(const wchar_t *w, int x, int y, DWORD color, DWORD unk);   // D2Win #10076
typedef DWORD (__fastcall *hud_size_t)(DWORD font);                                              // D2Win #10047, returns the old font
typedef DWORD (__fastcall *hud_width_t)(const wchar_t *w, DWORD *width, DWORD *fileno);          // D2Win #10179
typedef wchar_t *(__cdecl *hud_lname_t)(DWORD level);                                              // thunk to D2Client +0x18250 (level in ESI)
typedef BYTE *(__stdcall *hud_ltxt_t)(DWORD level);                                              // D2Common #10142
static hud_draw_t p_hdraw; static hud_size_t p_hsize; static hud_width_t p_hwidth; static hud_lname_t p_hlname; static hud_ltxt_t p_hltxt;
static BYTE *g_hud_cl;
#include "stats.h"
#include "buffs.h"
#include "autoparty.h"
static int g_clock = 1, g_clock_24h = 0, g_clock_color = 4, g_clock_font = 1, g_clock_fh = 0, g_clock_right = 0, g_clock_top = 6, g_info = 1, g_info_color = 4;

// Texts are built (and measured) only when they change -- the clock once a minute, the map info when the level or difficulty changes -- and then
// just drawn each frame.
typedef struct { wchar_t text[64]; int width; int font; int h; } hud_line;   // font: the game font index (0 = the default one, 10 high); h: its height, 0 = the default by font
static void hud_set(hud_line *l, const wchar_t *w) {
    wcsncpy(l->text, w, 63); l->text[63] = 0;
    DWORD old = p_hsize((DWORD)l->font), wd = 0, fn = 0;
    p_hwidth(l->text, &wd, &fn); p_hsize(old);
    l->width = (int)wd;
}
static void hud_put(const hud_line *l, int xr, int y, int color) {
    if (!l->text[0]) return;
    DWORD old = p_hsize((DWORD)l->font);
    p_hdraw(l->text, xr - l->width, y + (l->h > 0 ? l->h : l->font == 6 ? 7 : 10), (DWORD)color, 0);        // BH adds the font height (10 for font 0, 7 for font 6) to y
    p_hsize(old);
}

static void hud_base(int sw);
static void __cdecl hud_frame(void) {
    if (!p_hdraw || !g_hud_cl) return;
    int sw = (int)*(DWORD *)(g_hud_cl + 0xF7034);
    if (sw <= 0 || sw > 8192) return;
    hud_base(sw);
    ap_frame();
    int sh = (int)*(DWORD *)(g_hud_cl + 0xF7038);
    if (sh > 0 && sh <= 8192) buffs_draw(sw, sh);
    if (g_stats_open) stats_draw(sw);
}
static void hud_base(int sw) {
    int map_open = *(DWORD *)(g_hud_cl + 0x11C8B8) != 0;
    if (!map_open) {
        if (!g_clock) return;
        static hud_line clk; static int key = -1;
        SYSTEMTIME st; GetLocalTime(&st);
        int k = ((st.wHour * 60 + st.wMinute) * 2 + (g_clock_24h ? 1 : 0)) * 16 + g_clock_font;   // rebuilt when the minute, the 12/24 h setting or the font changes
        if (k != key) {
            key = k; wchar_t t[32];
            if (g_clock_24h) swprintf(t, 32, L"%02d:%02d", st.wHour, st.wMinute);
            else swprintf(t, 32, L"%d:%02d", st.wHour % 12 ? st.wHour % 12 : 12, st.wMinute);
            clk.font = g_clock_font; hud_set(&clk, t);
        }
        clk.h = g_clock_fh > 0 ? g_clock_fh : g_clock_font == 0 ? 10 : g_clock_font == 6 ? 7 : 16;   // font 1 (the item labels) is about 16 high; clock_font_height overrides
        hud_put(&clk, sw - g_clock_right, g_clock_top, g_clock_color);
        return;
    }
    if (!g_info) return;
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x30)) return;
    DWORD path = *(DWORD *)(pl + 0x2C); if (!path || !readable((void *)path, 0x20)) return;
    DWORD r1 = *(DWORD *)(path + 0x1C); if (!r1 || !readable((void *)r1, 0x20)) return;
    DWORD r2 = *(DWORD *)(r1 + 0x10); if (!r2 || !readable((void *)r2, 0x5C)) return;
    DWORD lv = *(DWORD *)(r2 + 0x58); if (!lv || !readable((void *)lv, 0x1D4)) return;
    DWORD no = *(DWORD *)(lv + 0x1D0); if (!no) return;
    static hud_line ln[3]; static DWORD ckey_no; static int ckey_diff = -2;
    int diff = p_diff ? (int)p_diff() : 0;
    if (no != ckey_no || diff != ckey_diff) {                         // level or difficulty changed: rebuild the three lines
        static int once; if (!once) { once = 1; logf_("hud: first map-info draw"); }
        ckey_no = no; ckey_diff = diff;
        memset(ln, 0, sizeof ln);
        wchar_t *name = p_hlname ? p_hlname(no) : NULL;
        if (once == 1) { once = 2; logf_("hud: level %lu name %s", (unsigned long)no, name ? "found" : "missing"); }
        if (name && readable(name, 2) && name[0]) hud_set(&ln[0], name);
        static const wchar_t *dn[] = {L"Normal", L"Nightmare", L"Hell"};
        if (diff >= 0 && diff <= 2) hud_set(&ln[1], dn[diff]);
        BYTE *rec = p_hltxt ? p_hltxt(no) : NULL;                     // the Levels.txt row; the area level is MonLvlEx (expansion) for the difficulty
        if (rec && readable(rec, 0x1C) && *(WORD *)rec == no && diff >= 0 && diff <= 2) {
            int al = (int)*(WORD *)(rec + 0x16 + 2 * diff);
            if (al > 0) { wchar_t t[16]; swprintf(t, 16, L"%d", al); hud_set(&ln[2], t); }   // a town has area level 0: leave the line blank
        }
        if (once == 2) { once = 3; logf_("hud: first map-info draw done"); }
    }
    int y = 6;
    for (int i = 0; i < 3; i++) if (ln[i].text[0]) { hud_put(&ln[i], sw - 10, y, g_info_color); y += 16; }
}

static DWORD WINAPI hud_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL, win = NULL, cm = NULL;
    for (int i = 0; i < 6000 && !(cl && win && cm); i++) { cl = GetModuleHandleA("D2Client.dll"); win = GetModuleHandleA("D2Win.dll"); cm = GetModuleHandleA("D2Common.dll"); if (!(cl && win && cm)) Sleep(100); }
    if (!cl || !win || !cm) { logf_("hud: game modules not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("hud: this D2Client build is not supported (1.13d only); off"); return 0; }
    p_hdraw = (hud_draw_t)GetProcAddress(win, (LPCSTR)10076); p_hsize = (hud_size_t)GetProcAddress(win, (LPCSTR)10047);
    p_hwidth = (hud_width_t)GetProcAddress(win, (LPCSTR)10179); p_hltxt = (hud_ltxt_t)GetProcAddress(cm, (LPCSTR)10142);
    if (!p_hdraw || !p_hsize || !p_hwidth) { logf_("hud: D2Win text functions not found; off"); p_hdraw = NULL; return 0; }
    g_hud_cl = (BYTE *)cl;
    p_bstate = (bf_state_t)GetProcAddress(cm, (LPCSTR)10706); p_bload = (bf_load_t)GetProcAddress(win, (LPCSTR)10023); p_bdraw = NULL; p_bopen = NULL;
    { HMODULE gx = GetModuleHandleA("D2gfx.dll"), st = GetModuleHandleA("Storm.dll");
      if (gx) p_bdraw = (bf_draw_t)GetProcAddress(gx, (LPCSTR)10067);
      if (st) p_bopen = (bf_open_t)GetProcAddress(st, (LPCSTR)266); }
    p_sstat = (st_stat_t)GetProcAddress(cm, (LPCSTR)10550); p_smerc = (st_merc_t)(g_hud_cl + 0x9C0A0);
    { HMODULE gfx = GetModuleHandleA("D2gfx.dll"); if (gfx) p_srect = (am_rect_t)GetProcAddress(gfx, (LPCSTR)10028); }
    { BYTE *th = stub_alloc();                                      // D2Client +0x17D10 draws a frame around the RECT passed in EAX: pushad ; mov eax,[esp+24h] ; call fn ; popad ; ret
      if (th) { static const BYTE a[] = {0x60, 0x8B, 0x44, 0x24, 0x24}; static const BYTE b[] = {0x61, 0xC3};
                BYTE *q = emit(th, a, 5); q = emit_rel(q, 0xE8, (DWORD)(g_hud_cl + 0x17D10)); emit(q, b, 2); p_sframe = (void (*)(RECT *))th; } }
    { BYTE *th = stub_alloc();                                      // the game's GetLevelName takes the level in ESI and may clobber other registers:
      if (th) {                                                     // pushad ; mov esi,[esp+24h] ; call fn ; mov [esp+1Ch],eax ; popad ; ret
          static const BYTE a[] = {0x60, 0x8B, 0x74, 0x24, 0x24}; static const BYTE b[] = {0x89, 0x44, 0x24, 0x1C, 0x61, 0xC3};
          BYTE *q = emit(th, a, 5); q = emit_rel(q, 0xE8, (DWORD)(g_hud_cl + 0x18250)); emit(q, b, 6); p_hlname = (hud_lname_t)th; } }
    ap_init((BYTE *)cl);
    p_player = (mh_player_t)(g_hud_cl + 0x613C0); p_diff = (mh_diff_t)(g_hud_cl + 0x42980);
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; int patched = 0;
        long at = sig_find_gamedraw(code, sec->Misc.VirtualSize, &patched);
        if (at < 0) continue;
        if (patched) { logf_("hud: hook already installed"); return 0; }
        BYTE *site = code + at, *st = stub_alloc(); if (!st) return 0;
        static const BYTE a[] = {0x60};                                // pushad
        static const BYTE b[] = {0x61, 0x5E, 0x5B, 0x59, 0xC2, 0x04, 0x00};   // popad ; pop esi ; pop ebx ; pop ecx ; ret 4
        BYTE *p = emit(st, a, 1);
        p = emit_rel(p, 0xE8, (DWORD)hud_frame);
        p = emit(p, b, sizeof b);
        BYTE j[6]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5); j[5] = 0x90;
        patch_bytes(site, j, 6);
        logf_("hud: clock %s, map info %s (D2Client +%lx)", g_clock ? "on" : "off", g_info ? "on" : "off", (unsigned long)(site - base));
        return 0;
    }
    logf_("hud: the draw code was not found in D2Client (or appears more than once); off");
    return 0;
}
#endif
