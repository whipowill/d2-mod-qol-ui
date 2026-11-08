// Offline tests for the item filter engine. Usage: filter_test            (built-in checks)
//                                                 filter_test <BH.cfg>    (also parses a real config and lists unknown tokens)
#include "../src/filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

typedef struct { int sockets, fres, cres, lres, sk245, gold; } stats_t;
static int get_stat(void *ctx, int stat, int layer) {
    stats_t *s = (stats_t *)ctx;
    switch (stat) { case 194: return s->sockets; case 39: return s->fres; case 43: return s->cres; case 41: return s->lres; case 107: return layer == 245 ? s->sk245 : 0; case 14: return s->gold; }
    return 0;
}
static fi_item item(const char *code, int quality, unsigned groups, stats_t *st) {
    fi_item it; memset(&it, 0, sizeof it); memcpy(it.code, code, 3); it.quality = quality; it.groups = groups; it.ilvl = 85; it.qlvl = 30;
    it.stat = get_stat; it.ctx = st; it.filter_level = 1; it.clvl = 80; it.basename = code; it.price = -1;
    return it;
}
static char *name_of(fi_set *s, const fi_item *it, const char *orig, int *blocked) { static char out[600]; fi_name(s, it, orig, out, sizeof out, blocked); return out; }

int main(int argc, char **argv) {
    const char *cfg =
        "// test config\n"
        "ConditionGroup[boots]: EQ5 NMAG\n"
        "ItemDisplay[UNI]: %GOLD%%NAME%%DOT-61%%CONTINUE%\n"
        "ItemDisplay[UNI SUP]: %RED%[%ILVL%] %NAME%\n"
        "ItemDisplay[UNI]: %WHITE%%NAME%\n"
        "ItemDisplay[RARE FRES+CRES+LRES>79]: %PURPLE%%NAME% %TIER-3%\n"
        "ItemDisplay[boots !SOCK=0]:\n"                                     // ignore rule (no text)
        "ItemDisplay[SK245>1 (ETH OR RW)]: %TEAL%SK %NAME%\n"
        "ItemDisplay[xyz]: %Sage%lower %name%\n"
        "ItemDisplay[COUNT=2,FRES>30,CRES>30,LRES>30]: counted %NAME%\n"
        "ItemDisplay[BADTOKEN]: never\n";
    int unknown = 0; char bad[96];
    fi_set *s = fi_parse(cfg, strlen(cfg), &unknown, bad, sizeof bad);
    CHECK(fi_rule_count(s) == 9, "rule count");
    CHECK(unknown == 1 && !strcmp(bad, "BADTOKEN"), "unknown token reported");
    { stats_t z0 = {0}; fi_item any = item("zzz", 2, 0, &z0); CHECK(strcmp(name_of(s, &any, "n", NULL), "never") != 0, "a rule of only unknown tokens matches nothing"); }

    stats_t z = {0};
    fi_item u = item("uar", 7, 0, &z);
    CHECK(!strcmp(name_of(s, &u, "Shako", NULL), "\xFF" "c0\xFF" "c4Shako"), "unique: first rule continues, second needs SUP, third stops (NAME is the name so far)");
    u.quality = 7; { fi_action a[4]; int n = fi_actions(s, &u, a, 4); CHECK(n == 1 && a[0].dot == 0x61, "dot colour parsed"); }
    fi_item r = item("xtb", 6, 0, &z); stats_t rs = {0, 30, 25, 26, 0, 0}; r.ctx = &rs;
    { fi_action a[4]; int n = fi_actions(s, &r, a, 4); CHECK(n == 0, "map rules are only those with map colours"); }
    CHECK(strstr(name_of(s, &r, "Boots", NULL), "\xFF" "c;Boots") != NULL, "rare with 81 total resists matches the sum condition");
    rs.cres = 10; CHECK(strstr(name_of(s, &r, "Boots", NULL), "\xFF" "c;") == NULL, "sum below 80 does not match");
    // precedence: SK245>1 (ETH OR RW) is an implicit AND
    stats_t ks = {0, 0, 0, 0, 2, 0}; fi_item k = item("abc", 6, 0, &ks);
    CHECK(strstr(name_of(s, &k, "X", NULL), "SK") == NULL, "needs ETH or RW");
    k.flags = 0x400000; CHECK(strstr(name_of(s, &k, "X", NULL), "SK X") != NULL, "ETH satisfied");
    // group + ignore rule: normal boots with sockets are blocked
    stats_t bs = {2, 0, 0, 0, 0, 0}; fi_item bt = item("lbt", 2, FI_G_BOOTS, &bs);
    int blocked = 0; char *nm = name_of(s, &bt, "Boots", &blocked);
    CHECK(blocked == 1 && strstr(nm, "[blocked]"), "ignore rule blocks");
    fi_item bt2 = item("lbt", 2, FI_G_BOOTS, &z); (void)name_of(s, &bt2, "Boots", &blocked); CHECK(blocked == 0, "SOCK=0 does not block");
    // lower-case macros are upper-cased, item code rules
    fi_item x = item("xyz", 2, 0, &z); CHECK(!strcmp(name_of(s, &x, "Thing", NULL), "\xFF" "c\x07lower Thing"), "macro case");
    // COUNT
    stats_t cs = {0, 40, 40, 0, 0, 0}; fi_item c = item("cnt", 4, 0, &cs);
    CHECK(strstr(name_of(s, &c, "Ring", NULL), "counted Ring") != NULL, "COUNT=2 of 3");
    cs.cres = 0; CHECK(strstr(name_of(s, &c, "Ring", NULL), "counted") == NULL, "COUNT below 2");
    fi_free(s);

    // operator precedence quirk: AND and OR share one level, evaluated left to right: A OR B AND C == (A OR B) AND C
    const char *q = "ItemDisplay[RW OR ETH SOCK=3]: hit\n";
    fi_set *s2 = fi_parse(q, strlen(q), NULL, NULL, 0);
    stats_t q3 = {3, 0, 0, 0, 0, 0}, q0 = {0, 0, 0, 0, 0, 0};
    fi_item qi = item("abc", 6, 0, &q3); qi.flags = 0x400000;
    CHECK(!strcmp(name_of(s2, &qi, "n", NULL), "hit"), "(ETH) and SOCK=3");
    qi.ctx = &q0; CHECK(!strcmp(name_of(s2, &qi, "n", NULL), "n"), "ETH but no sockets: (A OR B) AND C is false");
    fi_free(s2);

    { // GOLD: the size of a gold pile
        const char *gc = "ItemDisplay[GOLD<500]:\n";
        fi_set *gs = fi_parse(gc, strlen(gc), &unknown, bad, sizeof bad);
        stats_t small = {0, 0, 0, 0, 0, 200}, big = {0, 0, 0, 0, 0, 900};
        fi_item g1 = item("gld", 2, 0, &small), g2 = item("gld", 2, 0, &big), g3 = item("xyz", 2, 0, &small);
        CHECK(fi_is_blocked(gs, &g1), "small gold pile is blocked by GOLD<500");
        CHECK(!fi_is_blocked(gs, &g2), "big gold pile is not");
        CHECK(!fi_is_blocked(gs, &g3), "GOLD never matches a non-gold item");
        fi_free(gs);
    }

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *buf = (char *)malloc((size_t)n + 1); if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
        fclose(f);
        fi_set *real = fi_parse(buf, (size_t)n, &unknown, bad, sizeof bad);
        printf("real config: %d rules, %d unrecognised token(s)%s%s\n", fi_rule_count(real), unknown, unknown ? ", first: " : "", unknown ? bad : "");
        fi_free(real); free(buf);
    }
    printf(fails ? "%d FAIL\n" : "filter ok\n", fails);
    return fails != 0;
}
