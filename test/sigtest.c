// sigtest iname-*|loop-orig|loop-patched|loop-none <hex>: sig_find_itemname / sig_find_gameloop on a byte buffer
#include <stdio.h>
#include <string.h>
#include "sig.h"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    unsigned char big[256] = {0}; size_t n = strlen(argv[2]) / 2 < sizeof big ? strlen(argv[2]) / 2 : sizeof big;
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(argv[2] + 2 * i, "%2x", &v); big[i] = (unsigned char)v; }
    int patched = -1;
    if (!strncmp(argv[1], "loop-", 5)) {
        long g = sig_find_gameloop(big, n, &patched);
        int ok = !strcmp(argv[1], "loop-orig") ? (g == 4 && !patched) : !strcmp(argv[1], "loop-patched") ? (g == 4 && patched == 1) : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    if (!strncmp(argv[1], "gcopy-", 6)) {
        long g = sig_find_goldcopy(big, n, &patched);
        int ok = !strcmp(argv[1], "gcopy-orig") ? (g == 0 && !patched) : !strcmp(argv[1], "gcopy-patched") ? (g == 0 && patched == 1) : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    if (!strncmp(argv[1], "chat-", 5)) {
        long g = sig_find_chatsend(big, n);
        int ok = !strcmp(argv[1], "chat-ok") ? g == 16 : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    if (!strncmp(argv[1], "gold-", 5)) {
        long g = sig_find_golddraw(big, n);
        int ok = !strcmp(argv[1], "gold-ok") ? g == 20 : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    if (!strncmp(argv[1], "draw-", 5)) {
        long g = sig_find_gamedraw(big, n, &patched);
        int ok = !strcmp(argv[1], "draw-orig") ? (g == 25 && !patched) : !strcmp(argv[1], "draw-patched") ? (g == 25 && patched == 1) : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    if (!strncmp(argv[1], "map-", 4)) {
        long g = sig_find_automap(big, n, &patched);
        int ok = !strcmp(argv[1], "map-orig") ? (g == 29 && !patched) : !strcmp(argv[1], "map-patched") ? (g == 29 && patched == 1) : g < 0;
        printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", g); return !ok;
    }
    long at = sig_find_itemname(big, n, &patched);
    int ok = !strcmp(argv[1], "iname-orig") ? (at == 4 && !patched) : !strcmp(argv[1], "iname-patched") ? (at == 4 && patched == 1) : at < 0;
    printf("%s %s (offset %ld)\n", argv[1], ok ? "ok" : "FAIL", at); return !ok;
}
