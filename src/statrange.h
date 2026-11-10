// Item stat ranges (ported from BH Modules/Item/Item.cpp: OnPropertyBuild, GetItemsTxtStatByMod, GetAllStatModifier): next to every magical property in an
// item's description the possible range is shown, e.g. "+25% Faster Cast Rate [20 - 30]". Ranges come from the game's own data tables in memory
// (UniqueItems, SetItems, Runes = runewords, and the magic prefix / suffix / automagic tables for magic, rare and crafted items).
// Hook: the call that builds one property line (D2Client +0x2E06D); our stub lets the game build the line, then `sr_hook` appends the range.
// BH also adds ranges to damage lines and a "Base Defense [min - max]" line: not ported yet.  Switched by qol_ui.ini [features] item_stat_ranges.
#ifndef UI_STATRANGE_H
#define UI_STATRANGE_H

typedef struct { DWORD prop, par; int mn, mx; } sr_stat;            // ItemsTxtStat: Properties.txt row, parameter, min, max
static int g_stat_ranges = 1, g_range_color = 10;
static BYTE *g_sr_cm;                                               // D2Common base
typedef BYTE *(__stdcall *sr_magic_t)(int);                         // D2Common #10174 GetItemMagicalMods (1-based, 0x90-byte records)
typedef BYTE *(__stdcall *sr_rune_t)(int);                          // D2Common #10405 GetRunesTxt (0x120-byte records)
typedef int *(__stdcall *sr_runes_n_t)(void);                       // D2Common #10981 GetRunesTxtRecords
typedef BOOL (__stdcall *sr_match_t)(DWORD item, int type);         // D2Common #10601 IsMatchingType
typedef int (__stdcall *sr_base_t)(DWORD unit, int stat, int layer);   // D2Common #10216 GetBaseStatSigned
static sr_magic_t p_sr_magic; static sr_rune_t p_sr_rune; static sr_runes_n_t p_sr_runes_n; static sr_match_t p_sr_match; static sr_base_t p_sr_base;

static DWORD sr_sgpt(void) { return g_sr_cm ? *(DWORD *)(g_sr_cm + 0xA33F0) : 0; }

// BH GetItemsTxtStatByMod: the entry of `stats` (n of them) that produces item stat `nstat` (with parameter `par`)
static sr_stat *sr_find(sr_stat *stats, int n, int nstat, int par, DWORD sgpt) {
    if (nstat == 196 || nstat == 198 || nstat == 195 || nstat == 197 || nstat == 199 || nstat == 201 || nstat == 204 ||   // skill on kill / hit / striking / death / level up / when struck, charged
        nstat == 54 || nstat == 50 || nstat == 48 || nstat == 57 || nstat == 52) return NULL;                            // cold / lightning / fire / poison / magic min damage (their lines are built elsewhere)
    DWORD props = *(DWORD *)(sgpt + 0xA4), nprops = *(DWORD *)(sgpt + 0xAC);
    for (int i = 0; i < n; i++) {
        if (stats[i].prop == 0xFFFFFFFFu || stats[i].prop >= nprops) break;
        BYTE *p = (BYTE *)props + (size_t)stats[i].prop * 0x2E;
        WORD stat0 = *(WORD *)(p + 0x20); BYTE func0 = p[0x18];
        if (stat0 == 0xFFFF && func0 == 7 && (nstat == 25 || nstat == 18 || nstat == 17 || nstat == 273 || nstat == 219)) return &stats[i];   // enhanced damage
        if (stat0 == 0xFFFF && func0 == 6 && (nstat == 22 || nstat == 24 || nstat == 272 || nstat == 218)) return &stats[i];                  // max damage
        if (stat0 == 0xFFFF && func0 == 5 && (nstat == 21 || nstat == 23)) return &stats[i];                                                  // min damage
        for (int j = 0; j < 7; j++) {
            WORD sj = *(WORD *)(p + 0x20 + 2 * j);
            if (sj == 0xFFFF) break;
            if (sj == nstat && stats[i].par == (DWORD)par) return &stats[i];
        }
    }
    return NULL;
}
// BH GetAllStatModifier: another entry of the same item that also feeds `nstat` (all resistances, all attributes ...)
static sr_stat *sr_all(sr_stat *stats, int n, int nstat, sr_stat *origin, DWORD sgpt) {
    DWORD props = *(DWORD *)(sgpt + 0xA4), nprops = *(DWORD *)(sgpt + 0xAC);
    for (int i = 0; i < n; i++) {
        if (stats[i].prop == 0xFFFFFFFFu) break;
        if (stats[i].prop == origin->prop) continue;
        if (stats[i].prop >= nprops) break;
        BYTE *p = (BYTE *)props + (size_t)stats[i].prop * 0x2E;
        for (int j = 0; j < 7; j++) { WORD sj = *(WORD *)(p + 0x20 + 2 * j); if (sj == 0xFFFF) break; if (sj == nstat) return &stats[i]; }
    }
    return NULL;
}

// the range for one property line; returns 1 and fills mn/mx when there is a range to show
static int sr_range(int nstat, DWORD item, int par, int *mn, int *mx) {
    DWORD sgpt = sr_sgpt(); if (!sgpt || !readable((void *)sgpt, 0xC30)) return 0;
    DWORD data = *(DWORD *)(item + 0x14); if (!data || !readable((void *)data, 0x40)) return 0;
    DWORD quality = *(DWORD *)data, flags = *(DWORD *)(data + 0x18), fileindex = *(DWORD *)(data + 0x28);
    sr_stat *stat = NULL, *allst = NULL; int lo = 0, hi = 0, have = 0;
    if (quality == 5 || quality == 7) {                              // set / unique
        BYTE *rec = NULL; sr_stat *list = NULL; int n = 0;
        if (quality == 5) { if (fileindex < *(DWORD *)(sgpt + 0xC1C)) { rec = (BYTE *)*(DWORD *)(sgpt + 0xC18) + (size_t)fileindex * 0x1B8; list = (sr_stat *)(rec + 0x88); n = 19; } }
        else              { if (fileindex < *(DWORD *)(sgpt + 0xC28)) { rec = (BYTE *)*(DWORD *)(sgpt + 0xC24) + (size_t)fileindex * 0x14C; list = (sr_stat *)(rec + 0x8C); n = 12; } }
        if (!rec || !readable(rec, quality == 5 ? 0x1B8 : 0x14C)) return 0;
        stat = sr_find(list, n, nstat, par, sgpt);
        if (stat) allst = sr_all(list, n, nstat, stat, sgpt);
        if (stat) { lo = stat->mn; hi = stat->mx; if (allst) { lo += allst->mn; hi += allst->mx; } have = lo < hi; }
    } else if (flags & 0x4000000u) {                                 // runeword
        int rwid = *(WORD *)(data + 0x38), cnt = p_sr_runes_n ? *p_sr_runes_n() : 0; BYTE *rec = NULL;
        for (int i = 1; i < cnt && p_sr_rune; i++) { BYTE *r = p_sr_rune(i); if (!r || !readable(r, 0xB4)) break; DWORD id; memcpy(&id, r + 0x82, 4); if ((int)id == rwid) { rec = r; break; } }
        if (!rec || !readable(rec, 0x120)) return 0;
        sr_stat *list = (sr_stat *)(rec + 0xB0);
        stat = sr_find(list, 7, nstat, par, sgpt);
        if (stat) { allst = sr_all(list, 7, nstat, stat, sgpt); lo = stat->mn; hi = stat->mx; if (allst) { lo += allst->mn; hi += allst->mx; } have = stat->mn != stat->mx; }
    } else if (quality == 4 || quality == 6 || quality == 8) {       // magic / rare / crafted: the lowest and highest value over every affix that could have rolled this stat
        BYTE *first = p_sr_magic ? p_sr_magic(1) : NULL, *automagic = *(BYTE **)(g_sr_cm + 0xA4CE4);
        if (!first || !automagic) return 0;
        int nAffixes = (int)((automagic - first) / 0x90);
        int mn_ = 0, mx_ = 0; int itype = 0; (void)itype;
        WORD autoprefix = *(WORD *)(data + 0x36);
        for (int i = 1; i < 4096; i++) {
            if (!autoprefix && i > nAffixes) break;                  // no automagic affixes for this item
            BYTE *t = p_sr_magic(i); if (!t || !readable(t, 0x90)) break;
            WORD version = *(WORD *)(t + 0x22);
            if (version == 0) continue;                              // expansion character: skip non-expansion affixes
            if (*(DWORD *)(t + 0x58) > 99) continue;                 // level > 99
            if (quality < 8 && !*(WORD *)(t + 0x54)) continue;       // not spawnable
            if (quality >= 6 && !t[0x64]) continue;                  // not for rares
            int excluded = 0, included = 0;
            for (int j = 0; j < 5; j++) { WORD e = *(WORD *)(t + 0x78 + 2 * j); if (!e || e == 0xFFFF) break; if (p_sr_match(item, e)) { excluded = 1; break; } }
            if (excluded) continue;
            for (int j = 0; j < 7; j++) { WORD it_ = *(WORD *)(t + 0x6A + 2 * j); if (!it_ || it_ == 0xFFFF) break; if (p_sr_match(item, it_)) { included = 1; break; } }
            if (!included) continue;
            sr_stat *s = sr_find((sr_stat *)(t + 0x24), 3, nstat, par, sgpt);
            if (!s) continue;
            mn_ = mn_ == 0 ? s->mn : (s->mn < mn_ ? s->mn : mn_);
            mx_ = s->mx > mx_ ? s->mx : mx_;
        }
        lo = mn_; hi = mx_; have = lo < hi;
    }
    if (!have) return 0;
    if ((nstat == 216 || nstat == 217 || nstat == 219 || nstat == 218) && p_player && p_sr_base) {    // per-level stats: show the value at the player's level
        DWORD pl = p_player(); int lv = pl ? p_sr_base(pl, 12, 0) : 0; lo = (lv * lo) >> 3; hi = (lv * hi) >> 3;
    }
    *mn = lo; *mx = hi;
    return 1;
}

// A small cache: a tooltip rebuilds its text every frame, the ranges never change for an item.
typedef struct { DWORD uid, quality, flags; int nstat, par, state, mn, mx; } sr_c;   // state 0 empty, 1 no range, 2 range
static sr_c g_src[64];

// Called after the game has built one property line in `wout` (a 128-character buffer).
static void __cdecl sr_hook(int nstat, wchar_t *wout, DWORD item, int par) {
    if (!wout || !item || !readable((void *)item, 0x40) || *(DWORD *)item != 4) return;
    if (!master_on) return;
    if (!g_stat_ranges && !(GetKeyState(VK_CONTROL) & 0x8000)) return;   // item_stat_ranges=0: only while Ctrl is held (BH)
    DWORD data = *(DWORD *)(item + 0x14); if (!data || !readable((void *)data, 0x40)) return;
    DWORD uid = *(DWORD *)(item + 0x0C), quality = *(DWORD *)data, flags = *(DWORD *)(data + 0x18) & 0x4000000u;
    sr_c *c = &g_src[(uid ^ ((DWORD)nstat * 31u) ^ ((DWORD)par * 7u)) & 63];
    if (!(c->state && c->uid == uid && c->nstat == nstat && c->par == par && c->quality == quality && c->flags == flags)) {
        int mn = 0, mx = 0, has = sr_range(nstat, item, par, &mn, &mx);
        c->uid = uid; c->nstat = nstat; c->par = par; c->quality = quality; c->flags = flags; c->state = has ? 2 : 1; c->mn = mn; c->mx = mx;
    }
    if (c->state != 2) return;
    size_t len = wcslen(wout); if (len >= 127) return;
    swprintf(wout + len, 128 - len, L" \377c%c[%d - %d]\377c3", (wchar_t)('0' + g_range_color), c->mn, c->mx);   // BH: dark green range, then back to blue
}

static DWORD WINAPI statrange_thread(LPVOID arg) {
    (void)arg;
    HMODULE cl = NULL, cm = NULL;
    for (int i = 0; i < 6000 && !(cl && cm); i++) { cl = GetModuleHandleA("D2Client.dll"); cm = GetModuleHandleA("D2Common.dll"); if (!(cl && cm)) Sleep(100); }
    if (!cl || !cm) { logf_("stat ranges: game modules not loaded; off"); return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) { logf_("stat ranges: this D2Client build is not supported (1.13d only); off"); return 0; }
    g_sr_cm = (BYTE *)cm;
    p_sr_magic = (sr_magic_t)GetProcAddress(cm, (LPCSTR)10174); p_sr_rune = (sr_rune_t)GetProcAddress(cm, (LPCSTR)10405); p_sr_runes_n = (sr_runes_n_t)GetProcAddress(cm, (LPCSTR)10981);
    p_sr_match = (sr_match_t)GetProcAddress(cm, (LPCSTR)10601); p_sr_base = (sr_base_t)GetProcAddress(cm, (LPCSTR)10216);
    if (!p_sr_magic || !p_sr_rune || !p_sr_runes_n || !p_sr_match || !p_sr_base) { logf_("stat ranges: D2Common functions not found; off"); return 0; }
    p_player = (mh_player_t)((BYTE *)cl + 0x613C0);
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress;
        static const short pat[] = {0x8B,0x84,0x24,0xD0,0x12,0x00,0x00, 0x56, 0x57, 0x8B,0x7C,0x24,0x18, 0x57, 0x50, 0x8D,0x74,0x24,0x3C, 0x8B,0xC3, 0xE8,-1,-1,-1,-1, 0x85,0xC0, 0x0F,0x84};
        int c = 0; long at = qt_find(code, sec->Misc.VirtualSize, 0, sec->Misc.VirtualSize, pat, (int)(sizeof pat / sizeof *pat), &c);
        if (c != 1) continue;
        BYTE *site = code + at + 21; DWORD orig = (DWORD)(site + 5) + *(DWORD *)(site + 1);
        if (orig < (DWORD)base || orig >= (DWORD)base + 0xCE000) { logf_("stat ranges: hook already installed"); return 0; }
        BYTE *st = stub_alloc(); if (!st) return 0;
        // stub (see the comments): save the return address and the arguments we need, call the game's own function, then ours
        DWORD ret_save = (DWORD)(st + 96), nst = ret_save + 4, wo = ret_save + 8, itm = ret_save + 12, prm = ret_save + 16;
        BYTE *p = st;
        #define EMIT(...) do { static const BYTE b_[] = {__VA_ARGS__}; p = emit(p, b_, (int)sizeof b_); } while (0)
        #define EMIT_ADDR(op0, op1, addr) do { BYTE b_[6] = {op0, op1}; *(DWORD *)(b_ + 2) = (addr); p = emit(p, b_, 6); } while (0)
        EMIT_ADDR(0x8F, 0x05, ret_save);                              // pop dword [ret_save]
        EMIT_ADDR(0x89, 0x05, nst);                                   // mov [nst],eax        (the stat number)
        EMIT_ADDR(0x89, 0x35, wo);                                    // mov [wo],esi         (the line buffer)
        EMIT(0x8B, 0x0C, 0x24);       EMIT_ADDR(0x89, 0x0D, itm);     // mov ecx,[esp] ; mov [itm],ecx      (the item)
        EMIT(0x8B, 0x4C, 0x24, 0x08); EMIT_ADDR(0x89, 0x0D, prm);     // mov ecx,[esp+8] ; mov [prm],ecx    (the parameter)
        p = emit_rel(p, 0xE8, orig);                                  // call the game's function (it removes its own arguments)
        EMIT_ADDR(0xFF, 0x35, ret_save);                              // push dword [ret_save]
        EMIT(0x85, 0xC0);                                             // test eax,eax
        BYTE *jz = p; EMIT(0x74, 0x00);                               // jz done
        EMIT(0x50, 0x51, 0x52);                                       // push eax ; push ecx ; push edx
        EMIT_ADDR(0xFF, 0x35, prm); EMIT_ADDR(0xFF, 0x35, itm); EMIT_ADDR(0xFF, 0x35, wo); EMIT_ADDR(0xFF, 0x35, nst);
        p = emit_rel(p, 0xE8, (DWORD)sr_hook);
        EMIT(0x83, 0xC4, 0x10, 0x5A, 0x59, 0x58);                     // add esp,16 ; pop edx ; pop ecx ; pop eax
        jz[1] = (BYTE)(p - (jz + 2));
        EMIT(0xC3);                                                   // done: ret
        BYTE j[5]; j[0] = 0xE8; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5);
        patch_bytes(site, j, 5);
        logf_("stat ranges on (D2Client +%lx, %s)", (unsigned long)(site - base), g_stat_ranges ? "always shown" : "shown while Ctrl is held");
        return 0;
    }
    logf_("stat ranges: the item-property code was not found in D2Client (or appears more than once); off");
    return 0;
}
#endif
