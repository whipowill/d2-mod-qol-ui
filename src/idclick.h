// Item mouse helpers, ported from BH's ItemMover (the game window is subclassed for the mouse clicks):
//   Shift + left click on an unidentified item (inventory, stash, cube): identify it with the Tome of Identify in your inventory. Sends the game's own
//     packets: use the tome (0x20), then the identify-item packet (0x27).               [features] shift_click_id
//   Shift + right click on an item: move it inventory -> stash (or the open cube), stash -> open cube (else inventory), cube -> stash (else inventory).
//     Ctrl + right click: drop it on
//     the ground. Packets: pick up (0x19), then put in container (0x18) / drop (0x17).   [features] item_move
// Stock 1.13d only.
#ifndef UI_IDCLICK_H
#define UI_IDCLICK_H

typedef void (__stdcall *id_send_t)(size_t len, DWORD arg1, BYTE *packet);   // D2Net #10015
typedef DWORD (__stdcall *id_cursor_t)(void);                                // D2Client +0x144A0, the item held on the cursor
static id_send_t p_idsend; static id_cursor_t p_idcursor;
static void *(__stdcall *p_idtext)(DWORD); static DWORD (__stdcall *p_idstat)(DWORD, DWORD, DWORD);
static WNDPROC g_old_wndproc;
static DWORD g_mv_item, g_mv_x, g_mv_y, g_mv_dest, g_mv_at;                    // a pending move: the pickup was sent, the put-down follows one tick later
static DWORD g_view_unit;                                                       // the unit whose inventory is shown (0 = yours); see inspect.h
static void iv_tick(void);
static DWORD g_id_item, g_id_book, g_id_at;                                  // a pending identify: send 0x27 when the clock passes g_id_at

static int id_ui(int n) { return *(DWORD *)(g_hud_cl + 0x11C890 + 4 * n) != 0; }

// Finds the unidentified item under the mouse and the tome. Returns 1 and fills item/book if there is something to do.
static int id_find(DWORD *item_out, DWORD *book_out, DWORD pl) {
    DWORD inv = *(DWORD *)(pl + 0x60);
    if (!inv || !readable((void *)inv, 0x2C)) return 0;
    int mx = (int)*(DWORD *)(g_hud_cl + 0x11C950), my = (int)*(DWORD *)(g_hud_cl + 0x11C94C);
    DWORD item = 0, book = 0; int guard = 0;
    for (DWORD it = *(DWORD *)(inv + 0x0C); it && readable((void *)it, 0x30) && guard < 512; guard++) {
        DWORD data = *(DWORD *)(it + 0x14);
        if (!data || !readable((void *)data, 0x70)) break;
        BYTE *txt = (BYTE *)p_idtext(*(DWORD *)(it + 4));
        BYTE loc = *(BYTE *)(data + 0x45);
        if (txt && readable(txt, 0x111)) {
            if (!(*(DWORD *)(data + 0x18) & 0x10) && !item) {              // not identified
                static const struct { BYTE loc; int ui, layout; } where[] = {{0, 1, 0x1016F0}, {4, 0x19, 0x1015E0}, {3, 0x1A, 0x1016D8}};
                for (int w = 0; w < 3; w++) {
                    if (loc != where[w].loc) continue;
                    if (loc != 0 && !id_ui(where[w].ui)) continue;   // stash / cube items count only while that panel is open; the inventory is visible in all these states (with the stash open its own UI flag is not set)
                    BYTE *lay = g_hud_cl + where[w].layout;
                    int left = (int)*(DWORD *)(lay + 4), top = (int)*(DWORD *)(lay + 0xC), cell = lay[0x15];
                    DWORD path = *(DWORD *)(it + 0x2C);
                    if (cell == 0 || !path || !readable((void *)path, 0x14) || mx < left || my < top) continue;
                    int cx = (mx - left) / cell, cy = (my - top) / cell, px = (int)*(DWORD *)(path + 0x0C), py = (int)*(DWORD *)(path + 0x10);
                    if (cx >= px && cx < px + txt[0x10F] && cy >= py && cy < py + txt[0x110]) item = *(DWORD *)(it + 0x0C);
                }
            }
            if (!book && loc == 0 && !memcmp(txt + 0x80, "ibk", 3) && (int)p_idstat(it, 70, 0) > 0) book = *(DWORD *)(it + 0x0C);   // Tome of Identify with scrolls left
        }
        it = *(DWORD *)(data + 0x64);
    }
    if (!item || !book) return 0;
    *item_out = item; *book_out = book;
    return 1;
}

static int id_try_click(void) {
    static int logged;
#define ID_WHY(msg) do { if (g_logging && logged < 25) { logged++; logf_("shift-click identify: %s", msg); } } while (0)
    if (!g_shift_id || !g_hud_cl || !p_idsend || !p_idtext || !p_idstat || !p_player) return 0;
    if (g_view_unit) return 0;                                       // the inventory on screen is another unit's
    if (g_id_item) { ID_WHY("an identify is still pending"); return 0; }
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x30)) { ID_WHY("no player"); return 0; }
    if (p_idcursor && p_idcursor()) { ID_WHY("an item is on the cursor"); return 0; }
    if (!id_ui(1) && !id_ui(0x19) && !id_ui(0x1A)) { ID_WHY("no inventory / stash / cube open"); return 0; }
    DWORD item, book;
    if (!id_find(&item, &book, pl)) { ID_WHY("no unidentified item under the mouse, or no Tome of Identify with scrolls"); return 0; }
    ID_WHY("identifying: using the tome, then the item");
    DWORD path = *(DWORD *)(pl + 0x2C);
    if (!path || !readable((void *)path, 0x10)) return 0;
    BYTE pk[13] = {0x20};                                                  // use the tome (it then waits for an item to identify)
    *(DWORD *)(pk + 1) = book; *(WORD *)(pk + 5) = *(WORD *)(path + 2); *(WORD *)(pk + 9) = *(WORD *)(path + 6);
    p_idsend(13, 0, pk);
    g_id_item = item; g_id_book = book; g_id_at = GetTickCount() + 30;     // BH sends the second packet when the game answers; one game-loop tick later does the same
    return 1;
}

// Game thread, from the game loop.
static void mv_tick(void) {
    if (!g_mv_item || (int)(GetTickCount() - g_mv_at) < 0) return;
    if (g_mv_dest == 0xFF) { BYTE pk[5] = {0x17}; *(DWORD *)(pk + 1) = g_mv_item; p_idsend(5, 1, pk); }                            // drop on the ground
    else { BYTE pk[17] = {0x18}; *(DWORD *)(pk + 1) = g_mv_item; *(DWORD *)(pk + 5) = g_mv_x; *(DWORD *)(pk + 9) = g_mv_y; *(DWORD *)(pk + 13) = g_mv_dest; p_idsend(17, 1, pk); }   // put in a container
    g_mv_item = 0;
}
static void id_tick(void) {
    mv_tick();
    iv_tick();
    if (!g_id_item || (int)(GetTickCount() - g_id_at) < 0) return;
    BYTE pk[9] = {0x27}; *(DWORD *)(pk + 1) = g_id_item; *(DWORD *)(pk + 5) = g_id_book;
    p_idsend(9, 0, pk);
    g_id_item = g_id_book = 0;
}


// ---- shift / ctrl + right click: move an item ----
#define MV_MAX 16
typedef struct { int w, h; DWORD cell[MV_MAX * MV_MAX]; } mv_grid;
enum { MV_INV, MV_STASH, MV_CUBE };                                    // container numbers; the game's ItemLocation is 0 / 4 / 3 and is also the packet destination
static const BYTE mv_loc[3] = {0, 4, 3};
static void mv_layout(int which, int *left, int *right, int *top, int *bottom, int *cell, int *w, int *h) {
    static const int off[] = {0x1016F0, 0x1015E0, 0x1016D8};          // inventory, stash, cube
    BYTE *lay = g_hud_cl + off[which];
    *w = lay[0]; *h = lay[1]; *left = (int)*(DWORD *)(lay + 4); *right = (int)*(DWORD *)(lay + 8); *top = (int)*(DWORD *)(lay + 0xC); *bottom = (int)*(DWORD *)(lay + 0x10); *cell = lay[0x15];
    if (*w > MV_MAX) *w = MV_MAX;
    if (*h > MV_MAX) *h = MV_MAX;
}
// the first free spot of w x h cells, scanning column by column (as BH does)
static int mv_free(const mv_grid *g, int iw, int ih, int *ox, int *oy) {
    for (int x = 0; x + iw <= g->w; x++)
        for (int y = 0; y + ih <= g->h; y++) {
            int ok = 1;
            for (int a = 0; a < iw && ok; a++) for (int b = 0; b < ih; b++) if (g->cell[(y + b) * MV_MAX + (x + a)]) { ok = 0; break; }
            if (ok) { *ox = x; *oy = y; return 1; }
        }
    return 0;
}
static int mv_try_click(int shift, int ctrl) {
    static int logged;
#define MV_WHY(msg) do { if (g_logging && logged < 25) { logged++; logf_("item move: %s", msg); } } while (0)
    if (!g_item_move_cfg || !g_hud_cl || !p_idsend || !p_idtext || !p_player || g_mv_item || g_view_unit) return 0;
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x64)) return 0;
    if (p_idcursor && p_idcursor()) { MV_WHY("an item is on the cursor"); return 0; }
    int ui_inv = id_ui(1), ui_stash = id_ui(0x19), ui_cube = id_ui(0x1A);
    int mx = (int)*(DWORD *)(g_hud_cl + 0x11C950), my = (int)*(DWORD *)(g_hud_cl + 0x11C94C);
    int l[3], r[3], t[3], b[3], c[3], w[3], h[3];
    for (int i = 0; i < 3; i++) mv_layout(i, &l[i], &r[i], &t[i], &b[i], &c[i], &w[i], &h[i]);
    int source = -1, sx = 0, sy = 0;
    if ((ui_inv || ui_stash || ui_cube) && c[MV_INV] > 0 && mx >= l[MV_INV] && mx <= r[MV_INV] && my >= t[MV_INV] && my <= b[MV_INV]) source = MV_INV;
    else if (ui_cube && c[MV_CUBE] > 0 && mx >= l[MV_CUBE] && mx <= r[MV_CUBE] && my >= t[MV_CUBE] && my <= b[MV_CUBE]) source = MV_CUBE;   // the cube panel is drawn over the stash's place
    else if (ui_stash && c[MV_STASH] > 0 && mx >= l[MV_STASH] && mx <= r[MV_STASH] && my >= t[MV_STASH] && my <= b[MV_STASH]) source = MV_STASH;
    if (source < 0) return 0;                                             // not over one of the panels: leave the click alone
    sx = (mx - l[source]) / c[source]; sy = (my - t[source]) / c[source];
    DWORD inventory = *(DWORD *)(pl + 0x60);
    if (!inventory || !readable((void *)inventory, 0x2C)) return 0;
    mv_grid g[3]; memset(g, 0, sizeof g);
    for (int i = 0; i < 3; i++) { g[i].w = w[i]; g[i].h = h[i]; }
    DWORD found = 0; int fw = 0, fh = 0, guard = 0;
    for (DWORD it = *(DWORD *)(inventory + 0x0C); it && readable((void *)it, 0x30) && guard < 512; guard++) {
        DWORD data = *(DWORD *)(it + 0x14);
        if (!data || !readable((void *)data, 0x70)) break;
        BYTE loc = *(BYTE *)(data + 0x45); int which = loc == 0 ? MV_INV : loc == 4 ? MV_STASH : loc == 3 ? MV_CUBE : -1;
        BYTE *txt = (BYTE *)p_idtext(*(DWORD *)(it + 4));
        DWORD path = *(DWORD *)(it + 0x2C);
        if (which >= 0 && txt && readable(txt, 0x111) && path && readable((void *)path, 0x14)) {
            int px = (int)*(DWORD *)(path + 0x0C), py = (int)*(DWORD *)(path + 0x10), iw = txt[0x10F], ih = txt[0x110];
            for (int a = px; a < px + iw && a < MV_MAX; a++) for (int bb = py; bb < py + ih && bb < MV_MAX; bb++) g[which].cell[bb * MV_MAX + a] = *(DWORD *)(it + 0x0C);
            if (which == source && sx >= px && sx < px + iw && sy >= py && sy < py + ih && memcmp(txt + 0x80, "box", 3)) { found = *(DWORD *)(it + 0x0C); fw = iw; fh = ih; }   // never the cube itself
        }
        it = *(DWORD *)(data + 0x64);
    }
    if (!found) { MV_WHY("no item under the mouse"); return 1; }          // over a panel but no item: swallow like BH
    DWORD dest; int dx = 0, dy = 0, to = -1;
    if (ctrl) dest = 0xFF;                                                // the ground
    else if (shift) {                                                     // inventory -> stash (else cube); stash -> cube (else inventory); cube -> stash (else inventory)
        if (source == MV_INV) to = ui_stash ? MV_STASH : ui_cube ? MV_CUBE : -1;
        else if (source == MV_STASH) to = ui_cube ? MV_CUBE : MV_INV;
        else to = ui_stash ? MV_STASH : MV_INV;
        if (to < 0) { MV_WHY("nothing to do (neither the stash nor the cube is open)"); return 0; }
        dest = mv_loc[to];
        if (!mv_free(&g[to], fw, fh, &dx, &dy)) { MV_WHY("no room in the target"); return 1; }
    } else return 0;
    BYTE pk[5] = {0x19}; *(DWORD *)(pk + 1) = found; p_idsend(5, 1, pk);   // pick it up; the put-down follows one tick later
    g_mv_item = found; g_mv_x = (DWORD)dx; g_mv_y = (DWORD)dy; g_mv_dest = dest; g_mv_at = GetTickCount() + 30;
    MV_WHY(ctrl ? "dropping on the ground" : "moving the item");
    return 1;
}

static LRESULT CALLBACK id_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LBUTTONDOWN && g_view_unit && g_hud_cl && id_ui(1) && (int)*(DWORD *)(g_hud_cl + 0x11C950) >= 400) return 0;   // inspecting: the right half is not yours to click (BH)
    if (m == WM_LBUTTONDOWN && (GetKeyState(VK_SHIFT) & 0x8000)) { static int n; if (g_logging && n < 3) { n++; logf_("shift-click identify: shift + left click seen"); } if (id_try_click()) return 0; }   // swallowed: the game never sees the click
    if (m == WM_RBUTTONDOWN) {
        int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if ((shift || ctrl) && mv_try_click(shift, ctrl)) return 0;
    }
    return CallWindowProcA(g_old_wndproc, h, m, w, l);
}

static BOOL CALLBACK id_enum(HWND h, LPARAM l) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}

static DWORD WINAPI idclick_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL, cm = NULL, net = NULL;
    for (int i = 0; i < 6000 && !(cl && cm && net); i++) { cl = GetModuleHandleA("D2Client.dll"); cm = GetModuleHandleA("D2Common.dll"); net = GetModuleHandleA("D2Net.dll"); if (!(cl && cm && net)) Sleep(100); }
    if (!cl || !cm || !net) { logf_("shift-click identify: game modules not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("shift-click identify: this D2Client build is not supported (1.13d only); off"); return 0; }
    g_hud_cl = (BYTE *)cl;
    p_idsend = (id_send_t)GetProcAddress(net, (LPCSTR)10015); p_idtext = (void *(__stdcall *)(DWORD))GetProcAddress(cm, (LPCSTR)10994);
    p_idstat = (DWORD (__stdcall *)(DWORD, DWORD, DWORD))GetProcAddress(cm, (LPCSTR)10550); p_idcursor = (id_cursor_t)(g_hud_cl + 0x144A0);
    p_player = (mh_player_t)(g_hud_cl + 0x613C0);
    if (!p_idsend || !p_idtext || !p_idstat) { logf_("shift-click identify: game functions not found; off"); return 0; }
    HWND win = NULL;                                                 // the game's own window (D2gfx #10007 GetHwnd, as BH uses), else the first top-level window of the process
    HMODULE gx = NULL; for (int i = 0; i < 600 && !gx; i++) { gx = GetModuleHandleA("D2gfx.dll"); if (!gx) Sleep(100); }
    typedef HWND (__stdcall *gethwnd_t)(void); gethwnd_t gh = gx ? (gethwnd_t)GetProcAddress(gx, (LPCSTR)10007) : NULL;
    for (int i = 0; i < 1200 && !win; i++) { if (gh) win = gh(); if (!win) EnumWindows(id_enum, (LPARAM)&win); if (!win) Sleep(100); }
    if (!win) { logf_("shift-click identify: game window not found; off"); return 0; }
    g_old_wndproc = (WNDPROC)SetWindowLongA(win, GWL_WNDPROC, (LONG)id_wndproc);
    logf_("shift-click identify on (window hooked: %08lx, via %s)", (unsigned long)(DWORD)win, gh ? "D2gfx GetHwnd" : "enumeration");
    return 0;
}
#endif
