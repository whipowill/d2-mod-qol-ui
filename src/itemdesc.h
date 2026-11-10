// Item descriptions: the {...} text of the ItemDisplay rules is added to the end of the item's description box (BH's Item::OnProperties, a jump at
// D2Client +0x2E3FC). The hovered item is the game's own "selected inventory item" (D2Client +0x11CB28, BH's SelectedInvItem). Stock 1.13d only.
//   [features] item_descriptions
#ifndef UI_ITEMDESC_H
#define UI_ITEMDESC_H

static int g_item_desc = 1;
static BYTE *g_dd_cl;
typedef struct { DWORD uid, gen, tick; int len; wchar_t text[256]; } dd_cache;
static dd_cache g_dd[16];
#define DD_TTL 2000u
#define DD_BUF 1024                                                  // the game's description buffer holds 0x400 wide characters

// Called at the end of the description text builder: `wtxt` is the whole description text so far (a wide string, up to DD_BUF characters).
static void __cdecl dd_hook(wchar_t *wtxt) {
    if (!g_item_desc || !g_filter_on || !g_fi || !g_dd_cl || !wtxt || !readable(wtxt, 4)) return;
    DWORD item = *(DWORD *)(g_dd_cl + 0x11CB28);                     // the item under the mouse
    if (!item || !readable((void *)item, 0x30) || *(DWORD *)item != 4) return;
    DWORD uid = *(DWORD *)(item + 0x0C), now = GetTickCount();
    dd_cache *c = &g_dd[(uid ^ (uid >> 5)) & 15];
    if (!(c->uid == uid && c->gen == (DWORD)g_name_gen && now - c->tick < DD_TTL)) {   // the rules are only evaluated again after a while (the box is redrawn every frame)
        c->uid = uid; c->gen = (DWORD)g_name_gen; c->tick = now; c->len = 0;
        fi_ctx cx; fi_item it; char base[128];
        if (!mh_fill_item(&it, &cx, item, base, sizeof base)) return;
        char out[640]; fi_set_video(g_fi, mh_video_glide());
        if (fi_desc(g_fi, &it, base, out, sizeof out) <= 0) return;
        int wl = MultiByteToWideChar(CP_ACP, 0, out, -1, c->text, 255);
        if (wl <= 0) return;
        c->text[255] = 0; c->len = (int)wcslen(c->text);
    }
    if (c->len <= 0) return;
    size_t n = 0; while (n < DD_BUF && wtxt[n]) n++;
    if (n + (size_t)c->len + 3 >= DD_BUF) return;                    // does not fit: leave the box alone
    if (n > 0 && wtxt[n - 1] != L'\n') wtxt[n++] = L'\n';
    memcpy(wtxt + n, c->text, (size_t)c->len * sizeof(wchar_t)); n += (size_t)c->len;
    wtxt[n++] = L'\n'; wtxt[n] = 0;                                  // BH ends the line too
}

static DWORD WINAPI itemdesc_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL;
    for (int i = 0; i < 6000 && !cl; i++) { cl = GetModuleHandleA("D2Client.dll"); if (!cl) Sleep(100); }
    if (!cl) { logf_("item descriptions: D2Client not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("item descriptions: this D2Client build is not supported (1.13d only); off"); return 0; }
    g_dd_cl = (BYTE *)cl;
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress; int patched = 0;
        long at = sig_find_itemdesc(code, sec->Misc.VirtualSize, &patched);
        if (at < 0) continue;
        if (patched) { logf_("item descriptions: hook already installed"); return 0; }
        BYTE *site = code + at, *st = stub_alloc(); if (!st) return 0;
        static const BYTE a[] = {0x50};                              // push eax            (the description text)
        static const BYTE b[] = {0x83, 0xC4, 0x04,                   // add esp,4
                                 0x81, 0xC4, 0x08, 0x08, 0x00, 0x00, // add esp,808h        (what the game's own code did here)
                                 0xC2, 0x0C, 0x00};                  // ret 0Ch
        BYTE *p = emit(st, a, 1);
        p = emit_rel(p, 0xE8, (DWORD)dd_hook);
        p = emit(p, b, sizeof b);
        BYTE j[6]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5); j[5] = 0x90;
        patch_bytes(site, j, 6);
        logf_("item descriptions on (D2Client +%lx, item_descriptions=%d)", (unsigned long)(site - base), g_item_desc);
        return 0;
    }
    logf_("item descriptions: the description code was not found in D2Client (or appears more than once); off");
    return 0;
}
#endif
