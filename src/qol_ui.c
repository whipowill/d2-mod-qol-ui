// qol_ui.dll - Diablo II LoD 1.13d single-player interface features (a companion to qol.dll, loaded the same way).
// Everything is configured in qol_ui.ini and the loot filter rules in qol_ui_filter.cfg (both next to the game).
// Nothing here can be changed in game.
//   - loot filter: item names on the ground and in tooltips follow ItemDisplay rules (the BH syntax)
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "filter.h"
#include "itemdb.h"
#include "sig.h"

#define D2CLIENT_TIMESTAMP_113D 1318970890u

static FILE *g_log;
static int g_logging = 0, g_filter_on = 1, g_filter_level = 1, g_ping_level = 5, g_hide_blocked = 1;
static volatile LONG g_lines;
static int g_exit_lines = 1, g_shift_id = 1, g_show_gold = 0, g_item_move_cfg = 1, g_remember_filter = 1;
#define MAX_LINES 20000

static void logf_(const char *fmt, ...) {
    if (!g_log || InterlockedIncrement(&g_lines) > MAX_LINES) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); fputc('\n', g_log);
    va_end(ap);                                      // flushed off-thread by log_flusher (disk I/O on the game thread spikes)
}
static DWORD WINAPI log_flusher(LPVOID arg) { (void)arg; for (;;) { Sleep(1000); if (g_log) fflush(g_log); } return 0; }
static int readable(const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const char *)p + n <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

// For loops over many game structures on the game thread: remembers the last few memory regions found readable, so most checks cost nothing. Call
// readable_reset() at the start of each loop (the cache is only trusted within one pass, in which the game cannot free anything under us).
static struct { const char *lo, *hi; } g_rc[6]; static int g_rc_n;
static void readable_reset(void) { g_rc_n = 0; }
static int readable_c(const void *p, size_t n) {
    const char *a = (const char *)p;
    for (int i = 0; i < g_rc_n; i++) if (a >= g_rc[i].lo && a + n <= g_rc[i].hi) return 1;
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    const char *hi = (const char *)mbi.BaseAddress + mbi.RegionSize;
    if (a + n > hi) return 0;
    if (g_rc_n < 6) { g_rc[g_rc_n].lo = (const char *)mbi.BaseAddress; g_rc[g_rc_n].hi = hi; g_rc_n++; }
    return 1;
}

// ---- reading the game's data tables through Storm (same method as qol.dll) --------------------------------------------
static int storm_is(HMODULE st, unsigned ord, const unsigned char *sig, int n, FARPROC *out) {
    FARPROC f = GetProcAddress(st, (LPCSTR)(UINT_PTR)ord);
    if (!f || !readable(f, (size_t)n) || memcmp((void *)f, sig, (size_t)n)) return 0;
    *out = f; return 1;
}
static DWORD storm_scope(void) {                     // -direct: the loose Data folder first, then the archives
    const char *cl = GetCommandLineA(); char low[1024]; int i = 0;
    if (!cl) return 0;
    for (; cl[i] && i < (int)sizeof low - 1; i++) low[i] = (cl[i] >= 'A' && cl[i] <= 'Z') ? (char)(cl[i] + 32) : cl[i];
    low[i] = 0;
    return strstr(low, "-direct") ? 3 : 0;
}
static int storm_read(const char *name, int (*parse)(const char *, int)) {
    HMODULE st = GetModuleHandleA("Storm.dll");
    if (!st) return -1;
    static const unsigned char s_open[] = {0x8B, 0x4C, 0x24, 0x10, 0x81, 0xEC, 0x10, 0x01};
    static const unsigned char s_size[] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};
    static const unsigned char s_read[] = {0x8B, 0x44, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x10};
    static const unsigned char s_close[] = {0x57};
    FARPROC fo, fs, fr, fc;
    if (!storm_is(st, 268, s_open, sizeof s_open, &fo) || !storm_is(st, 265, s_size, sizeof s_size, &fs) ||
        !storm_is(st, 269, s_read, sizeof s_read, &fr) || !storm_is(st, 253, s_close, sizeof s_close, &fc)) { logf_("Storm.dll not recognised"); return -2; }
    HANDLE h = NULL; DWORD scope = storm_scope();
    if (!((int (__stdcall *)(HANDLE, const char *, DWORD, HANDLE *))fo)(NULL, name, scope, &h) || !h) return -1;
    DWORD size = ((DWORD (__stdcall *)(HANDLE, DWORD *))fs)(h, NULL), got = 0; int n = -1;
    if (size > 0 && size < (16u << 20)) {
        char *buf = HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (buf) {
            if (((int (__stdcall *)(HANDLE, void *, DWORD, DWORD *, void *))fr)(h, buf, size, &got, NULL) && got == size) n = parse(buf, (int)got);
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    ((int (__stdcall *)(HANDLE))fc)(h);
    if (n > 0) logf_("%s read through Storm", name);
    return n;
}

// ---- in-memory patching helpers ----------------------------------------------------------------------------------------
static void patch_bytes(BYTE *at, const BYTE *src, int n) {
    DWORD old;
    if (!VirtualProtect(at, (SIZE_T)n, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(at, src, (size_t)n);
    VirtualProtect(at, (SIZE_T)n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, (SIZE_T)n);
}
static BYTE *g_stub_mem; static volatile LONG g_stub_used;           // allocated once in worker() before any thread that patches code starts
static BYTE *stub_alloc(void) {                                        // thread-safe: the feature threads patch the game at the same time
    if (!g_stub_mem) return NULL;
    LONG at = InterlockedExchangeAdd(&g_stub_used, 128);
    return at + 128 <= 8192 ? g_stub_mem + at : NULL;
}
static BYTE *emit(BYTE *p, const void *src, int n) { memcpy(p, src, (size_t)n); return p + n; }
static BYTE *emit_rel(BYTE *p, BYTE op, DWORD target) { *p++ = op; *(DWORD *)p = target - (DWORD)(p + 4); return p + 4; }

static wchar_t g_msg[96]; static volatile LONG g_msg_ready;      // a chat line queued by the key thread, printed on the game thread
#include "lootfilter.h"
#include "automap.h"
#include "hud.h"
#include "idclick.h"
#include "statrange.h"
#include "keys.h"


// ---- crash report: a vectored handler writes where an access violation happened (module + offset, and the callers on the stack) to the log ----
static void crash_where(DWORD addr, char *out, size_t cap) {
    HMODULE m = NULL; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m) {
        GetModuleFileNameA(m, name, sizeof name);
        const char *b = strrchr(name, '\\'); if (b) memmove(name, b + 1, strlen(b));
        snprintf(out, cap, "%s+%lx", name, (unsigned long)(addr - (DWORD)m));
    } else snprintf(out, cap, "%08lx", (unsigned long)addr);
}
static LONG CALLBACK crash_handler(EXCEPTION_POINTERS *ep) {
    static volatile LONG n;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION) || InterlockedIncrement(&n) > 6 || !g_log) return EXCEPTION_CONTINUE_SEARCH;
    char w[160]; CONTEXT *c = ep->ContextRecord;
    crash_where((DWORD)ep->ExceptionRecord->ExceptionAddress, w, sizeof w);
    fprintf(g_log, "EXCEPTION %08lx at %s, address touched %08lx; eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", (unsigned long)code, w,
            (unsigned long)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0), (unsigned long)c->Eax, (unsigned long)c->Ebx, (unsigned long)c->Ecx, (unsigned long)c->Edx, (unsigned long)c->Esi, (unsigned long)c->Edi, (unsigned long)c->Ebp, (unsigned long)c->Esp);
    DWORD *sp = (DWORD *)c->Esp;
    if (readable(sp, 64 * 4)) for (int i = 0; i < 64; i++) {                   // return addresses that point into a module's code
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void *)sp[i], &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) && mbi.Type == MEM_IMAGE) {
            crash_where(sp[i], w, sizeof w); fprintf(g_log, "  stack[%d] %s\n", i, w);
        }
    }
    fflush(g_log);
    return EXCEPTION_CONTINUE_SEARCH;
}

static int master_on = 1;
static void load_config(void) {
    char path[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, path); lstrcatA(path, "\\qol_ui.ini");
    int master = GetPrivateProfileIntA("qol_ui", "enabled", 1, path);
    master_on = master;
    g_logging = GetPrivateProfileIntA("qol_ui", "logging", 0, path);
    g_filter_on = master && GetPrivateProfileIntA("lootfilter", "enabled", 1, path);
    g_filter_level = GetPrivateProfileIntA("lootfilter", "filter_level", 1, path);
    g_ping_level = GetPrivateProfileIntA("lootfilter", "ping_level", 5, path);
    g_hide_blocked = GetPrivateProfileIntA("lootfilter", "hide_blocked", 1, path);
    g_show_gold = GetPrivateProfileIntA("lootfilter", "show_gold_amount", 0, path);
    g_dots_on = GetPrivateProfileIntA("lootfilter", "map_dots", 1, path);
    g_alerts = GetPrivateProfileIntA("lootfilter", "alerts", 1, path);
    g_alert_close = GetPrivateProfileIntA("lootfilter", "alert_close", 1, path);
    g_exit_lines = master && GetPrivateProfileIntA("automap", "exit_lines", 1, path);
    g_clock = master && GetPrivateProfileIntA("display", "clock", 1, path);
    g_clock_24h = GetPrivateProfileIntA("display", "clock_24h", 0, path);
    g_clock_color = GetPrivateProfileIntA("display", "clock_color", 4, path);
    g_buffs = master && GetPrivateProfileIntA("display", "buffs", 1, path);
    g_bbottom = GetPrivateProfileIntA("display", "buff_bottom", 5, path); if (g_bbottom < -49) g_bbottom = -49; if (g_bbottom > 2000) g_bbottom = 2000;
    g_bside = GetPrivateProfileIntA("display", "buff_side", 0, path); if (g_bside < -117) g_bside = -117; if (g_bside > 2000) g_bside = 2000;
    g_bgap = GetPrivateProfileIntA("display", "buff_spacing", 5, path); if (g_bgap < 0) g_bgap = 0; if (g_bgap > 32) g_bgap = 32;
    g_btint = GetPrivateProfileIntA("display", "buff_tint", 3, path); if (g_btint < 0 || g_btint > 255) g_btint = 3;
    g_dtint = GetPrivateProfileIntA("display", "debuff_tint", 1, path); if (g_dtint < 0 || g_dtint > 255) g_dtint = 1;
    g_info = master && GetPrivateProfileIntA("display", "map_info", 1, path);
    g_info_color = GetPrivateProfileIntA("display", "map_info_color", 4, path);
    g_stat_ranges = GetPrivateProfileIntA("features", "item_stat_ranges", 1, path);
    g_range_color = GetPrivateProfileIntA("features", "stat_range_color", 10, path); if (g_range_color < 0 || g_range_color > 15) g_range_color = 10;
    g_remember_filter = GetPrivateProfileIntA("lootfilter", "remember_filter_level", 1, path);
    g_item_move_cfg = master && GetPrivateProfileIntA("features", "item_move", 1, path);
    g_shift_id = master && GetPrivateProfileIntA("features", "shift_click_id", 1, path);
    GetPrivateProfileStringA("keys", "reload_command", "reload", g_reload_cmd, (DWORD)sizeof g_reload_cmd, path);
    g_key_stats = GetPrivateProfileIntA("keys", "stats_key", 66, path);
    g_key_merc = GetPrivateProfileIntA("keys", "stats_merc_key", 0, path);
    g_stats_right = GetPrivateProfileIntA("display", "stats_on_right", 1, path);
    g_key_up = GetPrivateProfileIntA("keys", "filter_up_key", GetPrivateProfileIntA("keys", "tier_up_key", 187, path), path);
    g_key_down = GetPrivateProfileIntA("keys", "filter_down_key", GetPrivateProfileIntA("keys", "tier_down_key", 189, path), path);
    g_key_up2 = GetPrivateProfileIntA("keys", "filter_up_key2", GetPrivateProfileIntA("keys", "tier_up_key2", 107, path), path);
    g_key_down2 = GetPrivateProfileIntA("keys", "filter_down_key2", GetPrivateProfileIntA("keys", "tier_down_key2", 109, path), path);
    g_merc_color = GetPrivateProfileIntA("automap", "minion_color", GetPrivateProfileIntA("automap", "merc_color", 13, path), path);
    g_npc_color = GetPrivateProfileIntA("automap", "npc_color", g_merc_color, path);   // unset: the same as minion_color
    g_me_color = GetPrivateProfileIntA("automap", "me_color", 14, path);
    static const char *ck[AM_SLOTS] = {"next_color", "other_color", "wp_color", "prev_color", "extra_color"};
    for (int i = 0; i < AM_SLOTS; i++) g_am_color[i] = GetPrivateProfileIntA("automap", ck[i], g_am_color[i], path);
}

static DWORD WINAPI worker(LPVOID arg) {
    (void)arg;
    g_stub_mem = VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    load_config();
    if (g_logging) {
        g_log = fopen("qol_ui.log", "w");
        if (g_log) { setvbuf(g_log, NULL, _IOFBF, 1 << 16); CreateThread(NULL, 0, log_flusher, NULL, 0, NULL); }
    }
    AddVectoredExceptionHandler(1, crash_handler);
    logf_("qol_ui started: lootfilter %s", g_filter_on ? "on" : "off");
    if (g_filter_on) CreateThread(NULL, 0, lootfilter_thread, NULL, 0, NULL);
    if (g_exit_lines || g_filter_on) CreateThread(NULL, 0, automap_thread, NULL, 0, NULL);
    if (master_on) CreateThread(NULL, 0, statrange_thread, NULL, 0, NULL);
    if (g_shift_id || g_item_move_cfg) CreateThread(NULL, 0, idclick_thread, NULL, 0, NULL);
    if (master_on) CreateThread(NULL, 0, keys_thread, NULL, 0, NULL);
    if (g_clock || g_info || g_buffs) CreateThread(NULL, 0, hud_thread, NULL, 0, NULL);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(NULL, 0, worker, NULL, 0, NULL); }
    return TRUE;
}
