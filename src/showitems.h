// Always show items (ported from BH Item "Always Show Items", `PermShowItems` patches): ground item labels stay on without holding Alt; a key toggles them.
// The game draws the labels only while UI var 0xD (the Alt flag, D2Client +0x11C8C4) is set. Four reads of that flag are redirected to stubs that, while
// `g_show_items` is on, pretend it is set; with it off the stubs do exactly what the game's own code did. Switched by qol_ui.ini [features] always_show_items
// (the state at start) and [keys] show_items_key (toggle, 76 = L, 0 = no key).
#ifndef UI_SHOWITEMS_H
#define UI_SHOWITEMS_H

static volatile LONG g_show_items;                                  // 1 = labels always shown (read by the stubs on the game thread, flipped by keys_thread)
static int g_show_items_cfg = 0, g_key_show_items = 76, g_show_items_ok;

static BYTE *si_u32(BYTE *p, DWORD v) { memcpy(p, &v, 4); return p + 4; }

static DWORD WINAPI showitems_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL;
    for (int i = 0; i < 6000 && !cl; i++) { cl = GetModuleHandleA("D2Client.dll"); if (!cl) Sleep(100); }
    if (!cl) return 0;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) return 0;
    BYTE *base = (BYTE *)cl; DWORD flag = (DWORD)(base + 0x11C890 + 0x0D * 4);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; DWORD sz = sec->Misc.VirtualSize;
        long at[4]; int pt[4], bad = 0, found = 0;
        for (int w = 0; w < 4; w++) { at[w] = sig_find_showitems(code, sz, w, &pt[w]); if (at[w] >= 0) found++; }
        if (!found) continue;
        for (int w = 0; w < 4; w++) if (at[w] < 0 || pt[w] || (w != 2 && *(DWORD *)(code + at[w] + 2) != flag)) bad = 1;   // a site is missing, already patched, or reads another variable
        if (bad) { logf_("show items: a patch site did not match stock 1.13d (or is already patched); off"); return 0; }
        BYTE *st[4]; for (int w = 0; w < 4; w++) if (!(st[w] = stub_alloc())) return 0;
        DWORD fl = (DWORD)&g_show_items;
        static const BYTE force[] = {0x74, 0x03, 0x85, 0xE4, 0xC3};                  // je off ; test esp,esp (ZF = 0) ; ret
        for (int w = 0; w < 4; w++) {                                                // each stub starts: cmp dword [g_show_items],0
            BYTE *p = st[w]; *p++ = 0x83; *p++ = 0x3D; p = si_u32(p, fl); *p++ = 0x00;
            if (w < 3) {
                p = emit(p, force, 5);
                if (w < 2) { *p++ = 0x39; *p++ = 0x1D; p = si_u32(p, flag); *p++ = 0xC3; }       // cmp [flag],ebx ; ret
                else { static const BYTE o[] = {0x8B, 0x44, 0x24, 0x20, 0x85, 0xC0, 0xC3}; p = emit(p, o, 7); }   // mov eax,[esp+20h] ; test eax,eax ; ret
            } else {
                static const BYTE on[] = {0x74, 0x06, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xC3};           // je off ; mov ecx,1 ; ret
                p = emit(p, on, 8); *p++ = 0x8B; *p++ = 0x0D; p = si_u32(p, flag); *p++ = 0xC3;   // off: mov ecx,[flag] ; ret
            }
        }
        InterlockedExchange(&g_show_items, g_show_items_cfg ? 1 : 0);
        for (int w = 0; w < 4; w++) {
            BYTE *s = code + at[w], j[6]; j[0] = 0xE8; *(DWORD *)(j + 1) = (DWORD)st[w] - (DWORD)(s + 5); j[5] = 0x90;
            patch_bytes(s, j, 6);
        }
        g_show_items_ok = 1;
        logf_("show items: ground labels can stay on (D2Client +%lx +%lx +%lx +%lx), %s", (unsigned long)(code + at[0] - base), (unsigned long)(code + at[1] - base),
              (unsigned long)(code + at[2] - base), (unsigned long)(code + at[3] - base), g_show_items ? "on" : "off");
        return 0;
    }
    logf_("show items: the label code was not found in D2Client (or appears more than once); off");
    return 0;
}
#endif
