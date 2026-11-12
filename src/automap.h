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
// ---- BH AutoTele, copied step by step: GetVectors -> CCollisionMap::CreateMap / Search / FillGaps / GetLevelExits, FindPresetLocation ----
// Everything runs once on the game thread when the player's area changes (am_loop). Room data is added and removed in exactly BH's order.
typedef struct { DWORD target; int x, y; } am_exit;
static int am_blocked(const WORD *m, int W, int H, int x, int y) { (void)H; return m[y * W + x] % 2; }
static int am_is_gap(const WORD *m, int W, int H, int x, int y) {      // BH CCollisionMap::IsGap
    if (m[y * W + x] % 2) return 0;
    int n = 0;
    for (int i = x - 2; i <= x + 2 && n < 3; i++) { if (i < 0 || i >= W || m[y * W + i] % 2) n = 0; else n++; }
    if (n < 3) return 1;
    n = 0;
    for (int i = y - 2; i <= y + 2 && n < 3; i++) { if (i < 0 || i >= H || m[i * W + x] % 2) n = 0; else n++; }
    return n < 3;
}
static DWORD am_cur_room1(void) {                                      // BH reads player->pPath->pRoom1 again for every AddRoomData / RemoveRoomData
    DWORD pl = p_player(); if (!pl || !readable((void *)pl, 0x30)) return 0;
    DWORD path = *(DWORD *)(pl + 0x2C); if (!path || !readable((void *)path, 0x20)) return 0;
    return *(DWORD *)(path + 0x1C);
}
static DWORD am_cur_act(void) { DWORD pl = p_player(); return (pl && readable((void *)pl, 0x30)) ? *(DWORD *)(pl + 0x1C) : 0; }
static int am_add_room(DWORD r, DWORD lno) {
    if (!g_am_load_rooms) return 0;                                    // read-only mode: never touch the game's room data
    DWORD act = am_cur_act(), r1 = am_cur_room1();
    if (!p_addroom || !act || !r1) return 0;
    p_addroom(act, (int)lno, (int)*(DWORD *)(r + 0x34), (int)*(DWORD *)(r + 0x38), r1); return 1;
}
static void am_del_room(DWORD r, DWORD lno) {
    DWORD act = am_cur_act(), r1 = am_cur_room1();
    if (p_delroom && act && r1) p_delroom(act, (int)lno, (int)*(DWORD *)(r + 0x34), (int)*(DWORD *)(r + 0x38), r1);
}

static struct { WORD *m; int W, H, ox, oy; DWORD lno; DWORD skip[2048]; int nskip; } g_cm;   // BH CCollisionMap (one at a time)
static void am_cm_search(DWORD r, DWORD scan, int depth) {             // BH CCollisionMap::Search (AddCollisionData inlined)
    if (!r || depth > 1500 || !readable((void *)r, 0x60)) return;
    DWORD lv = *(DWORD *)(r + 0x58);
    if (!lv || !readable((void *)lv, 0x1D4) || *(DWORD *)(lv + 0x1D0) != scan) return;
    for (int i = 0; i < g_cm.nskip; i++) if (g_cm.skip[i] == r) return;
    if (g_cm.nskip >= 2048) return;
    int add = 0;
    if (!*(DWORD *)(r + 0x30)) add = am_add_room(r, scan);
    g_cm.skip[g_cm.nskip++] = r;
    DWORD r1 = *(DWORD *)(r + 0x30), coll = (r1 && readable((void *)r1, 0x24)) ? *(DWORD *)(r1 + 0x20) : 0;
    if (coll && readable((void *)coll, 0x28)) {
        int px = (int)*(DWORD *)coll - g_cm.ox, py = (int)*(DWORD *)(coll + 4) - g_cm.oy, sx = (int)*(DWORD *)(coll + 8), sy = (int)*(DWORD *)(coll + 0xC);
        WORD *src = *(WORD **)(coll + 0x20);
        if (src && sx > 0 && sy > 0 && sx <= 512 && sy <= 512 && readable(src, (size_t)sx * (size_t)sy * 2))
            for (int j = 0; j < sy; j++) for (int i = 0; i < sx; i++) {
                int gx = px + i, gy = py + j; if (gx < 0 || gy < 0 || gx >= g_cm.W || gy >= g_cm.H) continue;
                WORD v = src[j * sx + i]; g_cm.m[gy * g_cm.W + gx] = v == 1024 ? 11115 : v;
            }
    }
    DWORD *nr = *(DWORD **)(r + 8); int nn = (int)*(DWORD *)(r + 0x2C);
    if (nr && nn > 0 && nn < 64 && readable(nr, (size_t)nn * 4)) { DWORD near_[64]; memcpy(near_, nr, (size_t)nn * 4); for (int k = 0; k < nn; k++) am_cm_search(near_[k], scan, depth + 1); }
    if (add) am_del_room(r, scan);                                     // BH removes the room only after its neighbours were searched
}
static int am_cm_create(DWORD misc, const DWORD *areas, int na) {      // BH BuildMapData: origin = top-left of the levels, size = sum of the level sizes
    memset(&g_cm, 0, sizeof g_cm);
    DWORD first = am_level_ptr(misc, areas[0]); if (!first || !readable((void *)first, 0x1D4)) return 0;
    g_cm.ox = (int)*(DWORD *)(first + 0x1C) * 5; g_cm.oy = (int)*(DWORD *)(first + 0x20) * 5; g_cm.lno = areas[0];
    int W = 0, H = 0;
    for (int n = 0; n < na; n++) {
        DWORD lv = am_level_ptr(misc, areas[n]); if (!lv || !readable((void *)lv, 0x1D4)) continue;
        if (g_cm.ox / 5 > (int)*(DWORD *)(lv + 0x1C)) g_cm.ox = (int)*(DWORD *)(lv + 0x1C) * 5;
        if (g_cm.oy / 5 > (int)*(DWORD *)(lv + 0x20)) g_cm.oy = (int)*(DWORD *)(lv + 0x20) * 5;
        W += (int)*(DWORD *)(lv + 0x24) * 5; H += (int)*(DWORD *)(lv + 0x28) * 5;
    }
    if (W <= 0 || H <= 0 || W > 4096 || H > 4096 || (long)W * H > 4000000L) return 0;
    g_cm.m = (WORD *)malloc((size_t)W * (size_t)H * sizeof(WORD)); if (!g_cm.m) return 0;
    g_cm.W = W; g_cm.H = H;
    for (long i = 0; i < (long)W * H; i++) g_cm.m[i] = 0xFFFF;         // MAP_DATA_INVALID
    for (int n = 0; n < na; n++) {
        DWORD lv = am_level_ptr(misc, areas[n]); if (!lv || !readable((void *)lv, 0x1D4)) continue;
        am_cm_search(*(DWORD *)(lv + 0x10), areas[n], 0);
    }
    for (int pass = 0; pass < 2; pass++)                               // FillGaps twice
        for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) if (am_is_gap(g_cm.m, W, H, x, y)) g_cm.m[y * W + x] = 11111;
    return 1;
}
static int am_cm_exits(DWORD misc, am_exit *out, int max) {           // BH CCollisionMap::GetLevelExits
    WORD *m = g_cm.m; int W = g_cm.W, H = g_cm.H, ox = g_cm.ox, oy = g_cm.oy; DWORD lno = g_cm.lno;
    int sx0[4], sy0[4], sx1[4], sy1[4], np = 0;
    for (int i = 0; i < W; i++) if (!am_blocked(m, W, H, i, 0)) { sx0[np] = i; sy0[np] = 0; sx1[np] = W - 1; sy1[np] = 0; for (i++; i < W; i++) if (am_blocked(m, W, H, i, 0)) { sx1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < W; i++) if (!am_blocked(m, W, H, i, H - 1)) { sx0[np] = i; sy0[np] = H - 1; sx1[np] = W - 1; sy1[np] = H - 1; for (i++; i < W; i++) if (am_blocked(m, W, H, i, H - 1)) { sx1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < H; i++) if (!am_blocked(m, W, H, 0, i)) { sx0[np] = 0; sy0[np] = i; sx1[np] = 0; sy1[np] = H - 1; for (i++; i < H; i++) if (am_blocked(m, W, H, 0, i)) { sy1[np] = i - 1; break; } np++; break; }
    for (int i = 0; i < H; i++) if (!am_blocked(m, W, H, W - 1, i)) { sx0[np] = W - 1; sy0[np] = i; sx1[np] = W - 1; sy1[np] = H - 1; for (i++; i < H; i++) if (am_blocked(m, W, H, W - 1, i)) { sy1[np] = i - 1; break; } np++; break; }
    int cx[4], cy[4];
    for (int i = 0; i < np; i++) {
        int dx = sx1[i] - sx0[i], dy = sy1[i] - sy0[i];
        int xc = dx > 0 ? sx0[i] + (dx % 2 ? (dx - dx % 2) / 2 : dx / 2) : 0, yc = dy > 0 ? sy0[i] + (dy % 2 ? (dy - dy % 2) / 2 : dy / 2) : 0;
        cx[i] = xc ? xc : sx0[i]; cy[i] = yc ? yc : sy0[i];
    }
    DWORD lv0 = am_level_ptr(misc, lno); int n = 0, guard = 0;
    if (!lv0 || !readable((void *)lv0, 0x1D4)) return 0;
    for (DWORD r = *(DWORD *)(lv0 + 0x10); r && readable((void *)r, 0x60) && guard < 4096; r = *(DWORD *)(r + 0x24), guard++) {
        DWORD *nr = *(DWORD **)(r + 8); int nn = (int)*(DWORD *)(r + 0x2C);
        if (nr && nn > 0 && nn < 64 && readable(nr, (size_t)nn * 4)) {
            for (int k = 0; k < nn; k++) {
                DWORD o = nr[k]; if (!o || !readable((void *)o, 0x60)) continue;
                DWORD ol = *(DWORD *)(o + 0x58); if (!ol || !readable((void *)ol, 0x1D4)) continue;
                DWORD olno = *(DWORD *)(ol + 0x1D0); if (olno == lno) continue;
                int rx = (int)*(DWORD *)(r + 0x34) * 5, ry = (int)*(DWORD *)(r + 0x38) * 5, rw = (int)*(DWORD *)(r + 0x3C) * 5, rh = (int)*(DWORD *)(r + 0x40) * 5;
                for (int j = 0; j < np; j++)
                    if (cx[j] + ox >= rx && cx[j] + ox <= rx + rw && cy[j] + oy >= ry && cy[j] + oy <= ry + rh && n < max) { out[n].target = olno; out[n].x = cx[j] + ox; out[n].y = cy[j] + oy; n++; }
                break;
            }
        }
        int added = 0;
        if (!*(DWORD *)(r + 0x30)) added = am_add_room(r, lno);           // every room without a Room1, as BH
        int pg = 0;
        for (DWORD ps = *(DWORD *)(r + 0x5C); ps && readable((void *)ps, 0x1C) && pg < 256; ps = *(DWORD *)(ps + 0x0C), pg++) {
            if (*(DWORD *)(ps + 0x14) != 5) continue;
            DWORD dest = am_tile_dest(r, *(DWORD *)(ps + 4));
            if (!dest) continue;
            int x = (int)(*(DWORD *)(r + 0x34) * 5 + *(DWORD *)(ps + 8)), y = (int)(*(DWORD *)(r + 0x38) * 5 + *(DWORD *)(ps + 0x18)), have = 0;
            for (int i = 0; i < n; i++) if (out[i].x == x && out[i].y == y) have = 1;
            if (!have && n < max) { out[n].target = dest; out[n].x = x; out[n].y = y; n++; }
        }
        if (added) am_del_room(r, lno);
    }
    return n;
}
static void am_cm_free(void) { free(g_cm.m); memset(&g_cm, 0, sizeof g_cm); }

static int am_find_preset(DWORD misc, DWORD type, DWORD id, DWORD area, int *ox, int *oy) {   // BH FindPresetLocation
    *ox = *oy = 0;
    DWORD lv = am_level_ptr(misc, area); if (!lv || !readable((void *)lv, 0x1D4)) return 0;
    int guard = 0;
    for (DWORD r = *(DWORD *)(lv + 0x10); r && readable((void *)r, 0x60) && guard < 4096; r = *(DWORD *)(r + 0x24), guard++) {
        int added = 0, stop = 0;
        if (!*(DWORD *)(r + 0x5C) && !*(DWORD *)(r + 0x4C) && !*(DWORD *)(r + 0x30)) added = am_add_room(r, area);
        int pg = 0;
        for (DWORD ps = *(DWORD *)(r + 0x5C); ps && readable((void *)ps, 0x1C) && pg < 256; ps = *(DWORD *)(ps + 0x0C), pg++)
            if ((!type || type == *(DWORD *)(ps + 0x14)) && id == *(DWORD *)(ps + 4)) {
                *ox = (int)(*(DWORD *)(ps + 8) + *(DWORD *)(r + 0x34) * 5); *oy = (int)(*(DWORD *)(ps + 0x18) + *(DWORD *)(r + 0x38) * 5); stop = 1; break;
            }
        if (added) am_del_room(r, area);
        if (stop) break;
    }
    return *ox && *oy;
}

// BH AutoTele::GetVectors
static void am_get_vectors(DWORD level) {
    DWORD my = *(DWORD *)(level + 0x1D0), misc = *(DWORD *)(level + 0x1B4);
    if (my >= AT_AREAS || !misc || !readable((void *)misc, 0x480)) return;
    DWORD areas[2] = {my, 0}; int na = 1, build = 0;
    for (int i = 0; i < 4; i++) {
        at_vec_t v = at_vectors[my * 4 + i];
        if (v.type == 6 || v.type == 8 || (my == 46 && v.type == 0 && v.id == 0)) { if (v.area) { areas[1] = v.area; na = 2; } build = 1; }
    }
    am_exit ex[64]; int ne = 0;
    if (build && am_cm_create(misc, areas, na)) { ne = am_cm_exits(misc, ex, 64); am_cm_free(); build = 1; } else build = 0;
    memset(g_tgt, 0, sizeof g_tgt);
    for (int i = 0; i < 4; i++) {
        at_vec_t v = at_vectors[my * 4 + i];
        if (my == 46 && v.type == 0 && v.id == 0) { v.type = 6; v.id = (unsigned short)*(DWORD *)(misc + 0x94); }   // Canyon of the Magi: the tomb with the staff
        if (v.type == 7) { g_tgt[i].x = v.id; g_tgt[i].y = v.id2; }
        if (v.type == 1 || v.type == 2 || v.type == 5) { am_find_preset(misc, v.type, v.id, v.area ? v.area : my, &g_tgt[i].x, &g_tgt[i].y); }
        if ((v.type == 6 || v.type == 8) && build) {
            for (int e = 0; e < ne; e++) {
                if (ex[e].target == v.id) { g_tgt[i].x = ex[e].x; g_tgt[i].y = ex[e].y; }
                else if (v.type == 8 && ex[e].target == v.id2) { g_tgt[AM_EXTRA].x = ex[e].x; g_tgt[AM_EXTRA].y = ex[e].y; }
            }
        }
    }
    for (int i = 0; i < AM_SLOTS; i++) g_tgt[i].ok = g_tgt[i].x && g_tgt[i].y;     // BH draws a vector only when both coordinates are non-zero
    logf_("automap: level %lu [next %d other %d wp %d prev %d extra %d]", (unsigned long)my, g_tgt[0].ok, g_tgt[1].ok, g_tgt[2].ok, g_tgt[3].ok, g_tgt[4].ok);
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

// BH IsGameReady (D2Helpers.cpp): the player, path, room, level, act, inventory and a non-zero position must all exist.
static int am_game_ready(void) {
    DWORD pl = p_player(); if (!pl || !readable((void *)pl, 0x70)) return 0;
    DWORD path = *(DWORD *)(pl + 0x2C), act = *(DWORD *)(pl + 0x1C), inv = *(DWORD *)(pl + 0x60);
    if (!path || !act || !inv || !readable((void *)path, 0x20) || !readable((void *)act, 0x20)) return 0;
    if (!*(DWORD *)(act + 0x10) || !*(WORD *)(path + 2) || !*(WORD *)(path + 6)) return 0;      // act->pRoom1, xPos, yPos
    return 1;
}
// Runs on the game thread from the game loop, a copy of BH's AutoTele::OnLoop: when the player's area changes, GetVectors runs once.
static void __cdecl am_loop(void) {
    if (!g_exit_lines) return;
    if (!am_game_ready()) { g_exit_level = 0xFFFFFFFFu; return; }       // left the game / between games: the next game must look its level up again
    DWORD path, level, room1, no = am_player_level(&path, &level, &room1);
    static DWORD last_try;
    if (no == 0) return;
    if (no != g_exit_level) { g_name_gen++; g_exit_level = no; last_try = 0; memset(g_tgt, 0, sizeof g_tgt); }
    else if (g_am_load_rooms) return;                                   // BH way: one look-up per area change
    else {                                                              // read-only mode: look again every 2 s while something is missing (rooms load as you walk)
        int missing = 0; for (int i = 0; i < 4; i++) if (!g_tgt[i].ok && no < AT_AREAS && at_vectors[no * 4 + i].type != 0 && at_vectors[no * 4 + i].type != 9) missing = 1;
        DWORD now = GetTickCount(); if (!missing || now - last_try < 2000) return;
    }
    last_try = GetTickCount();
    am_get_vectors(level);
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
