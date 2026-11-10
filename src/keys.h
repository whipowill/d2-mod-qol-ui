// Key binds (polled from a helper thread, only while the game window has focus, you are in a game and no chat box is open):
//   typing .reload in chat (the word is [keys] reload_command) re-reads qol_ui.ini and qol_ui_filter.cfg
//   show_items_key                    toggles always-showing the ground item labels (showitems.h)
//   filter_up_key / filter_down_key   raise / lower the filter level the rules compare with FILTLVL (0 none, 1 minimal, 2 moderate, 3 aggressive)
// Keys are Windows virtual-key numbers (decimal) in qol_ui.ini [keys]; 0 = unbound.
#ifndef UI_KEYS_H
#define UI_KEYS_H
static void load_config(void);

static int g_key_inspect = 73, g_key_stats = 66, g_key_merc = 0, g_key_up = 187, g_key_down = 189, g_key_up2 = 107, g_key_down2 = 109;

static void keys_say(const wchar_t *fmt, int a, int b) {   // fmt takes two ints           // queued; printed on the game thread by mh_game_loop
    if (InterlockedCompareExchange(&g_msg_ready, 2, 0) != 0) return;
    wchar_t body[80]; swprintf(body, 80, fmt, a, b);
    swprintf(g_msg, 96, L"\377c4UI\377c0: %ls", body);                 // "UI:": gold UI, white colon
    InterlockedExchange(&g_msg_ready, 1);
}
static char g_reload_cmd[32] = "reload";
static volatile LONG g_reload_req;

// ---- chat commands: the game's "send typed chat message" step (a call that builds the chat packet in ESI) is redirected to a stub that asks qol_chat first ----
static int __fastcall qol_chat(const char *pkt) {                    // pkt: byte 0 packet id, typed ANSI text from +3. Returns -1 = consumed (not sent)
    if (!pkt || !readable(pkt, 8) || !g_reload_cmd[0]) return 0;
    const char *t = pkt + 3; int i = 0;
    while (t[i] == ' ') i++;
    if (t[i] != '.') return 0;
    i++;
    size_t n = strlen(g_reload_cmd);
    if (_strnicmp(t + i, g_reload_cmd, n)) return 0;
    int e = i + (int)n; while (t[e] == ' ') e++;
    if (t[e]) return 0;                                              // ".reloadx" is not our command
    InterlockedExchange(&g_reload_req, 1);                           // done on the key thread (a reload waits for the old rules to be unused)
    return -1;
}
static void keys_install_chat(void) {
    HMODULE cl = NULL;
    for (int i = 0; i < 6000 && !cl; i++) { cl = GetModuleHandleA("D2Client.dll"); if (!cl) Sleep(100); }
    if (!cl) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)cl + ((IMAGE_DOS_HEADER *)cl)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2CLIENT_TIMESTAMP_113D) return;
    BYTE *base = (BYTE *)cl; IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int k = 0; k < nt->FileHeader.NumberOfSections; k++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE *code = base + sec->VirtualAddress;
        long at = sig_find_chatsend(code, sec->Misc.VirtualSize);
        if (at < 0) continue;
        BYTE *site = code + at; DWORD orig = (DWORD)(site + 5) + *(DWORD *)(site + 1);
        if (orig < (DWORD)base || orig >= (DWORD)base + 0xCE000) { logf_("chat: hook already installed"); return; }
        BYTE *st = stub_alloc(); if (!st) return;
        static const BYTE a[] = {0x60, 0x8B, 0xCE};                  // pushad ; mov ecx,esi
        static const BYTE b[] = {0x83, 0xF8, 0xFF, 0x61, 0x74, 0x06};   // cmp eax,-1 ; popad (keeps the flags) ; je block
        static const BYTE c[] = {0xC3, 0x31, 0xC0, 0xC3};            // ret ; block: xor eax,eax ; ret
        BYTE *p = emit(st, a, 3);
        p = emit_rel(p, 0xE8, (DWORD)qol_chat);
        p = emit(p, b, sizeof b);
        p = emit_rel(p, 0xE8, orig);                                 // not ours: the game's own chat-packet builder
        p = emit(p, c, sizeof c);
        BYTE j[5]; j[0] = 0xE8; *(DWORD *)(j + 1) = (DWORD)st - (DWORD)(site + 5);
        patch_bytes(site, j, 5);
        logf_("chat: typing .%s reloads the settings and rules (D2Client +%lx)", g_reload_cmd, (unsigned long)(site - base));
        return;
    }
    logf_("chat: the chat-send code was not found in D2Client (or appears more than once); the .%s command is off", g_reload_cmd);
}

static int keys_down(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000); }
static int keys_focused(void) {
    HWND w = GetForegroundWindow(); DWORD pid = 0;
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static void keys_reload(void) {
    int was_show = g_show_items_cfg;
    load_config();
    if (g_logging && !g_log) { g_log = fopen("qol_ui.log", "a"); if (g_log) { setvbuf(g_log, NULL, _IOFBF, 1 << 16); CreateThread(NULL, 0, log_flusher, NULL, 0, NULL); } }   // logging switched on after start
    am_label_apply();                                               // the label colours are patched into the game's code: write the new ones
    if (g_show_items_cfg != was_show) InterlockedExchange(&g_show_items, g_show_items_cfg ? 1 : 0);   // always_show_items changed (a key toggle in between is kept otherwise)
    g_name_gen++;                                                   // item labels may depend on the settings: forget the cached ones
    char *cfg; size_t cn; int rules = -1;
    if (g_fi && mh_read_file("qol_ui_filter.cfg", &cfg, &cn)) {
        fi_set *s = fi_parse(cfg, cn, NULL, NULL, 0); free(cfg);
        fi_set_video(s, mh_video_glide());
        g_name_gen++;                                               // forget the cached item labels
        fi_set *old = g_fi; g_fi = s; rules = fi_rule_count(s);
        Sleep(2000);                                               // let a game-thread call that was using the old set finish
        fi_free(old);
    }
    logf_("reload: settings and %d rules re-read", rules);
    keys_say(L"Reloaded (%d rules, filter level %d).", rules < 0 ? 0 : rules, g_filter_level);
}

static DWORD WINAPI keys_thread(LPVOID arg) {
    (void)arg;
    int prev[9] = {0};
    keys_install_chat();
    for (;;) {
        Sleep(40);
        if (InterlockedExchange(&g_reload_req, 0)) keys_reload();
        if (!master_on) { memset(prev, 0, sizeof prev); continue; }                    // [qol_ui] enabled=0: only the reload command works
        int k[9] = {g_key_up, g_key_down, g_key_up2, g_key_down2, g_key_stats, 27, g_key_merc, g_key_inspect, g_key_show_items}, now[9], fire[9];
        for (int i = 0; i < 9; i++) { now[i] = keys_down(k[i]); fire[i] = now[i] && !prev[i]; prev[i] = now[i]; }
        if (!(fire[0] | fire[1] | fire[2] | fire[3] | fire[4] | fire[5] | fire[6] | fire[7] | fire[8])) continue;
        HMODULE cl = GetModuleHandleA("D2Client.dll");
        if (!cl || !p_player || !keys_focused() || !p_player()) continue;                // not in a game
        if (*(DWORD *)((BYTE *)cl + 0x11C890 + 5 * 4)) continue;                          // chat box open (UI var 5)
        if (fire[7] && g_inspect_ok) g_iv_req = GetTickCount() | 1;                       // inspect: handled on the game thread a moment later (inspect.h)
        if (fire[8] && g_show_items_ok) {                                              // always-show-items toggle (showitems.h)
            LONG on = !g_show_items; InterlockedExchange(&g_show_items, on);
            keys_say(on ? L"Item labels always shown.%.0d%.0d" : L"Item labels shown only with Alt.%.0d%.0d", 0, 0);
        }
        if (fire[4]) g_stats_open = !g_stats_open;
        if (fire[5]) g_stats_open = 0;                                                 // Esc closes the stats panel
        if (fire[6]) { g_stats_merc = !g_stats_merc; g_stats_open = 1; }
        if (fire[0] || fire[2] || fire[1] || fire[3]) {                                   // + / -: the filter level (BH: 0 None, 1 Minimal, 2 Moderate, 3 Aggressive)
            int up = fire[0] || fire[2];
            if (up && g_filter_level < 3) g_filter_level++; else if (!up && g_filter_level > 0) g_filter_level--;
            static const wchar_t *fm[] = {L"Filter level 0 (none).%.0d", L"Filter level 1 (minimal).%.0d", L"Filter level 2 (moderate).%.0d", L"Filter level 3 (aggressive).%.0d"};
            g_name_gen++;                                                                  // item labels depend on FILTLVL: forget the cached ones
            if (g_remember_filter) {                                                       // keep it for the next game: write the number back into qol_ui.ini
                char ini[MAX_PATH], num[8]; GetCurrentDirectoryA(MAX_PATH, ini); lstrcatA(ini, "\\qol_ui.ini"); snprintf(num, sizeof num, "%d", g_filter_level);
                WritePrivateProfileStringA("lootfilter", "filter_level", num, ini);
            }
            keys_say(fm[g_filter_level], 0, 0);
        }
    }
    return 0;
}
#endif
