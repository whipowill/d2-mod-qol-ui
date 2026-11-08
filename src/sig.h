// Byte-signature search for the D2Client item-name code. Pure functions, unit-tested natively (test/sigtest.c).
#ifndef UI_SIG_H
#define UI_SIG_H
#include <stddef.h>

__attribute__((unused)) static long qt_find(const unsigned char *buf, size_t n, size_t from, size_t to, const short *pat, int len, int *count) {
    long found = -1; int c = 0;
    if (to > n) to = n;
    for (size_t i = from; i + (size_t)len <= to; i++) {
        int ok = 1;
        for (int k = 0; k < len && ok; k++) if (pat[k] >= 0 && buf[i + (size_t)k] != (unsigned char)pat[k]) ok = 0;
        if (ok) { if (found < 0) found = (long)i; c++; }
    }
    if (count) *count = c;
    return found;
}

__attribute__((unused)) static long sig_find_itemname(const unsigned char *buf, size_t n, int *patched) {
    static const short orig[]   = {0x8B,0x6C,0x24,0x10, 0x8A,0x85,0x2A,0x01,0x00,0x00, 0x84,0xC0,0x74,0x4B, 0x8A,0x85,0x2B,0x01,0x00,0x00};
    static const short jumped[] = {0x8B,0x6C,0x24,0x10, 0xE8,-1,-1,-1,-1,0x90, 0x84,0xC0,0x74,0x4B, 0x8A,0x85,0x2B,0x01,0x00,0x00};
    int c1 = 0, c2 = 0;
    long a = qt_find(buf, n, 0, n, orig, (int)(sizeof orig / sizeof *orig), &c1), b = qt_find(buf, n, 0, n, jumped, (int)(sizeof jumped / sizeof *jumped), &c2);
    if (c1 + c2 != 1) return -1;
    if (patched) *patched = c2 == 1;
    return (c1 ? a : b) + 4;
}
// The game-loop function entry (BH's GameLoop patch, D2Client +0x45280): `sub esp,20 ; mov [esp+0C],ecx ; call ...`. Returns the offset of the
// 7 bytes to replace (the call that follows is part of the signature only), -1 unless exactly one match.
__attribute__((unused)) static long sig_find_gameloop(const unsigned char *buf, size_t n, int *patched) {
    static const short orig[]   = {0x83,0xEC,0x20, 0x89,0x4C,0x24,0x0C, 0xE8,-1,-1,-1,-1};
    static const short jumped[] = {0xE8,-1,-1,-1,-1, 0x90,0x90, 0xE8,-1,-1,-1,-1};
    int c1 = 0, c2 = 0;
    long a = qt_find(buf, n, 0, n, orig, (int)(sizeof orig / sizeof *orig), &c1), b = qt_find(buf, n, 0, n, jumped, (int)(sizeof jumped / sizeof *jumped), &c2);
    if (c1 + c2 != 1) return -1;
    if (patched) *patched = c2 == 1;
    return c1 ? a : b;
}
// The tail of the automap-draw function (D2Client +0x7349A): `... call ; mov ecx,edi ; pop edi ; pop esi ; jmp far ; ret`. We replace the 9 bytes
// `8B CF 5F 5E E9 rel32` with `E9 rel32 90 90 90 90` (jump to a stub that draws, then does the same). Returns the offset of those 9 bytes.
__attribute__((unused)) static long sig_find_automap(const unsigned char *buf, size_t n, int *patched) {
    static const short orig[]   = {0x83,0xFE,0x06,0x74,0x05,0x83,0xFE,0x08,0x75,0x05,0xE8,-1,-1,-1,-1,0xA1,-1,-1,-1,-1,0x85,0xC0,0x74,0x05,0xE8,-1,-1,-1,-1,
                                   0x8B,0xCF,0x5F,0x5E,0xE9,-1,-1,-1,-1,0xC3};
    static const short jumped[] = {0x83,0xFE,0x06,0x74,0x05,0x83,0xFE,0x08,0x75,0x05,0xE8,-1,-1,-1,-1,0xA1,-1,-1,-1,-1,0x85,0xC0,0x74,0x05,0xE8,-1,-1,-1,-1,
                                   0xE9,-1,-1,-1,-1,0x90,0x90,0x90,0x90,0xC3};
    int c1 = 0, c2 = 0;
    long a = qt_find(buf, n, 0, n, orig, (int)(sizeof orig / sizeof *orig), &c1), b = qt_find(buf, n, 0, n, jumped, (int)(sizeof jumped / sizeof *jumped), &c2);
    if (c1 + c2 != 1) return -1;
    if (patched) *patched = c2 == 1;
    return (c1 ? a : b) + 29;
}
// The end of the in-game draw function (BH's GameDraw patch, D2Client +0x1D7B4): `pop esi ; pop ebx ; pop ecx ; ret 4`, preceded by calls.
// We replace those 6 bytes with `E9 rel32 90`. Returns the offset of the 6 bytes.
__attribute__((unused)) static long sig_find_gamedraw(const unsigned char *buf, size_t n, int *patched) {
    static const short orig[]   = {0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0x5E,0x5B,0x59,0xC2,0x04,0x00};
    static const short jumped[] = {0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE8,-1,-1,-1,-1, 0xE9,-1,-1,-1,-1, 0x90};
    int c1 = 0, c2 = 0;
    long a = qt_find(buf, n, 0, n, orig, (int)(sizeof orig / sizeof *orig), &c1), b = qt_find(buf, n, 0, n, jumped, (int)(sizeof jumped / sizeof *jumped), &c2);
    if (c1 + c2 != 1) return -1;
    if (patched) *patched = c2 == 1;
    return (c1 ? a : b) + 25;
}
// The ground-label builder's draw call for GOLD piles (D2Client +0x19AB5): `mov ecx,[..] ; mov edx,[..] ; push 1 ; push ebx ; push ecx ; lea ecx,[esp+40h] ;
// call DrawText-thunk ; pop edi ...`. Returns the offset of the `E8 rel32` (it is the only change we make: the rel32 points to our stub), -1 unless the pattern
// occurs exactly once. The caller decides "already patched" by checking where the call goes.
__attribute__((unused)) static long sig_find_golddraw(const unsigned char *buf, size_t n) {
    static const short pat[] = {0x8B,0x0D,-1,-1,-1,-1, 0x8B,0x15,-1,-1,-1,-1, 0x6A,0x01, 0x53, 0x51, 0x8D,0x4C,0x24,0x40, 0xE8,-1,-1,-1,-1,
                                0x5F,0x5D,0x5B,0x5E, 0x81,0xC4,0x24,0x02,0x00,0x00, 0xC2,0x08,0x00};
    int c = 0;
    long a = qt_find(buf, n, 0, n, pat, (int)(sizeof pat / sizeof *pat), &c);
    return c == 1 ? a + 20 : -1;
}
// The same builder when its caller wants the TEXT instead of drawing it (D2Client +0x19AEC, gold branch): `lea edx,[esp+34h] ; mov ecx,esi ; call ebp` copies the
// finished text to the caller's buffer, then the function returns. 8 bytes, replaced by `E8 rel32 90 90 90`; offset returned, -1 unless exactly one match.
// (Already patched: the first byte is E8 instead of 8D.)
__attribute__((unused)) static long sig_find_goldcopy(const unsigned char *buf, size_t n, int *patched) {
    static const short orig[]   = {0x8D,0x54,0x24,0x34, 0x8B,0xCE, 0xFF,0xD5, 0x5F,0x5D,0x5B,0x5E, 0x81,0xC4,0x24,0x02,0x00,0x00, 0xC2,0x08,0x00};
    static const short jumped[] = {0xE8,-1,-1,-1,-1, 0x90,0x90,0x90, 0x5F,0x5D,0x5B,0x5E, 0x81,0xC4,0x24,0x02,0x00,0x00, 0xC2,0x08,0x00};
    int c1 = 0, c2 = 0;
    long a = qt_find(buf, n, 0, n, orig, (int)(sizeof orig / sizeof *orig), &c1), b = qt_find(buf, n, 0, n, jumped, (int)(sizeof jumped / sizeof *jumped), &c2);
    if (c1 + c2 != 1) return -1;
    if (patched) *patched = c2 == 1;
    return c1 ? a : b;
}
// The plain chat-message send (D2Client +0x1B2342 relative: `mov al,[esp+71h] ; lea esi,[esp+70h] ; mov byte [esp+172h],0 ; call build-chat-packet ; jmp end`).
// ESI points at the packet being built: byte 0 is the packet id, the typed ANSI text starts at +3. Returns the offset of the `E8 rel32` (patched or not;
// the caller tells by where the call goes), -1 unless exactly one match.
__attribute__((unused)) static long sig_find_chatsend(const unsigned char *buf, size_t n) {
    static const short pat[] = {0x8A,0x44,0x24,0x71, 0x8D,0x74,0x24,0x70, 0xC6,0x84,0x24,0x72,0x01,0x00,0x00,0x00, 0xE8,-1,-1,-1,-1, 0xE9,-1,-1,-1,-1};
    int c = 0;
    long a = qt_find(buf, n, 0, n, pat, (int)(sizeof pat / sizeof *pat), &c);
    return c == 1 ? a + 16 : -1;
}
#endif
