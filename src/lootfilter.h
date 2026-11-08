// Loot filter: item names follow the ItemDisplay rules in qol_ui_filter.cfg.
// Included into qol_ui.c (uses its logging / patching helpers). Switched by qol_ui.ini [lootfilter] enabled.
#ifndef UI_LOOTFILTER_H
#define UI_LOOTFILTER_H
#include "filter.h"
#include "itemdb.h"

static fi_set *g_fi;                                            // the loaded rules

typedef struct { DWORD unit; unsigned groups; } fi_ctx;
typedef struct { WORD sub, index; DWORD value; } d2stat;
typedef void *(__stdcall *mh_gettext_t)(DWORD);
typedef DWORD (__stdcall *mh_getstat_t)(DWORD unit, DWORD stat, DWORD layer);
typedef void *(__stdcall *mh_getlist_t)(DWORD unit, DWORD unk, DWORD max);
typedef DWORD (__stdcall *mh_copylist_t)(void *list, d2stat *out, DWORD max);
typedef DWORD (__stdcall *mh_getreq_t)(DWORD item, DWORD player);
typedef wchar_t *(__fastcall *mh_locale_t)(WORD no);
typedef DWORD (__stdcall *mh_player_t)(void);
typedef BYTE (__stdcall *mh_diff_t)(void);
static mh_gettext_t p_gettext; static mh_getstat_t p_getstat; static mh_getlist_t p_getlist; static mh_copylist_t p_copylist;
static mh_getreq_t p_getreq; static mh_locale_t p_locale; static mh_player_t p_player; static mh_diff_t p_diff;

static int mh_istat(void *ctx, int stat, int layer) {
    fi_ctx *c = (fi_ctx *)ctx;
    if (stat < 100000) return (int)p_getstat(c->unit, (DWORD)stat, (DWORD)layer);
    void *list = p_getlist(c->unit, 0, 0x40);
    if (!list) return 0;
    d2stat arr[256]; DWORD n = p_copylist(list, arr, 256); int sum = 0;
    int want = stat == FI_STAT_ED ? ((c->groups & FI_G_ALLARMOR) ? 16 : 17) : stat == FI_STAT_MAXDUR ? 75 : 0;
    for (DWORD i = 0; i < n; i++) {
        if (stat == FI_STAT_FOOLS) { if (arr[i].index == 218 && arr[i].sub == 0) sum += 1; if (arr[i].index == 224 && arr[i].sub == 0) sum += 2; }
        else if (stat == FI_STAT_CHARGE) { if (arr[i].index == 204 && (arr[i].sub >> 6) == layer) { int lv = arr[i].sub & 0x3F; if (lv > sum) sum = lv; } }
        else if (arr[i].index == want && arr[i].sub == 0) sum += (int)arr[i].value;
    }
    return sum;
}
static int mh_pstat(void *ctx, int stat, int layer) {
    (void)ctx; DWORD pl = p_player();
    return pl ? (int)p_getstat(pl, (DWORD)stat, (DWORD)layer) : 0;
}

// Fills an fi_item from a game item unit. `base` receives the item's base name (it->basename points at it). Returns 0 if the unit is unreadable.
static int mh_fill_item(fi_item *it, fi_ctx *cx, DWORD item, char *base, size_t basecap) {
    if (!readable((void *)item, 0x30) || *(DWORD *)item != 4) return 0;
    DWORD data = *(DWORD *)(item + 0x14);
    if (!data || !readable((void *)data, 0x30)) return 0;
    BYTE *txt = (BYTE *)p_gettext(*(DWORD *)(item + 4));
    if (!txt || !readable(txt, 0x100)) return 0;
    cx->unit = item; cx->groups = 0;
    memset(it, 0, sizeof *it);
    memcpy(it->code, txt + 0x80, 3);
    const itemdb_t *db = itemdb_find(it->code);
    if (db) { it->qlvl = db->qlvl; it->magiclvl = db->magiclvl; it->groups = db->groups; it->rune_no = db->rune_no; it->gem_level = db->gem_level; it->gem_type = db->gem_type; it->wpnspd = db->speed; it->rangeadder = db->rangeadder; }
    cx->groups = it->groups;
    it->quality = (int)*(DWORD *)data; it->flags = *(DWORD *)(data + 0x18); it->fileindex = (int)*(DWORD *)(data + 0x28); it->ilvl = (int)*(DWORD *)(data + 0x2C);
    DWORD pl = p_player();
    it->lvlreq = db ? db->levelreq : 0; if (pl && p_getreq) it->lvlreq = (int)(BYTE)p_getreq(item, pl);
    it->price = -1; it->clvl = pl ? (int)p_getstat(pl, 12, 0) : 0; it->difficulty = p_diff ? p_diff() : 0; it->expansion = 1;
    it->filter_level = g_filter_level; it->stat = mh_istat; it->pstat = mh_pstat; it->ctx = cx;
    base[0] = 0; WORD lno = *(WORD *)(txt + 0xF4);
    if (p_locale) { wchar_t *w = p_locale(lno); if (w && readable(w, 2)) WideCharToMultiByte(CP_ACP, 0, w, -1, base, (int)basecap - 1, NULL, NULL); }
    it->basename = base;
    return 1;
}

// Item labels are asked for every frame (and again for each tooltip), but an item's text only changes when the item does, so the result is kept per item
// for a short time: the rules are then evaluated about once per item every 2 s instead of once per item per frame.
typedef struct { DWORD uid, flags, mode, hash, gen, tick; int kind; wchar_t out[256]; } mh_nc;   // kind 0: keep the game's text, 1: use `out`, 2: blank
static mh_nc g_nc[128];
static volatile LONG g_name_gen;                                   // bumped when the rules are reloaded
#define MH_NAME_TTL 2000u

// Called by the stub at the game's "item name built" point: `name` is the game's wide-character name text, `item` the item unit.
static void __cdecl mh_item_name(wchar_t *name, DWORD item) {
    if (!g_fi || !name || !item || !readable((void *)item, 0x30)) return;
    DWORD data = *(DWORD *)(item + 0x14);
    if (!data || !readable((void *)data, 0x30)) return;
    DWORD hash = 2166136261u;
    for (int i = 0; i < 300 && name[i]; i++) hash = (hash ^ (DWORD)name[i]) * 16777619u;
    DWORD uid = *(DWORD *)(item + 0x0C), flags = *(DWORD *)(data + 0x18), mode = *(DWORD *)(item + 0x10), now = GetTickCount();
    mh_nc *c = &g_nc[(uid ^ (uid >> 7)) & 127];
    if (c->uid == uid && c->flags == flags && c->mode == mode && c->hash == hash && c->gen == (DWORD)g_name_gen && now - c->tick < MH_NAME_TTL) {
        if (c->kind == 2) name[0] = 0; else if (c->kind == 1) wcscpy(name, c->out);
        return;
    }
    fi_ctx cx; fi_item it; char base[128];
    if (!mh_fill_item(&it, &cx, item, base, sizeof base)) return;
    char in[512], out[640];
    int l = WideCharToMultiByte(CP_ACP, 0, name, -1, in, sizeof in - 1, NULL, NULL); if (l <= 0) return; in[sizeof in - 1] = 0;
    int blocked = 0;
    fi_name(g_fi, &it, in, out, sizeof out, &blocked);
    c->uid = uid; c->flags = flags; c->mode = mode; c->hash = hash; c->gen = (DWORD)g_name_gen; c->tick = now; c->kind = 0;
    if (blocked && g_hide_blocked && (mode == 3 || mode == 5)) { c->kind = 2; name[0] = 0; return; }   // blocked item lying on the ground (3) or dropping (5): blank its label   // blocked item lying on the ground (3) or dropping (5): blank its label
    if (!strcmp(in, out)) return;
    wchar_t wide[300]; int wl = MultiByteToWideChar(CP_ACP, 0, out, -1, wide, 255); if (wl <= 0) return; wide[255] = 0;
    wcscpy(c->out, wide); c->kind = 1;
    wcscpy(name, wide);
}

// ---- gold labels ------------------------------------------------------------------------------------------------------------
// A gold pile's ground label is not built by GetItemName: the ground-label builder (D2Client +0x19980) formats the amount itself ("%i", then " ", then
// "Gold") and draws it with D2Win's DrawText. We take over that one draw call (D2Client +0x19AB5; it only ever draws gold): before the text goes out
// `mh_gold_skip` decides whether to draw it at all. Skipping leaves the pile in the world with no label.
static int mh_gold_stat(void *ctx, int stat, int layer) { (void)layer; return stat == 14 ? *(int *)ctx : 0; }   // item stat 14 = the size of the pile
static int mh_gold_skip_inner(const wchar_t *buf);
static int __cdecl mh_gold_skip(const wchar_t *buf) {
    int r = mh_gold_skip_inner(buf);
    static int seen;
    if (g_logging && seen < 12 && buf && readable(buf, 8)) {         // diagnostics (logging=1): is the hook reached, with what text, and what was decided
        char tmp[160]; tmp[0] = 0; WideCharToMultiByte(CP_ACP, 0, buf, -1, tmp, sizeof tmp - 1, NULL, NULL); tmp[sizeof tmp - 1] = 0;
        seen++; logf_("gold label draw: text '%s', show_gold_amount=%d -> %s", tmp, g_show_gold, r ? "skipped" : "drawn");
    }
    return r;
}
static int mh_gold_skip_inner(const wchar_t *buf) {
    if (!g_fi || !buf || !readable(buf, 8)) return 0;
    if (!g_show_gold) return 1;                                      // show_gold_amount=0: no gold labels at all
    if (!g_hide_blocked) return 0;
    int amount = 0, got = 0;                                         // read the amount back out of the text ("<colour code>900 Gold")
    for (int i = 0; i < 64 && buf[i]; i++) {
        if (buf[i] == 0xFF) { if (buf[i + 1]) i += 2; continue; }    // a colour code: 0xFF 'c' digit
        if (buf[i] >= L'0' && buf[i] <= L'9') { amount = amount * 10 + (buf[i] - L'0'); got = 1; }
        else if (got) break;
    }
    if (!got) return 0;
    fi_item it; memset(&it, 0, sizeof it);
    memcpy(it.code, "gld", 3); it.quality = 2; it.price = -1; it.expansion = 1; it.filter_level = g_filter_level;
    DWORD pl = p_player ? p_player() : 0; it.clvl = pl && p_getstat ? (int)p_getstat(pl, 12, 0) : 0; it.difficulty = p_diff ? p_diff() : 0;
    it.stat = mh_gold_stat; it.pstat = mh_pstat; it.ctx = &amount; it.basename = "Gold";
    const itemdb_t *db = itemdb_find("gld"); if (db) it.groups = db->groups;
    return fi_is_blocked(g_fi, &it);                                 // an ignore rule such as ItemDisplay[GOLD<500]: matches and nothing whitelists it
}

// Same decision for the copy path: the caller wants the text, not a drawing. If the label is to be skipped the text is emptied before it is copied out.
static void __cdecl mh_gold_blank(wchar_t *buf) {
    int r = mh_gold_skip_inner(buf);
    static int seen;
    if (g_logging && seen < 12 && buf && readable(buf, 8)) {
        char tmp[160]; tmp[0] = 0; WideCharToMultiByte(CP_ACP, 0, buf, -1, tmp, sizeof tmp - 1, NULL, NULL); tmp[sizeof tmp - 1] = 0;
        seen++; logf_("gold label copy: text '%s', show_gold_amount=%d -> %s", tmp, g_show_gold, r ? "emptied" : "kept");
    }
    if (r) buf[0] = 0;
}

// Redirects the gold draw call at `site` (an `E8 rel32` to the DrawText thunk) to a stub that asks mh_gold_skip first.
static void mh_install_gold(BYTE *code, DWORD size, BYTE *base) {
    long at = sig_find_golddraw(code, size);
    if (at < 0) { logf_("lootfilter: gold label code not found in D2Client (or appears more than once); gold labels unchanged"); return; }
    BYTE *site = code + at; DWORD orig = (DWORD)(site + 5) + *(DWORD *)(site + 1);
    if (orig < (DWORD)base || orig >= (DWORD)base + 0xCE000) { logf_("lootfilter: gold label hook already installed"); return; }
    BYTE *st = stub_alloc(); if (!st) return;
    static const BYTE a[] = {0x60, 0x51};                            // pushad ; push ecx (the text)
    static const BYTE b[] = {0x83, 0xC4, 0x04, 0x85, 0xC0, 0x61, 0x75, 0x05};   // add esp,4 ; test eax,eax ; popad (keeps the flags) ; jnz skip
    static const BYTE c[] = {0xC2, 0x0C, 0x00};                      // skip: ret 0Ch (what the DrawText call would have removed from the stack)
    BYTE *p = emit(st, a, 2);
    p = emit_rel(p, 0xE8, (DWORD)mh_gold_skip);
    p = emit(p, b, sizeof b);
    p = emit_rel(p, 0xE9, orig);                                     // not skipped: on to the original DrawText
    p = emit(p, c, sizeof c);
    BYTE j[5]; j[0] = 0xE8; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5);
    patch_bytes(site, j, 5);
    logf_("lootfilter: gold labels (draw) follow show_gold_amount and the GOLD rules (D2Client +%lx)", (unsigned long)(site - base));
    int patched = 0; long cp = sig_find_goldcopy(code, size, &patched);       // the copy path
    if (cp < 0) { logf_("lootfilter: gold label copy code not found; only the draw path is handled"); return; }
    if (patched) return;
    BYTE *s2 = code + cp, *st2 = stub_alloc(); if (!st2) return;
    static const BYTE a2[] = {0x60, 0x8D, 0x44, 0x24, 0x58, 0x50};            // pushad ; lea eax,[esp+58h] (the finished text) ; push eax
    static const BYTE b2[] = {0x83, 0xC4, 0x04, 0x61, 0x8D, 0x54, 0x24, 0x38, 0x8B, 0xCE, 0xFF, 0xD5, 0xC3};   // add esp,4 ; popad ; lea edx,[esp+38h] ; mov ecx,esi ; call ebp ; ret
    BYTE *q = emit(st2, a2, sizeof a2);
    q = emit_rel(q, 0xE8, (DWORD)mh_gold_blank);
    q = emit(q, b2, sizeof b2);
    BYTE j2[8]; j2[0] = 0xE8; *(DWORD *)(j2 + 1) = (DWORD)st2 - (DWORD)(s2 + 5); j2[5] = j2[6] = j2[7] = 0x90;
    patch_bytes(s2, j2, 8);
    logf_("lootfilter: gold labels (copy) follow show_gold_amount and the GOLD rules (D2Client +%lx)", (unsigned long)(s2 - base));
}

// ---- drop alerts (ported from BH: Maphack::OnDraw "Item Detailed Notifications") --------------------------------------------
// Each game-loop tick, ground items near the player that have not been checked yet are run through the rules' map actions. A rule whose
// %TIER-n% is above ping_level is skipped; %NOTIFY-DEAD% silences the item. Otherwise the item's full name is printed in chat in its quality
// colour. Unit flag 0x04000000 marks "checked" (BH reuses it the same way); item flag 0x2000 is "new", i.e. it just dropped.
#define MH_UF_CHECKED 0x04000000u
#define MH_IF_NEW     0x2000u
#define MH_NOTIFY_DEAD 0xdead
typedef int (__stdcall *mh_itemname_t)(DWORD item, wchar_t *buf, DWORD size);
typedef void (__stdcall *mh_print_t)(const wchar_t *msg, int color);
static mh_itemname_t p_itemname; static mh_print_t p_print;
static int g_alerts = 1, g_alert_close = 1, g_alert_debug = 0;

static int g_dbg_items;
// Map marks of ground items (BH: items flagged UNITFLAG_REVEALED are drawn on the automap by their rules' %MAP% / %DOT% / border / px / line colours).
// The colours are worked out once, when the item is first seen, and kept here by item id; the automap draw only looks them up.
#define MH_UF_REVEALED 0x20000000u
typedef struct { int ping, border, map, dot, px, line, stop; } mh_dact;
typedef struct { DWORD uid, unit; int n; mh_dact a[3]; } mh_dot;   // up to 3 matching rules per item, each with its own %TIER-n%, so a tier change takes effect at once
static mh_dot g_dots[256]; static int g_dots_next;
static int g_dots_on = 1;
static BYTE *g_loop_cl;                                              // D2Client base, set when the game-loop hook is installed

// The client's own handler for the server packet 0x0A "remove unit" (D2Client +0x83730, fastcall, ECX = the 6-byte packet {0x0A, unit type, unit id}).
// Calling it with a made-up packet takes an item out of the client's world exactly as if the server had removed it.
static void mh_remove_item(DWORD item) {
    BYTE pk[8] = {0x0A, 4, 0, 0, 0, 0, 0, 0};
    *(DWORD *)(pk + 2) = *(DWORD *)(item + 0x0C);
    ((void (__fastcall *)(BYTE *))(g_loop_cl + 0x83730))(pk);
}

// Returns 1 when the item was blocked and has been removed.
static int mh_alert_item(DWORD item, int alert) {
    fi_ctx cx; fi_item it; char base[128];
    if (!mh_fill_item(&it, &cx, item, base, sizeof base)) return 0;
    if (g_hide_blocked && g_loop_cl) {                               // BH keeps blocked ground items out of the game: an ignore rule matches, no map rule, no whitelist
        DWORD mode = *(DWORD *)(item + 0x10);
        if ((mode == 3 || mode == 5) && fi_is_blocked(g_fi, &it) && memcmp(it.code, "gld", 3)) { mh_remove_item(item); return 1; }   // gold stays in the world (you still walk over it and pick it up): only its label is blanked
    }
    fi_action acts[16]; int n = fi_actions(g_fi, &it, acts, 16), hit = 0;
    mh_dot d; memset(&d, 0, sizeof d); d.uid = *(DWORD *)(item + 0x0C); d.unit = item; int has_map = 0;
    for (int i = 0; i < n; i++) {
        if (g_dots_on && d.n < 3 && (acts[i].border != FI_UNDEF || acts[i].map != FI_UNDEF || acts[i].dot != FI_UNDEF || acts[i].px != FI_UNDEF || acts[i].line != FI_UNDEF)) {
            mh_dact *x = &d.a[d.n++]; x->ping = acts[i].ping; x->border = acts[i].border; x->map = acts[i].map; x->dot = acts[i].dot; x->px = acts[i].px; x->line = acts[i].line; x->stop = acts[i].stop;
            has_map = 1;
        }
        if (acts[i].ping > g_ping_level) continue;                    // BH: the rule's tier must not exceed the Ping Level setting
        if (acts[i].notify == MH_NOTIFY_DEAD) continue;
        hit = 1;
    }
    if (has_map) {
        g_dots[g_dots_next] = d; g_dots_next = (g_dots_next + 1) & 255;
        *(DWORD *)(item + 0xC4) |= MH_UF_REVEALED;
    }
    if (!alert) return 0;
    if (g_alert_debug && g_dbg_items < 300) logf_("alerts:   code %s quality %d: %d map rule(s) matched, alert %s", it.code, it.quality, n, hit ? "yes" : "no");
    if (!hit) return 0;
    static const int qcol[] = {0, 0, 0, 0, 3, 2, 9, 4, 8};          // none/inferior/normal/superior white, magic blue, set green, rare yellow, unique gold, crafted orange
    wchar_t w[256]; w[0] = 0;
    p_itemname(item, w, 255);                                        // returns no useful value
    if (!w[0]) { logf_("alerts:   GetItemName gave nothing"); return 0; }
    w[255] = 0;
    wchar_t o[300]; size_t k = 0;
    for (size_t j = 0; w[j] && k < 290; j++) { if (w[j] == L'\n') { o[k++] = L' '; o[k++] = L'-'; o[k++] = L' '; } else o[k++] = w[j]; }
    o[k] = 0;
    p_print(o, it.quality >= 0 && it.quality <= 8 ? qcol[it.quality] : 0);
    return 0;
}

static void mh_scan_ground(void) {
    readable_reset();
    DWORD pl = p_player();
    if (!pl || !readable_c((void *)pl, 0xF0)) return;
    DWORD act = *(DWORD *)(pl + 0x1C);
    if (!act || !readable_c((void *)act, 0x14)) return;
    int guard = 0;
    for (DWORD room = *(DWORD *)(act + 0x10); room && readable_c((void *)room, 0x80) && guard < 256; room = *(DWORD *)(room + 0x7C), guard++) {
        int ug = 0;
        DWORD u = *(DWORD *)(room + 0x74);
        while (u && readable_c((void *)u, 0xF0) && ug < 4096) {
            DWORD nxt = *(DWORD *)(u + 0xE8); ug++;                  // read first: handling an item may remove it from the list
            DWORD cur = u; u = nxt;
            if (*(DWORD *)cur != 4 || (*(DWORD *)(cur + 0xC4) & MH_UF_CHECKED)) continue;
            DWORD data = *(DWORD *)(cur + 0x14);
            if (!data || !readable_c((void *)data, 0x30)) continue;
            DWORD flags = *(DWORD *)(data + 0x18);
            *(DWORD *)(cur + 0xC4) |= MH_UF_CHECKED;
            int go = (flags & MH_IF_NEW) || g_alert_close;
            if (g_alert_debug && g_dbg_items < 300) { g_dbg_items++; logf_("alerts: item unit %08lx txt %lu mode %lu flags %08lx -> %s", (unsigned long)cur, (unsigned long)*(DWORD *)(cur + 4), (unsigned long)*(DWORD *)(cur + 0x10), (unsigned long)flags, go ? "checking rules" : "skipped (not new)"); }
            if (go || g_dots_on || g_hide_blocked) mh_alert_item(cur, go);
        }
    }
}

// Runs on the game thread once per game-loop tick (called from the stub installed by lootfilter_thread).
static void __cdecl am_loop(void);
static void id_tick(void);
static void __cdecl mh_game_loop(void) {
    static DWORD last;
    static int first = 1;
    if (first) { first = 0; logf_("alerts: game loop hook is running"); }
    am_loop();
    id_tick();
    if (g_msg_ready == 1 && p_print) { p_print(g_msg, 0); InterlockedExchange(&g_msg_ready, 0); }
    if (!g_fi || !(g_alerts || g_dots_on || g_hide_blocked) || !p_print || !p_itemname) return;
    DWORD now = GetTickCount();
    if (now - last < 100) return;                                    // a few scans a second are plenty
    last = now;
    mh_scan_ground();
}

static int mh_read_file(const char *file, char **buf, size_t *len) {
    char path[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, path); lstrcatA(path, "\\"); lstrcatA(path, file);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *buf = (char *)malloc((size_t)n + 1); if (!*buf) { fclose(f); return 0; }
    *len = fread(*buf, 1, (size_t)n, f); (*buf)[*len] = 0; fclose(f); return 1;
}
static int mh_pt(const char *t, int n) { return itemdb_parse_types(t, n); }
static int mh_pa(const char *t, int n) { return itemdb_parse_items(t, n, 0); }
static int mh_pw(const char *t, int n) { return itemdb_parse_items(t, n, 1); }
static int mh_pm(const char *t, int n) { return itemdb_parse_items(t, n, 2); }

// Hook the game-loop entry so mh_game_loop runs once per tick on the game thread.
static void mh_install_loop(BYTE *code, DWORD size, BYTE *base) {
    g_loop_cl = base;
    int patched = 0; long at = sig_find_gameloop(code, size, &patched);
    if (at < 0) { logf_("lootfilter: game-loop code not found in D2Client (or appears more than once); drop alerts off"); return; }
    if (patched) { logf_("lootfilter: game-loop hook already installed"); return; }
    BYTE *site = code + at, *st = stub_alloc(); if (!st) return;
    static const BYTE a[] = {0x60};                                  // pushad
    static const BYTE b[] = {0x61, 0x58, 0x83, 0xEC, 0x20, 0x89, 0x4C, 0x24, 0x0C, 0x50, 0xC3};   // popad ; pop eax ; sub esp,20 ; mov [esp+0C],ecx ; push eax ; ret
    BYTE *p = emit(st, a, 1);
    p = emit_rel(p, 0xE8, (DWORD)mh_game_loop);
    p = emit(p, b, sizeof b);
    BYTE jmp[7]; jmp[0] = 0xE8; *(DWORD *)(jmp + 1) = (DWORD)st - (DWORD)(site + 5); jmp[5] = 0x90; jmp[6] = 0x90;
    patch_bytes(site, jmp, 7);
    logf_("lootfilter: drop alerts on (game loop D2Client +%lx, ping_level %d)", (unsigned long)(site - base), g_ping_level);
}

static DWORD WINAPI lootfilter_thread(LPVOID arg) {
    (void)arg;
    if (!g_filter_on) return 0;
    HMODULE cl = NULL, cm = NULL, lang = NULL;
    for (int i = 0; i < 6000 && !(cl && cm && lang); i++) { cl = GetModuleHandleA("D2Client.dll"); cm = GetModuleHandleA("D2Common.dll"); lang = GetModuleHandleA("D2Lang.dll"); if (!(cl && cm && lang)) Sleep(100); }
    if (!cl || !cm || !lang) { logf_("lootfilter: game modules not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("lootfilter: this D2Client build is not supported (1.13d only); off"); return 0; }
    p_gettext = (mh_gettext_t)GetProcAddress(cm, (LPCSTR)10994); p_getstat = (mh_getstat_t)GetProcAddress(cm, (LPCSTR)10550);
    p_getlist = (mh_getlist_t)GetProcAddress(cm, (LPCSTR)10449); p_copylist = (mh_copylist_t)GetProcAddress(cm, (LPCSTR)10195);
    p_getreq = (mh_getreq_t)GetProcAddress(cm, (LPCSTR)10929); p_locale = (mh_locale_t)GetProcAddress(lang, (LPCSTR)10004);
    p_player = (mh_player_t)((BYTE *)cl + 0x613C0); p_diff = (mh_diff_t)((BYTE *)cl + 0x42980);
    p_itemname = (mh_itemname_t)((BYTE *)cl + 0x958C0); p_print = (mh_print_t)((BYTE *)cl + 0x75EB0);
    if (!p_gettext || !p_getstat || !p_getlist || !p_copylist) { logf_("lootfilter: D2Common functions not found; off"); return 0; }
    char *cfg; size_t cn;
    if (!mh_read_file("qol_ui_filter.cfg", &cfg, &cn)) { logf_("lootfilter: qol_ui_filter.cfg not found next to the game; off"); return 0; }
    int unknown = 0; char bad[96];
    fi_set *s = fi_parse(cfg, cn, &unknown, bad, sizeof bad); free(cfg);
    logf_("lootfilter: qol_ui_filter.cfg: %d rules, %d unrecognised token(s)%s%s", fi_rule_count(s), unknown, unknown ? ", first: " : "", unknown ? bad : "");
    int got = 0;                                                   // the game's data tables (read through Storm like the other features)
    for (int t = 0; t < 300 && got < 4; t++) {
        got = 0;
        itemdb_reset();
        if (storm_read("Data\\global\\excel\\ItemTypes.txt", mh_pt) > 0) got++;
        if (storm_read("Data\\global\\excel\\Armor.txt", mh_pa) > 0) got++;
        if (storm_read("Data\\global\\excel\\Weapons.txt", mh_pw) > 0) got++;
        if (storm_read("Data\\global\\excel\\Misc.txt", mh_pm) > 0) got++;
        if (got < 4) Sleep(200);
    }
    logf_("lootfilter: item tables: %d items known (%d of 4 tables)", itemdb_count(), got);
    if (got < 4) { fi_free(s); return 0; }
    fi_set_video(s, 0);
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; int patched = 0;
        long at = sig_find_itemname(code, sec->Misc.VirtualSize, &patched);
        if (at < 0) continue;
        if (patched) { logf_("lootfilter: item name hook already installed"); return 0; }
        BYTE *site = code + at, *back = site + 5, *st = stub_alloc(); if (!st) return 0;
        BYTE *p = st;
        static const BYTE a[] = {0x60, 0x53, 0x57};                  // pushad ; push ebx (item) ; push edi (name buffer)  -> cdecl mh_item_name(name, item)
        p = emit(p, a, 3);
        p = emit_rel(p, 0xE8, (DWORD)mh_item_name);
        static const BYTE b[] = {0x83, 0xC4, 0x08, 0x61, 0x8A, 0x85, 0x2A, 0x01, 0x00, 0x00, 0xC3};   // add esp,8 ; popad ; mov al,[ebp+12a] ; ret
        p = emit(p, b, sizeof b);
        g_fi = s;                                                    // rules ready before the first call can happen
        BYTE jmp[6]; jmp[0] = 0xE8; *(DWORD *)(jmp + 1) = (DWORD)st - (DWORD)back; jmp[5] = 0x90;
        patch_bytes(site, jmp, 6);
        logf_("lootfilter: item names now follow qol_ui_filter.cfg (D2Client +%lx)", (unsigned long)(site - base));
        mh_install_gold(code, sec->Misc.VirtualSize, base);
        if (g_alerts || g_dots_on || g_hide_blocked || g_exit_lines || g_shift_id || g_item_move_cfg) mh_install_loop(code, sec->Misc.VirtualSize, base);
        return 0;
    }
    logf_("lootfilter: the item-name code was not found in D2Client (or appears more than once); off");
    fi_free(s);
    return 0;
}
#endif
