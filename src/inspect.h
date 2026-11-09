// Player inspection (ported from BH Item::OnKey / OnLoop / GetViewUnit and its three "view inventory" patches): press the key (default I) while the mouse is on
// another player, or on your Iron Golem / Valkyrie / Shadow Master, and the inventory panel shows THAT unit's gear. Closing the inventory (or the unit going
// away) returns to your own. In single player this reaches your own summons; with other players in the game it shows theirs.
// Three game spots load "the player unit" into a register while the inventory is drawn; each is redirected to a stub that asks `iv_get` for the unit to show.
// Switched by qol_ui.ini [keys] inspect_key (0 = off).
#ifndef UI_INSPECT_H
#define UI_INSPECT_H

typedef DWORD (__stdcall *iv_sel_t)(void);                          // D2Client +0x17280 GetSelectedUnit
typedef DWORD (__fastcall *iv_find_t)(DWORD id, DWORD type);        // D2Client +0x620D0 FindServerSideUnit
typedef DWORD (__fastcall *iv_setui_t)(DWORD var, DWORD how, DWORD unk);   // D2Client +0x1C190 SetUIVar
static iv_sel_t p_ivsel; static iv_find_t p_ivfind; static iv_setui_t p_ivui;
static DWORD g_iv_req;                                              // tick count of a key press waiting to be handled on the game thread (0 = none)
static int g_inspect_ok;                                            // the hooks are installed

// What the inventory panel should show: you, or the inspected unit (with its name over the panel).
static DWORD __cdecl iv_get(void) {
    DWORD pl = p_player ? p_player() : 0;
    if (!g_view_unit || !pl || !readable((void *)g_view_unit, 0x30) || !readable((void *)pl, 0x30)) return pl;
    DWORD v = g_view_unit;
    if (*(DWORD *)(v + 0x0C) == *(DWORD *)(pl + 0x0C)) return pl;
    wchar_t name[40]; name[0] = 0;
    if (*(DWORD *)v == 0) {                                           // a player: the name is in its player data
        DWORD pd = *(DWORD *)(v + 0x14);
        if (pd && readable((void *)pd, 16)) { char a[17]; memcpy(a, (void *)pd, 16); a[16] = 0; MultiByteToWideChar(CP_ACP, 0, a, -1, name, 39); }
    } else { DWORD no = *(DWORD *)(v + 4); wcscpy(name, no == 291 ? L"Iron Golem" : no == 357 ? L"Valkyrie" : no == 418 ? L"Shadow Master" : L"?"); }
    if (name[0] && p_hdraw && p_hsize && p_hwidth && g_hud_cl) {
        int px = *(int *)(g_hud_cl + 0x11D354);                       // the panel offset: where the inventory sits on screen
        DWORD old = p_hsize(0), wd = 0, fn = 0; p_hwidth(name, &wd, &fn);
        p_hdraw(name, px + 160 + 320 - (int)wd / 2, 300 + 10, 0, 0); p_hsize(old);
    }
    return v;
}

// Game thread, from id_tick: handle a key press and keep the inspection state sane (BH Item::OnKey / OnLoop).
static void iv_tick(void) {
    if (!g_inspect_ok || !g_hud_cl) return;
    if (g_view_unit && !id_ui(1)) g_view_unit = 0;                    // the inventory was closed
    if (g_view_unit) {
        DWORD v = g_view_unit;
        if (!readable((void *)v, 0x64) || !*(DWORD *)(v + 0x60) || !p_ivfind || !p_ivfind(*(DWORD *)(v + 0x0C), *(DWORD *)v)) { g_view_unit = 0; if (p_ivui) p_ivui(1, 1, 0); }   // gone: close the panel
    }
    if (g_iv_req && (int)(GetTickCount() - g_iv_req) >= 120) {        // give the game time to handle the same key press (the inventory key) first
        g_iv_req = 0;
        DWORD u = p_ivsel ? p_ivsel() : 0;
        if (u && readable((void *)u, 0x30)) {
            DWORD mode = *(DWORD *)(u + 0x10), type = *(DWORD *)u, no = *(DWORD *)(u + 4);
            if (mode != 0 && mode != 17 && (type == 0 || no == 291 || no == 357 || no == 418)) {     // alive: a player, Iron Golem, Valkyrie or Shadow Master
                g_view_unit = u;
                if (!id_ui(1) && p_ivui) p_ivui(1, 0, 0);              // open the inventory if the key did not
            }
        }
    }
}

static DWORD WINAPI inspect_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL;
    for (int i = 0; i < 6000 && !cl; i++) { cl = GetModuleHandleA("D2Client.dll"); if (!cl) Sleep(100); }
    if (!cl) return 0;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) return 0;
    BYTE *base = (BYTE *)cl;
    p_ivsel = (iv_sel_t)(base + 0x17280); p_ivfind = (iv_find_t)(base + 0x620D0); p_ivui = (iv_setui_t)(base + 0x1C190);
    p_player = (mh_player_t)(base + 0x613C0); g_hud_cl = base;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; DWORD sz = sec->Misc.VirtualSize;
        static const short p1[] = {0x8B,0x35,-1,-1,-1,-1, 0x8B,0x7E,0x60, 0x50,0x53,0x56, 0x89,0x74,0x24,0x34, 0x89,0x7C,0x24,0x30, 0xE8};
        static const short p2[] = {0x83,0xEC,0x08, 0x33,0xC0, 0x3B,0xC8, 0x53, 0x0F,0x85,-1,-1,-1,-1, 0x8B,0x1D,-1,-1,-1,-1, 0x3B,0xD8, 0x0F,0x84,-1,-1,-1,-1, 0x83,0x7B,0x10,0x11, 0x0F,0x84};
        static const short p3[] = {0x85,0xC0, 0x8B,0x4F,0x60, 0x75,0x1C, 0x85,0xC9, 0x74,0x18, 0xA1,-1,-1,-1,-1, 0x85,0xC0, 0x74,0x14, 0x50,0x51, 0xE8};
        int c1 = 0, c2 = 0, c3 = 0;
        long a1 = qt_find(code, sz, 0, sz, p1, (int)(sizeof p1 / sizeof *p1), &c1);
        long a2 = qt_find(code, sz, 0, sz, p2, (int)(sizeof p2 / sizeof *p2), &c2);
        long a3 = qt_find(code, sz, 0, sz, p3, (int)(sizeof p3 / sizeof *p3), &c3);
        if (c1 != 1 || c2 != 1 || c3 != 1) continue;
        BYTE *s1 = code + a1, *s2 = code + a2 + 14, *s3 = code + a3;      // s1: mov esi,[player] (6 bytes)  s2: mov ebx,[player] (6)  s3: test eax,eax ; mov ecx,[edi+60h] (5)
        if (s1[0] != 0x8B || s2[0] != 0x8B || s3[0] != 0x85) { logf_("inspect: already patched or changed; off"); return 0; }
        BYTE *st1 = stub_alloc(), *st2 = stub_alloc(), *st3 = stub_alloc(); if (!st1 || !st2 || !st3) return 0;
        { static const BYTE a[] = {0x50, 0x51, 0x52}; static const BYTE b[] = {0x8B, 0xF0, 0x5A, 0x59, 0x58, 0xC3};            // push eax,ecx,edx ; call iv_get ; mov esi,eax ; pop edx,ecx,eax ; ret
          BYTE *p = emit(st1, a, 3); p = emit_rel(p, 0xE8, (DWORD)iv_get); emit(p, b, sizeof b); }
        { static const BYTE a[] = {0x50, 0x51, 0x52}; static const BYTE b[] = {0x8B, 0xD8, 0x5A, 0x59, 0x58, 0xC3};            // ... mov ebx,eax ...
          BYTE *p = emit(st2, a, 3); p = emit_rel(p, 0xE8, (DWORD)iv_get); emit(p, b, sizeof b); }
        { static const BYTE a[] = {0x50, 0x53, 0x51, 0x52};                                                                      // push eax,ebx,ecx,edx
          static const BYTE b[] = {0x8B, 0x1F, 0x83, 0xFB, 0x01, 0x74, 0x02, 0x8B, 0xF8, 0x5A, 0x59, 0x5B, 0x58, 0x85, 0xC0, 0x8B, 0x4F, 0x60, 0xC3};   // mov ebx,[edi] ; cmp ebx,1 ; je keep ; mov edi,eax ; pops ; test eax,eax ; mov ecx,[edi+60h] ; ret
          BYTE *p = emit(st3, a, 4); p = emit_rel(p, 0xE8, (DWORD)iv_get); emit(p, b, sizeof b); }
        BYTE j6[6]; j6[0] = 0xE8; j6[5] = 0x90; *(DWORD *)(j6 + 1) = (DWORD)st1 - (DWORD)(s1 + 5); patch_bytes(s1, j6, 6);
        *(DWORD *)(j6 + 1) = (DWORD)st2 - (DWORD)(s2 + 5); patch_bytes(s2, j6, 6);
        BYTE j5[5]; j5[0] = 0xE8; *(DWORD *)(j5 + 1) = (DWORD)st3 - (DWORD)(s3 + 5); patch_bytes(s3, j5, 5);
        g_inspect_ok = 1;
        logf_("inspect: the inventory can show another unit (D2Client +%lx, +%lx, +%lx)", (unsigned long)(s1 - base), (unsigned long)(s2 - base), (unsigned long)(s3 - base));
        return 0;
    }
    logf_("inspect: the inventory-draw code was not found in D2Client (or appears more than once); off");
    return 0;
}
#endif
