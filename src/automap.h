// Automap lines to exits, waypoints and points of interest (ported from BH's AutoTele "Draw Destination", ScreenToAutomap, D2GFX DrawLine).
// Included into qol_ui.c. Switched by qol_ui.ini [automap] exit_lines.
#ifndef UI_AUTOMAP_H
#define UI_AUTOMAP_H
#include "sig.h"

typedef void (__stdcall *am_line_t)(int x1, int y1, int x2, int y2, DWORD color, DWORD unk);
typedef DWORD (__stdcall *am_size_t)(void);
typedef void (__stdcall *am_rect_t)(int x1, int y1, int x2, int y2, DWORD color, DWORD trans);
static am_line_t p_drawline; static am_size_t p_mapsize; static am_rect_t p_rect;
static int *p_divisor; static LONG *p_offset;                    // D2Client +0xF34F8 (int), +0x11CF5C (POINT)
#include "palette.h"
static int g_lbl_npc = -1, g_lbl_team = -1, g_lbl_other = -1;   // text colours (0..12) of the automap name labels, -1 = the game's own
static int g_me_color = 32, g_merc_color = 29, g_npc_color = 13, g_team_color = 125, g_other_color = 10, g_teammin_color = 120, g_portal_cross = 158;
static BYTE *g_am_cl;

// Port of BH's AutoTele "Draw Destination": for every area a table (atvectors.h) names up to four things to point at -- next exit, "other" (a preset
// object or monster such as a quest chest or boss), waypoint, previous exit -- and one optional extra exit. Each gets a line from the player in its own
// colour (qol_ui.ini [automap], -1 = off) and a small square on the destination. Positions are looked up once per level (retried a few times while
// parts are missing), never per frame. Presets come from the level's Room2 list (rooms the game has not loaded get their data added for the look,
// like BH's FindPresetLocation); exits are tile presets or, on wilderness borders, the walkable gap on the border with the neighbouring level.
#include "atvectors.h"
enum { AM_NEXT, AM_OTHER, AM_WP, AM_PREV, AM_EXTRA, AM_SLOTS };
static int g_am_color[AM_SLOTS] = {10, 13, 154, 5, 24};        // BH's Next / Other / WP / Prev / Other Extra
static struct { int x, y, ok; } g_tgt[AM_SLOTS];
static DWORD g_exit_level = 0xFFFFFFFFu;                         // the level the targets belong to

typedef void (__stdcall *am_room_t)(DWORD act, int level, int x, int y, DWORD room1);   // D2Common #10890 AddRoomData / #10208 RemoveRoomData
static am_room_t p_addroom, p_delroom;

static void am_to_screen(int x, int y, int *ox, int *oy) {      // BH Hook::ScreenToAutomap
    x *= 32; y *= 32;
    *ox = ((x - y) / 2 / *p_divisor) - p_offset[0] + 8;
    *oy = ((x + y) / 4 / *p_divisor) - p_offset[1] - 8;
    if (p_mapsize()) { --*ox; *oy += 5; }
}

typedef struct { int slot, kind; DWORD type, id, target, area; int ok, x, y, by_tile, best; } am_want;   // kind 0 = preset, 1 = exit to level `target`

static DWORD am_level_ptr(DWORD act_misc, DWORD no) {          // BH AutoTele::GetLevel: the act's level list
    int g = 0;
    for (DWORD lv = *(DWORD *)(act_misc + 0x47C); lv && readable((void *)lv, 0x1D4) && g < 200; lv = *(DWORD *)(lv + 0x1AC), g++)
        if (*(DWORD *)(lv + 0x1D0) == no && *(DWORD *)(lv + 0x1C) > 0) return lv;
    return 0;
}
static DWORD am_tile_dest(DWORD room2, DWORD tile_no) {         // BH GetTileLevelNo
    int g = 0;
    for (DWORD t = *(DWORD *)(room2 + 0x4C); t && readable((void *)t, 0x14) && g < 64; t = *(DWORD *)(t + 4), g++) {
        DWORD *num = *(DWORD **)(t + 0x10), r2 = *(DWORD *)t;
        if (!num || !readable(num, 4) || *num != tile_no || !r2 || !readable((void *)r2, 0x5C)) continue;
        DWORD lv = *(DWORD *)(r2 + 0x58);
        return (lv && readable((void *)lv, 0x1D4)) ? *(DWORD *)(lv + 0x1D0) : 0;
    }
    return 0;
}
// ---- wilderness exits, the BH AutoTele way (CCollisionMap::BuildMapData / FillGaps / GetLevelExits) ----
// A collision map of the level is built from the rooms connected to the player's room (rooms the game has not loaded get their data added for the look),
// thin gaps are filled in (twice), then the first walkable gap on each of the four outer edges is taken as an exit; its centre is matched to the neighbouring
// level whose room sits next to it.
typedef struct { DWORD target; int x, y; } am_exit;
static int am_blocked(const WORD *m, int W, int H, int x, int y) { (void)H; return m[y * W + x] % 2; }
static int am_is_gap(const WORD *m, int W, int H, int x, int y) {      // BH CCollisionMap::IsGap: a walkable cell without 3 walkable in a row both ways
    if (m[y * W + x] % 2) return 0;
    int n = 0;
    for (int i = x - 2; i <= x + 2 && n < 3; i++) { if (i < 0 || i >= W || m[y * W + i] % 2) n = 0; else n++; }
    if (n < 3) return 1;
    n = 0;
    for (int i = y - 2; i <= y + 2 && n < 3; i++) { if (i < 0 || i >= H || m[i * W + x] % 2) n = 0; else n++; }
    return n < 3;
}
static int am_build_exits(DWORD level, DWORD act, DWORD proom1, am_exit *out, int max) {
    int W = (int)*(DWORD *)(level + 0x24) * 5, H = (int)*(DWORD *)(level + 0x28) * 5, ox = (int)*(DWORD *)(level + 0x1C) * 5, oy = (int)*(DWORD *)(level + 0x20) * 5;
    DWORD lno = *(DWORD *)(level + 0x1D0);
    if (W <= 0 || H <= 0 || W > 4096 || H > 4096 || (long)W * H > 4000000L) return 0;
    WORD *m = (WORD *)malloc((size_t)W * (size_t)H * sizeof(WORD)); if (!m) return 0;
    for (long i = 0; i < (long)W * H; i++) m[i] = 0xFFFF;                // MAP_DATA_INVALID (odd = not walkable)
    DWORD *stk = (DWORD *)malloc(2048 * sizeof(DWORD)), *vis = (DWORD *)malloc(2048 * sizeof(DWORD));
    if (!stk || !vis) { free(m); free(stk); free(vis); return 0; }
    int sp = 0, nv = 0;
    DWORD room1p = *(DWORD *)(proom1 + 0x10); if (room1p) stk[sp++] = room1p;   // flood fill from the player's room along the near-room lists
    while (sp > 0) {
        DWORD r = stk[--sp];
        if (!r || !readable((void *)r, 0x60) || *(DWORD *)(r + 0x58) != level) continue;
        int seen = 0; for (int i = 0; i < nv; i++) if (vis[i] == r) { seen = 1; break; }
        if (seen || nv >= 2048) continue;
        vis[nv++] = r;
        int did_add = 0;
        if (!*(DWORD *)(r + 0x30) && p_addroom && act) { p_addroom(act, (int)lno, (int)*(DWORD *)(r + 0x34), (int)*(DWORD *)(r + 0x38), proom1); did_add = 1; }
        DWORD r1 = *(DWORD *)(r + 0x30), coll = (r1 && readable((void *)r1, 0x24)) ? *(DWORD *)(r1 + 0x20) : 0;
        if (coll && readable((void *)coll, 0x28)) {
            int px = (int)*(DWORD *)coll - ox, py = (int)*(DWORD *)(coll + 4) - oy, sx = (int)*(DWORD *)(coll + 8), sy = (int)*(DWORD *)(coll + 0xC);
            WORD *src = *(WORD **)(coll + 0x20);
            if (src && sx > 0 && sy > 0 && sx <= 512 && sy <= 512 && readable(src, (size_t)sx * (size_t)sy * 2)) {
                for (int j = 0; j < sy; j++) for (int i = 0; i < sx; i++) {
                    int gx = px + i, gy = py + j; if (gx < 0 || gy < 0 || gx >= W || gy >= H) continue;
                    WORD v = src[j * sx + i]; m[gy * W + gx] = v == 1024 ? 11115 : v;
                }
            }
        }
        DWORD *nr = *(DWORD **)(r + 8); int nn = (int)*(DWORD *)(r + 0x2C);
        if (nr && nn > 0 && nn < 64 && readable(nr, (size_t)nn * 4)) for (int k = 0; k < nn && sp < 2040; k++) stk[sp++] = nr[k];
        if (did_add) p_delroom(act, (int)lno, (int)*(DWORD *)(r + 0x34), (int)*(DWORD *)(r + 0x38), proom1);
    }
    for (int pass = 0; pass < 2; pass++)                                  // FillGaps twice
        for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) if (am_is_gap(m, W, H, x, y)) m[y * W + x] = 11111;
    int sx0[4], sy0[4], sx1[4], sy1[4], np = 0;                           // the first walkable run on each outer edge
    for (int i = 0; i < W; i++) if (!am_blocked(m, W, H, i, 0)) { sx0[np] = i; sy0[np] = 0; sx1[np] = W - 1; sy1[np] = 0; for (i++; i < W; i++) if (am_blocked(m, W, H, i, 0)) { sx1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < W; i++) if (!am_blocked(m, W, H, i, H - 1)) { sx0[np] = i; sy0[np] = H - 1; sx1[np] = W - 1; sy1[np] = H - 1; for (i++; i < W; i++) if (am_blocked(m, W, H, i, H - 1)) { sx1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < H; i++) if (!am_blocked(m, W, H, 0, i)) { sx0[np] = 0; sy0[np] = i; sx1[np] = 0; sy1[np] = H - 1; for (i++; i < H; i++) if (am_blocked(m, W, H, 0, i)) { sy1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < H; i++) if (!am_blocked(m, W, H, W - 1, i)) { sx0[np] = W - 1; sy0[np] = i; sx1[np] = W - 1; sy1[np] = H - 1; for (i++; i < H; i++) if (am_blocked(m, W, H, W - 1, i)) { sy1[np] = i - 1; break; } np++; break; }
    int cx[4], cy[4];
    for (int i = 0; i < np; i++) { cx[i] = sx0[i] + (sx1[i] - sx0[i] > 0 ? (sx1[i] - sx0[i]) / 2 : 0); cy[i] = sy0[i] + (sy1[i] - sy0[i] > 0 ? (sy1[i] - sy0[i]) / 2 : 0); }
    int n = 0, guard = 0;
    for (DWORD r = *(DWORD *)(level + 0x10); r && readable((void *)r, 0x60) && guard < 4096; r = *(DWORD *)(r + 0x24), guard++) {
        DWORD *nr = *(DWORD **)(r + 8); int nn = (int)*(DWORD *)(r + 0x2C);
        if (!nr || nn <= 0 || nn >= 64 || !readable(nr, (size_t)nn * 4)) continue;
        for (int k = 0; k < nn; k++) {
            DWORD o = nr[k]; if (!o || !readable((void *)o, 0x60)) continue;
            DWORD ol = *(DWORD *)(o + 0x58); if (!ol || !readable((void *)ol, 0x1D4)) continue;
            DWORD olno = *(DWORD *)(ol + 0x1D0); if (olno == lno) continue;
            int rx = (int)*(DWORD *)(r + 0x34) * 5, ry = (int)*(DWORD *)(r + 0x38) * 5, rw = (int)*(DWORD *)(r + 0x3C) * 5, rh = (int)*(DWORD *)(r + 0x40) * 5;
            for (int j = 0; j < np; j++)
                if (cx[j] + ox >= rx && cx[j] + ox <= rx + rw && cy[j] + oy >= ry && cy[j] + oy <= ry + rh && n < max) { out[n].target = olno; out[n].x = cx[j] + ox; out[n].y = cy[j] + oy; n++; }
            break;                                                        // BH looks at the first neighbour of another level only
        }
    }
    free(m); free(stk); free(vis);
    return n;
}

// One pass over the rooms of level `lv` (number `lno`) resolving the wants that belong to it. Returns how many wants are still unresolved.
static DWORD g_vis[1024]; static int g_nvis;                     // rooms of this level already looked at (a retry only looks at the rest)
static int am_visited(DWORD r) { for (int i = 0; i < g_nvis; i++) if (g_vis[i] == r) return 1; return 0; }
static void am_mark(DWORD r) { if (g_nvis < 1024 && !am_visited(r)) g_vis[g_nvis++] = r; }
static DWORD g_nb_seen[16]; static int g_nb_n; static int g_tiles_seen;     // diagnostics: neighbouring levels and tile presets met by the last pass
static int am_pass(DWORD lv, DWORD lno, DWORD act, DWORD proom1, am_want *w, int nw, int cur_level, int *rooms, int *added) {
    int guard = 0;
    for (DWORD r = *(DWORD *)(lv + 0x10); r && readable((void *)r, 0x60) && guard < 4096; r = *(DWORD *)(r + 0x24), guard++) {
        if (am_visited(r)) continue;                             // done on an earlier attempt
        (*rooms)++;
        int open = 0;
        for (int i = 0; i < nw; i++) if (w[i].area == lno && !w[i].ok) open = 1;
        for (int i = 0; i < nw && !open; i++) if (cur_level && w[i].kind == 1 && !w[i].by_tile) open = 1;
        if (!open) break;                                        // everything found
        int did_add = 0;
        int need_tiles = 0; for (int i = 0; i < nw; i++) if (cur_level && w[i].kind == 1 && !w[i].by_tile) need_tiles = 1;
        // BH FindPresetLocation adds a room only when it has no presets, no tiles and no Room1; BH GetLevelExits (stairs, doors) adds EVERY room without a Room1,
        // because a room that has some presets can still lack its stairs until its data is added. Exits wanted -> the second rule.
        if (!*(DWORD *)(r + 0x30) && p_addroom && act && (need_tiles || (!*(DWORD *)(r + 0x5C) && !*(DWORD *)(r + 0x4C)))) {
            p_addroom(act, (int)lno, (int)*(DWORD *)(r + 0x34), (int)*(DWORD *)(r + 0x38), proom1); did_add = 1; (*added)++;
        }
        DWORD roomx = *(DWORD *)(r + 0x34), roomy = *(DWORD *)(r + 0x38);
        int pg = 0;
        for (DWORD ps = *(DWORD *)(r + 0x5C); ps && readable((void *)ps, 0x1C) && pg < 256; ps = *(DWORD *)(ps + 0x0C), pg++) {
            DWORD type = *(DWORD *)(ps + 0x14), id = *(DWORD *)(ps + 4);
            int x = (int)(*(DWORD *)(ps + 8) + roomx * 5), y = (int)(*(DWORD *)(ps + 0x18) + roomy * 5);
            DWORD dest = (type == 5 && cur_level) ? am_tile_dest(r, id) : 0;
            if (type == 5) g_tiles_seen++;
            for (int i = 0; i < nw; i++) {
                if (w[i].kind == 0 && !w[i].ok && w[i].area == lno && w[i].type == type && w[i].id == id) { w[i].ok = 1; w[i].x = x; w[i].y = y; }
                else if (w[i].kind == 1 && dest && w[i].target == dest && !w[i].by_tile) { w[i].ok = 1; w[i].by_tile = 1; w[i].x = x; w[i].y = y; }
            }
        }
        int got = *(DWORD *)(r + 0x30) || *(DWORD *)(r + 0x5C) || *(DWORD *)(r + 0x4C);   // data was there (or the add worked): this room is done
        if (did_add) p_delroom(act, (int)lno, (int)roomx, (int)roomy, proom1);
        if (got) am_mark(r);                                     // a room that could not be opened is tried again next time
    }
    int open = 0;
    for (int i = 0; i < nw; i++) if (!w[i].ok && (w[i].area == lno)) open++;
    return open;
}

// Finds the destinations for the level the player is in. Returns how many things are still unresolved.
static int am_resolve(DWORD level, DWORD act, DWORD proom1) {
    DWORD my = *(DWORD *)(level + 0x1D0), misc = *(DWORD *)(level + 0x1B4);
    if (my >= AT_AREAS || !misc || !readable((void *)misc, 0x480)) return 0;
    am_want w[AM_SLOTS]; int nw = 0, canyon = 0;
    for (int i = 0; i < 4; i++) {
        at_vec_t v = at_vectors[my * 4 + i];
        if (my == 46 && v.type == 0 && v.id == 0 && !canyon) { canyon = 1; v.type = 6; v.id = (unsigned short)*(DWORD *)(misc + 0x94); }   // Canyon of the Magi: the tomb with the staff
        if (g_tgt[i].ok) continue;
        if (v.type == 7) { g_tgt[i].x = v.id; g_tgt[i].y = v.id2; g_tgt[i].ok = 1; continue; }
        if ((v.type == 1 || v.type == 2 || v.type == 5) && v.id) { w[nw] = (am_want){i, 0, v.type, v.id, 0, v.area ? v.area : my, 0, 0, 0, 0, 0}; nw++; }
        else if ((v.type == 6 || v.type == 8) && v.id) {
            w[nw] = (am_want){i, 1, 0, 0, v.id, my, 0, 0, 0, 0, 0}; nw++;
            if (v.type == 8 && v.id2 && !g_tgt[AM_EXTRA].ok) { w[nw] = (am_want){AM_EXTRA, 1, 0, 0, v.id2, my, 0, 0, 0, 0, 0}; nw++; }
        }
    }
    if (!nw) return 0;
    int rooms = 0, added = 0; g_nb_n = 0; g_tiles_seen = 0;
    DWORD seen[AM_SLOTS]; int ns = 0;
    for (int i = 0; i < nw; i++) {                               // one pass per distinct level (almost always only the current one)
        int dup = 0; for (int j = 0; j < ns; j++) if (seen[j] == w[i].area) dup = 1;
        if (dup) continue;
        seen[ns++] = w[i].area;
        DWORD lv = w[i].area == my ? level : am_level_ptr(misc, w[i].area);
        if (lv && readable((void *)lv, 0x1D4)) am_pass(lv, w[i].area, act, proom1, w, nw, w[i].area == my, &rooms, &added);
    }
    static int exit_tries; static DWORD exit_level;
    if (exit_level != my) { exit_level = my; exit_tries = 0; }
    int want_exit = 0; for (int i = 0; i < nw; i++) if (w[i].kind == 1 && !w[i].ok) want_exit = 1;
    if (want_exit && exit_tries < 3) {                                      // BH's way: a collision map of the level and its edges (at most 3 attempts per level)
        exit_tries++;
        am_exit ex[64]; int ne = am_build_exits(level, act, proom1, ex, 64);
        for (int i = 0; i < nw; i++) {
            if (w[i].kind != 1 || w[i].by_tile) continue;
            for (int e = 0; e < ne; e++) if (ex[e].target == w[i].target) { w[i].ok = 1; w[i].x = ex[e].x; w[i].y = ex[e].y; }       // later ones win, as in BH
        }
        if (g_logging) logf_("automap:   BH-style exits found: %d", ne);
    }
    int open = 0;
    for (int i = 0; i < nw; i++) {
        if (w[i].ok) { g_tgt[w[i].slot].x = w[i].x; g_tgt[w[i].slot].y = w[i].y; g_tgt[w[i].slot].ok = 1; } else open++;
    }
    if (g_logging) {
        for (int i = 0; i < nw; i++) logf_("automap:   slot %d wants %s %lu (%s): %s at %d,%d%s", w[i].slot, w[i].kind ? "exit to level" : "preset type", w[i].kind ? (unsigned long)w[i].target : (unsigned long)w[i].type, w[i].kind ? "" : "id",
                                           w[i].ok ? "found" : "NOT found", w[i].x, w[i].y, w[i].ok && w[i].kind ? (w[i].by_tile ? " (tile)" : " (border)") : "");
        char nb[96] = ""; for (int q = 0; q < g_nb_n; q++) { char t[12]; snprintf(t, sizeof t, "%s%lu", q ? "," : "", (unsigned long)g_nb_seen[q]); lstrcatA(nb, t); }
        logf_("automap:   neighbouring levels seen: [%s], tile presets seen: %d", nb, g_tiles_seen);
    }
    logf_("automap: level %lu: %d of %d targets found (%d rooms looked at, %d opened for the look) [next %d other %d wp %d prev %d extra %d]", (unsigned long)my, nw - open, nw, rooms, added,
          g_tgt[0].ok, g_tgt[1].ok, g_tgt[2].ok, g_tgt[3].ok, g_tgt[4].ok);
    return open;
}

static void am_square(int x, int y, int color) { if (p_rect) p_rect(x - 2, y - 2, x + 3, y + 3, (DWORD)color, 7); }

static DWORD am_player_level(DWORD *path_out, DWORD *level_out, DWORD *room1_out) {   // the level number the player is in (0 = not in a game)
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x30)) return 0;
    DWORD path = *(DWORD *)(pl + 0x2C);
    if (!path || !readable((void *)path, 0x20)) return 0;
    DWORD room1 = *(DWORD *)(path + 0x1C);
    if (!room1 || !readable((void *)room1, 0x20)) return 0;
    DWORD room2 = *(DWORD *)(room1 + 0x10);
    if (!room2 || !readable((void *)room2, 0x5C)) return 0;
    DWORD level = *(DWORD *)(room2 + 0x58);
    if (!level || !readable((void *)level, 0x1D4)) return 0;
    *path_out = path; *level_out = level; *room1_out = room1;
    return *(DWORD *)(level + 0x1D0);
}

// Runs on the game thread from the game loop (like BH's AutoTele::OnLoop): on entering a level, looks up the destinations.
static void __cdecl am_loop(void) {
    static DWORD last; static int tries;
    DWORD now = GetTickCount();
    if (!g_exit_lines || now - last < 200) return;
    last = now;
    DWORD path, level, room1, no = am_player_level(&path, &level, &room1);
    if (no == 0) return;
    if (no != g_exit_level) { g_name_gen++; g_exit_level = no; memset(g_tgt, 0, sizeof g_tgt); g_nvis = 0; tries = 0; }
    if (tries == 0) {                                            // BH AutoTele::OnLoop -> GetVectors: ONE pass when the player's area changes, never repeated (repeated AddRoomData / RemoveRoomData calls can race the single-player server thread, e.g. on a Sorceress teleport)
        tries = 1;
        DWORD misc = *(DWORD *)(level + 0x1B4); DWORD act = (misc && readable((void *)misc, 0x470)) ? *(DWORD *)(misc + 0x46C) : 0;
        am_resolve(level, act, room1);
    }
}

// Runs on the game thread at the end of the automap draw: only draws.
static void am_draw_dots(int px, int py) {                     // BH: items flagged revealed get their rules' boxes / dots / line on the map
    for (int i = 0; i < 256; i++) {                                // the few marked items, not every unit in every room
        mh_dot *d = &g_dots[i];
        if (!d->unit) continue;
        DWORD u = d->unit;
        if (!readable((void *)u, 0xF0) || *(DWORD *)u != 4 || *(DWORD *)(u + 0x0C) != d->uid || !(*(DWORD *)(u + 0xC4) & MH_UF_REVEALED)) { d->unit = 0; continue; }   // picked up / gone
        DWORD mode = *(DWORD *)(u + 0x10);
        if (mode != 3 && mode != 5) { d->unit = 0; continue; }     // no longer on the ground
        DWORD path = *(DWORD *)(u + 0x2C);
        if (!path || !readable((void *)path, 0x14)) continue;
        int sx, sy; am_to_screen((int)*(DWORD *)(path + 0x0C), (int)*(DWORD *)(path + 0x10), &sx, &sy);
        for (int k = 0; k < d->n; k++) {                           // BH: each rule's tier is checked against the current tier level at draw time
            const mh_dact *x = &d->a[k];
            if (x->ping > g_ping_level) continue;
            if (p_rect) {
                if (x->border != FI_UNDEF) p_rect(sx - 4, sy - 4, sx + 4, sy + 4, (DWORD)x->border, 7);
                if (x->map != FI_UNDEF) p_rect(sx - 3, sy - 3, sx + 3, sy + 3, (DWORD)x->map, 7);
                if (x->dot != FI_UNDEF) p_rect(sx - 2, sy - 2, sx + 2, sy + 2, (DWORD)x->dot, 7);
                if (x->px != FI_UNDEF) p_rect(sx - 1, sy - 1, sx + 1, sy + 1, (DWORD)x->px, 7);
            }
            if (x->line != FI_UNDEF) p_drawline(px, py, sx, sy, (DWORD)x->line, (DWORD)-1);
            if (x->stop) break;
        }
    }
}

// The game's cross-colour table (D2Client +0x11CF70..7F). Written every frame; a setting of -1, or the DLL being disabled, puts the game's own value back.
static void am_cross_colors(void) {
    static BYTE orig[16]; static int have;
    if (!g_am_cl) return;
    BYTE *t = g_am_cl + 0x11CF70;
    if (!have) { memcpy(orig, t, 16); have = 1; }
#define AMC(i, v) do { int c_ = master_on ? (v) : -1; t[i] = c_ >= 0 ? (BYTE)c_ : orig[i]; } while (0)
    AMC(4, g_me_color);                                                   // you
    AMC(8, g_merc_color);                                                 // units you own while you are in a party (merc, minions)
    AMC(5, g_other_color >= 0 ? g_other_color : g_merc_color);            // other players NOT in your party; the same entry colours your minions when you are not in a party (the player colour wins)
    AMC(7, g_team_color);                                                 // other players in your party
    AMC(9, g_teammin_color);                                              // minions of the players in your party (drawn only while the game's automap 'show party' option is on)
    AMC(0xC, g_portal_cross);                                             // portal crosses (town / red portals: objects 0x3B, 0x3C)
    AMC(0xA, g_npc_color);                                                // town NPCs (the game uses entry 7A for monsters that are NPCs)
#undef AMC
}

static void __cdecl am_draw(void) {
    if (!p_drawline) return;
    am_cross_colors();
    DWORD path, level, room1, no = am_player_level(&path, &level, &room1);
    if (no == 0) return;
    int px, py; am_to_screen(*(WORD *)(path + 2), *(WORD *)(path + 6), &px, &py);
    if (g_filter_on && g_fi && g_dots_on) am_draw_dots(px, py);
    if (!g_exit_lines || no != g_exit_level) return;
    for (int i = 0; i < AM_SLOTS; i++) {
        if (!g_tgt[i].ok || g_am_color[i] < 0) continue;
        int ex, ey; am_to_screen(g_tgt[i].x, g_tgt[i].y, &ex, &ey);
        p_drawline(px, py, ex, ey, (DWORD)g_am_color[i], (DWORD)-1);
        am_square(ex, ey, g_am_color[i]);
    }
}

// The game draws the NPC / party / other-player name labels with fixed text colours (immediate bytes in its unit-draw routine); change those bytes.
// A palette index (the colour of a cross) -> the nearest of the game's text colours (the text only has a dozen). Approximate RGBs of the text colours.
static int am_text_for_palette(int idx) {
    static const unsigned char tc[][4] = {{0, 255, 255, 255}, {1, 255, 64, 64}, {2, 0, 255, 0}, {3, 100, 100, 255}, {4, 199, 179, 119}, {5, 105, 105, 105},
                                          {7, 200, 160, 90}, {8, 255, 168, 0}, {9, 255, 255, 100}, {10, 0, 128, 0}, {11, 170, 0, 255}};   // 6 (black) is left out: unreadable on the map
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < (int)(sizeof tc / sizeof *tc); i++) {
        int dr = am_pal[idx][0] - tc[i][1], dg = am_pal[idx][1] - tc[i][2], db = am_pal[idx][2] - tc[i][3], d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = tc[i][0]; }
    }
    return best;
}
static BYTE *g_lbl_site[4]; static BYTE g_lbl_orig[4];            // the patched operand bytes and the game's own values (found once; re-applied on .reload)
static void am_label_apply(void) {
    static const int *val[4] = {&g_lbl_npc, &g_lbl_team, &g_lbl_other, &g_lbl_other};
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!g_lbl_site[i]) continue;
        int v0 = master_on ? *val[i] : -1;                                              // -1 game's own; 0..255 a palette colour (nearest text colour); 256 + n the text colour n itself
        BYTE v = v0 < 0 ? g_lbl_orig[i] : v0 >= 256 ? (BYTE)(v0 - 256) : (BYTE)am_text_for_palette(v0);
        if (*g_lbl_site[i] != v) patch_bytes(g_lbl_site[i], &v, 1);
        n++;
    }
    logf_("automap: name labels: NPC text colour %d, party %d, other players %d (%d sites)", g_lbl_site[0] ? *g_lbl_site[0] : -1, g_lbl_site[1] ? *g_lbl_site[1] : -1, g_lbl_site[2] ? *g_lbl_site[2] : -1, n);
}
static void am_label_colors(BYTE *code, size_t size) {          // find the four sites once (always, so a later .reload can use them), then apply the settings
    for (int i = 0; i < 4; i++) {
        int cur = 0; long at = sig_find_labelcolor(code, size, i, &cur);
        if (at < 0) { logf_("automap: label colour site %d not found (or more than once); left alone", i); continue; }
        g_lbl_site[i] = code + at; g_lbl_orig[i] = (BYTE)cur;
    }
    am_label_apply();
}

static DWORD WINAPI automap_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL, gfx = NULL;
    for (int i = 0; i < 6000 && !(cl && gfx); i++) { cl = GetModuleHandleA("D2Client.dll"); gfx = GetModuleHandleA("D2gfx.dll"); if (!(cl && gfx)) Sleep(100); }
    if (!cl || !gfx) { logf_("automap: game modules not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("automap: this D2Client build is not supported (1.13d only); off"); return 0; }
    p_drawline = (am_line_t)GetProcAddress(gfx, (LPCSTR)10013);
    g_am_cl = (BYTE *)cl;
    { HMODULE cm = GetModuleHandleA("D2Common.dll"); if (cm) { p_addroom = (am_room_t)GetProcAddress(cm, (LPCSTR)10890); p_delroom = (am_room_t)GetProcAddress(cm, (LPCSTR)10208); } }
    p_rect = (am_rect_t)GetProcAddress(gfx, (LPCSTR)10028);
    if (!p_drawline) { logf_("automap: D2gfx DrawLine not found; off"); return 0; }
    p_player = (mh_player_t)((BYTE *)cl + 0x613C0); p_mapsize = (am_size_t)((BYTE *)cl + 0x6FDD0);
    p_divisor = (int *)((BYTE *)cl + 0xF34F8); p_offset = (LONG *)((BYTE *)cl + 0x11CF5C);
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; int patched = 0;
        if (g_lbl_npc >= 0 || g_lbl_team >= 0 || g_lbl_other >= 0) { if (sig_find_labelcolor(code, sec->Misc.VirtualSize, 0, NULL) >= 0 || sig_find_labelcolor(code, sec->Misc.VirtualSize, 1, NULL) >= 0) am_label_colors(code, sec->Misc.VirtualSize); }   // only in the section that holds the unit-draw routine
        long at = sig_find_automap(code, sec->Misc.VirtualSize, &patched);
        if (at < 0) continue;
        if (patched) { logf_("automap: hook already installed"); return 0; }
        BYTE *site = code + at, *st = stub_alloc(); if (!st) return 0;
        DWORD far_target = (DWORD)(site + 9) + *(DWORD *)(site + 5);   // the original `jmp rel32` at site+4
        static const BYTE a[] = {0x60};                                // pushad
        static const BYTE b[] = {0x61, 0x8B, 0xCF, 0x5F, 0x5E};        // popad ; mov ecx,edi ; pop edi ; pop esi
        BYTE *p = emit(st, a, 1);
        p = emit_rel(p, 0xE8, (DWORD)am_draw);
        p = emit(p, b, sizeof b);
        p = emit_rel(p, 0xE9, far_target);
        BYTE j[9]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5); memset(j + 5, 0x90, 4);
        patch_bytes(site, j, 9);
        {                                                              // the game's own top-right texts (area name, game name, version ...): map_info draws its own
            static const struct { int rel; DWORD fn; } lab[] = {{-44, 0x709E0}, {-39, 0x70900}, {-34, 0x70790}, {-19, 0x70880}, {-5, 0x70A30}};
            int hidden = 0;
            for (int q = 0; q < 5; q++) {
                BYTE *c = site + lab[q].rel;                           // each is a `call rel32` to a text-drawing routine; checked before it is replaced by NOPs
                if (c[0] == 0xE8 && (DWORD)(c + 5) + *(DWORD *)(c + 1) == (DWORD)base + lab[q].fn) { static const BYTE nop5[5] = {0x90, 0x90, 0x90, 0x90, 0x90}; patch_bytes(c, nop5, 5); hidden++; }
            }
            logf_("automap: %d of 5 game label drawers hidden", hidden);
        }
        logf_("automap: map lines on (D2Client +%lx; colours next %d other %d waypoint %d previous %d extra %d)", (unsigned long)(site - base), g_am_color[0], g_am_color[1], g_am_color[2], g_am_color[3], g_am_color[4]);
        return 0;
    }
    logf_("automap: the automap-draw code was not found in D2Client (or appears more than once); exit lines off");
    return 0;
}
#endif
