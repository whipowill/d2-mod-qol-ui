// Auto party: invite other players to your party and accept invites (BH Modules/Party, "Autoparty Enabled"). Included into hud.h.
// Runs from hud_frame (game thread) about once a second. The decision is in party.c (unit-tested); this file reads the roster and calls the game.
// 1.13d (BH's second offset column; your own unit id is UnitAny +0xC): roster list head D2Client +0x11CB04, RosterUnit: +0x10 unit id, +0x20 level (WORD), +0x22 party id (WORD),
// +0x30 party flags, +0x80 next; ClickParty +0xA2250 (BH's wrapper: eax = ecx = roster, edx = mode, mode 2 = invite / accept), LeaveParty +0xA26A0.
#ifndef UI_AUTOPARTY_H
#define UI_AUTOPARTY_H
#include "party.h"

static int g_auto_party = 0;
typedef void (__cdecl *ap_click_t)(DWORD roster, DWORD mode);
typedef void (__fastcall *ap_leave_t)(void);
static ap_click_t p_apclick; static ap_leave_t p_apleave;

static void ap_init(BYTE *cl) {                                      // after the timestamp check in hud_thread
    BYTE *st = stub_alloc();
    if (!st) { logf_("autoparty: no stub memory; off"); return; }
    static const BYTE a[] = {0x60, 0x8B, 0x44, 0x24, 0x24, 0x8B, 0x54, 0x24, 0x28, 0x89, 0xC1};   // pushad ; mov eax,[esp+24h] ; mov edx,[esp+28h] ; mov ecx,eax
    BYTE *q = emit(st, a, sizeof a); q = emit_rel(q, 0xE8, (DWORD)(cl + 0xA2250));
    static const BYTE b[] = {0x61, 0xC3};                           // popad ; ret
    emit(q, b, 2);
    p_apclick = (ap_click_t)st; p_apleave = (ap_leave_t)(cl + 0xA26A0);
}

static void ap_frame(void) {
    if (!g_auto_party || !master_on || !p_apclick || !g_hud_cl || !p_player) return;
    static DWORD last; DWORD now = GetTickCount();
    if (now - last < 1000) return;
    last = now;
    DWORD pl = p_player();
    if (!pl || !readable((void *)pl, 0x10)) return;
    party_member m[PARTY_MAX]; DWORD ptr[PARTY_MAX]; int n = 0;
    DWORD r = *(DWORD *)(g_hud_cl + 0x11CB04);
    readable_reset();
    while (r && n < PARTY_MAX && readable_c((void *)r, 0x84)) {
        m[n].unit_id = *(DWORD *)(r + 0x10); m[n].level = *(WORD *)(r + 0x20); m[n].party_id = *(WORD *)(r + 0x22); m[n].flags = *(DWORD *)(r + 0x30);
        ptr[n++] = r;
        r = *(DWORD *)(r + 0x80);
    }
    if (n < 2 || r) { static int once; if (r && !once) { once = 1; logf_("autoparty: roster list has %d entries and does not end; off", n); } return; }   // alone (single player) is silent
    int click[PARTY_MAX], leave = 0;
    if (party_decide(m, n, *(DWORD *)(pl + 0xC), click, &leave) != 0) return;               // roster not ready, or ourselves not in it
    if (leave) { logf_("autoparty: leaving the party"); p_apleave(); return; }
    for (int i = 0; i < n; i++) if (click[i]) { logf_("autoparty: click player %u (party %u, flags %u)", m[i].unit_id, m[i].party_id, m[i].flags); p_apclick(ptr[i], 2); }
}
#endif
