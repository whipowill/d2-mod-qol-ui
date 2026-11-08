// Item filter rule engine (see filter.h). The rule syntax and evaluation order follow the BH item display, including its quirks:
// adjacent conditions are ANDed implicitly, AND and OR have the same precedence (left to right), unknown tokens are skipped.
#include "filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ---- stats and constants -------------------------------------------------------------------------------------------------
enum { S_STR = 0, S_DEX = 2, S_MAXHP = 7, S_MAXMANA = 9, S_LEVEL = 12, S_ATTACKRATING = 19, S_MINDMG = 21, S_MAXDMG = 22, S_DEFENSE = 31,
       S_FIRERES = 39, S_LIGHTRES = 41, S_COLDRES = 43, S_POISRES = 45, S_REPLIFE = 74, S_GF = 79, S_MF = 80, S_CLASSSK = 83, S_IAS = 93,
       S_FRW = 96, S_NONCLASSSK = 97, S_FHR = 99, S_FBR = 102, S_FCR = 105, S_SINGLESK = 107, S_DTM = 114, S_TOHITPCT = 119,
       S_ALLSK = 127, S_MAEK = 138, S_SKILLTAB = 188, S_SOCKETS = 194, S_REPAIR = 252, S_REPQUANT = 253, S_STATMAX = 511 };
#define SKILL_MAX 400
#define SKILLTAB_MAX 50

enum { OP_EQ, OP_GT, OP_LT, OP_NONE };
enum { CT_NONE, CT_LP, CT_RP, CT_NOT, CT_BIN, CT_OPERAND };
enum { N_AND, N_OR, N_NOT, N_LP, N_RP, N_CODE, N_QID, N_FLAG, N_QUAL, N_NMAG, N_GEMLVL, N_GEMTYPE, N_RUNE, N_FALSE, N_ILVL, N_QLVL, N_ALVL,
       N_CRAFTALVL, N_LVLREQ, N_GROUP, N_STAT, N_CHARSTAT, N_FOOLS, N_GOODCLS, N_GOODTAB, N_CHARGED, N_DIFF, N_FILT, N_PRICE, N_PTYPE, N_RES,
       N_ADD, N_COUNT, N_CLVL, N_ED, N_MAXDUR, N_GOLD };

typedef struct fi_rule fi_rule;
typedef struct { int type, ct, op; unsigned v; int a, b; char code[4]; int *stats; int nstats; fi_rule *subs; int nsubs; } fi_node;
struct fi_rule { fi_node *rpn; int n; int dead; fi_action act; };

struct fi_set {
    fi_rule *rules; int n, cap;
    int *name_r, nname, *desc_r, ndesc, *map_r, nmap, *ign_r, nign, *white_r, nwhite;
    int *goodcls, ngoodcls, *goodtab, ngoodtab;
    int glide;
    struct { char *k, *v; } *groups; int ngroups;
    int unknown; char firstbad[96];
};

static int ci_ncmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if (x != y) return x - y; if (!x) return 0; } return 0; }
static char *dupn(const char *s, size_t n) { char *r = (char *)malloc(n + 1); memcpy(r, s, n); r[n] = 0; return r; }
static char *dups(const char *s) { return dupn(s, strlen(s)); }
static void trim(char *s) {
    size_t n = strlen(s), a = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[a] && isspace((unsigned char)s[a])) a++;
    if (a) memmove(s, s + a, strlen(s + a) + 1);
}
static int cmp_u(unsigned l, int op, unsigned r) { return op == OP_EQ ? l == r : op == OP_GT ? l > r : op == OP_LT ? l < r : 0; }
static int getop(char c) { return c == '=' ? OP_EQ : c == '<' ? OP_LT : c == '>' ? OP_GT : OP_NONE; }

static const struct { const char *k; int stat; } combo[] = {
    {"LIFE", S_MAXHP}, {"MANA", S_MAXMANA}, {"STR", S_STR}, {"DEX", S_DEX}, {"CRES", S_COLDRES}, {"FRES", S_FIRERES}, {"LRES", S_LIGHTRES},
    {"PRES", S_POISRES}, {"MINDMG", S_MINDMG}, {"MAXDMG", S_MAXDMG} };

static const struct { const char *k; const char *chat; int map; } colors[] = {
    {"WHITE", "\xFF" "c0", 0x20}, {"RED", "\xFF" "c1", 0x0A}, {"GREEN", "\xFF" "c2", 0x84}, {"BLUE", "\xFF" "c3", 0x97}, {"GOLD", "\xFF" "c4", 0x0D},
    {"GRAY", "\xFF" "c5", 0xD0}, {"BLACK", "\xFF" "c6", 0x00}, {"TAN", "\xFF" "c7", 0x5A}, {"ORANGE", "\xFF" "c8", 0x60}, {"YELLOW", "\xFF" "c9", 0x0C},
    {"PURPLE", "\xFF" "c;", 0x9B}, {"DARK_GREEN", "\xFF" "c:", 0x76}, {"CORAL", "\xFF" "c\x06", 0x66}, {"SAGE", "\xFF" "c\x07", 0x82},
    {"TEAL", "\xFF" "c\x09", 0xCB}, {"LIGHT_GRAY", "\xFF" "c\x0C", 0xD6} };
#define NCOLORS ((int)(sizeof colors / sizeof *colors))

static const char *GEMLV[] = {"NONE", "Chipped", "Flawed", "Normal", "Flawless", "Perfect"};
static const char *GEMTY[] = {"NONE", "Amethyst", "Diamond", "Emerald", "Ruby", "Sapphire", "Topaz", "Skull"};

// ---- building the condition list --------------------------------------------------------------------------------------
typedef struct { fi_node *v; int n, cap; int last; fi_set *set; } blist;
static void bpush(blist *b, fi_node nd) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 16; b->v = (fi_node *)realloc(b->v, (size_t)b->cap * sizeof *b->v); }
    b->v[b->n++] = nd;
}
static int ctype(int t) { return t == N_AND || t == N_OR ? CT_BIN : t == N_NOT ? CT_NOT : t == N_LP ? CT_LP : t == N_RP ? CT_RP : CT_OPERAND; }
static void add_nonoperand(blist *b, int t) {
    fi_node nd; memset(&nd, 0, sizeof nd); nd.type = t;
    if ((t == N_NOT || t == N_LP) && (b->last == CT_OPERAND || b->last == CT_RP)) { fi_node a; memset(&a, 0, sizeof a); a.type = N_AND; bpush(b, a); }
    bpush(b, nd); b->last = ctype(t);
}
static void add_operand(blist *b, fi_node nd) {
    if (b->last == CT_OPERAND || b->last == CT_RP) { fi_node a; memset(&a, 0, sizeof a); a.type = N_AND; bpush(b, a); }
    bpush(b, nd); b->last = CT_OPERAND;
}
static fi_node mk(int type, int op, unsigned v, int a, int b) { fi_node n; memset(&n, 0, sizeof n); n.type = type; n.op = op; n.v = v; n.a = a; n.b = b; return n; }

static void parse_rule_text(fi_set *s, const char *cond, fi_rule *out, int is_sub);

static int starts(const char *k, const char *p) { return strncmp(k, p, strlen(p)) == 0; }
// parse a non-negative decimal number occupying the whole string; -1 when it is not one (or too large)
static int whole_num(const char *s) {
    if (!*s) return -1;
    long v = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return -1; v = v * 10 + (*s - '0'); if (v > 100000000) return -1; }
    return (int)v;
}

static void build_token(blist *b, const char *tok0) {
    char token[512]; strncpy(token, tok0, sizeof token - 1); token[sizeof token - 1] = 0;
    int endc[16], nend = 0, i;
    for (i = 0; token[i]; i++) {                       // leading syntax characters
        if (token[i] == '!') add_nonoperand(b, N_NOT);
        else if (token[i] == '(') add_nonoperand(b, N_LP);
        else if (token[i] == ')') add_nonoperand(b, N_RP);
        else break;
    }
    memmove(token, token + i, strlen(token + i) + 1);
    for (i = (int)strlen(token) - 1; i >= 0; i--) {    // trailing ones
        int t = token[i] == '!' ? N_NOT : token[i] == '(' ? N_LP : token[i] == ')' ? N_RP : -1;
        if (t < 0) break;
        if (nend < 16) { memmove(endc + 1, endc, (size_t)nend * sizeof *endc); endc[0] = t; nend++; }
    }
    token[i + 1] = 0;

    char key[512], delim = 0, valstr[512] = ""; unsigned value = 0; int op = OP_NONE;
    char *dp = strpbrk(token, "<=>");
    int fail = 0;
    if (dp) {
        size_t kl = (size_t)(dp - token); memcpy(key, token, kl); key[kl] = 0; trim(key);
        delim = *dp; strcpy(valstr, dp + 1); trim(valstr);
        if (valstr[0]) { char *e; long v = strtol(valstr, &e, 10); if (e == valstr) fail = 1; else value = (unsigned)v; }
    } else strcpy(key, token);
    op = getop(delim);
    if (fail) { b->set->unknown++; if (!b->set->firstbad[0]) snprintf(b->set->firstbad, sizeof b->set->firstbad, "%s", tok0); goto done; }

    size_t kl = strlen(key); int added = 1; unsigned v = value;
    if (!strcmp(key, "AND") || !strcmp(key, "&&")) add_nonoperand(b, N_AND);
    else if (!strcmp(key, "OR") || !strcmp(key, "||")) add_nonoperand(b, N_OR);
    else if (kl == 3 && !(isupper((unsigned char)key[0]) || isupper((unsigned char)key[1]) || isupper((unsigned char)key[2]))) {
        fi_node n = mk(N_CODE, 0, 0, 0, 0); memcpy(n.code, key, 3); add_operand(b, n);
    } else if (strchr(key, '+')) {
        fi_node n = mk(N_ADD, op, v, 0, 0); n.stats = (int *)calloc(16, sizeof(int));
        char *tmp = dups(key), *p = tmp;
        for (;;) {
            char *pl = strchr(p, '+'); if (pl) *pl = 0;
            for (size_t j = 0; j < sizeof combo / sizeof *combo; j++) if (!strcmp(p, combo[j].k) && n.nstats < 16) n.stats[n.nstats++] = combo[j].stat;
            if (!pl) break;
            p = pl + 1;
        }
        free(tmp); add_operand(b, n);
    } else if (!strcmp(key, "ETH")) add_operand(b, mk(N_FLAG, 0, 0x400000, 0, 0));
    else if (!strcmp(key, "SOCK")) add_operand(b, mk(N_STAT, op, v, S_SOCKETS, 0));
    else if (starts(key, "SET")) {
        int id = starts(key, "SET") && key[3] && strlen(key) <= 7 ? whole_num(key + 3) : -1;
        add_operand(b, id >= 0 ? mk(N_QID, 0, 5, id, 0) : mk(N_QUAL, 0, 5, 0, 0));
    } else if (!strcmp(key, "MAG")) add_operand(b, mk(N_QUAL, 0, 4, 0, 0));
    else if (!strcmp(key, "RARE")) add_operand(b, mk(N_QUAL, 0, 6, 0, 0));
    else if (starts(key, "UNI")) {
        int id = key[3] && strlen(key) <= 7 ? whole_num(key + 3) : -1;
        add_operand(b, id >= 0 ? mk(N_QID, 0, 7, id, 0) : mk(N_QUAL, 0, 7, 0, 0));
    } else if (!strcmp(key, "CRAFTALVL")) add_operand(b, mk(N_CRAFTALVL, op, v, 0, 0));
    else if (!strcmp(key, "CRAFT")) add_operand(b, mk(N_QUAL, 0, 8, 0, 0));
    else if (!strcmp(key, "RW")) add_operand(b, mk(N_FLAG, 0, 0x4000000, 0, 0));
    else if (!strcmp(key, "NMAG")) add_operand(b, mk(N_NMAG, 0, 0, 0, 0));
    else if (!strcmp(key, "SUP")) add_operand(b, mk(N_QUAL, 0, 3, 0, 0));
    else if (!strcmp(key, "INF")) add_operand(b, mk(N_QUAL, 0, 1, 0, 0));
    else if (!strcmp(key, "NORM")) add_operand(b, mk(N_GROUP, 0, FI_G_NORMAL, 0, 0));
    else if (!strcmp(key, "EXC")) add_operand(b, mk(N_GROUP, 0, FI_G_EXCEPTIONAL, 0, 0));
    else if (!strcmp(key, "ELT")) add_operand(b, mk(N_GROUP, 0, FI_G_ELITE, 0, 0));
    else if (!strcmp(key, "ID")) add_operand(b, mk(N_FLAG, 0, 0x10, 0, 0));
    else if (!strcmp(key, "ILVL")) add_operand(b, mk(N_ILVL, op, v, 0, 0));
    else if (!strcmp(key, "QLVL")) add_operand(b, mk(N_QLVL, op, v, 0, 0));
    else if (!strcmp(key, "ALVL")) add_operand(b, mk(N_ALVL, op, v, 0, 0));
    else if (!strcmp(key, "CLVL")) add_operand(b, mk(N_CLVL, op, v, 0, 0));
    else if (!strcmp(key, "FILTLVL")) add_operand(b, mk(N_FILT, op, v, 0, 0));
    else if (!strcmp(key, "DIFF")) add_operand(b, mk(N_DIFF, op, v, 0, 0));
    else if (!strcmp(key, "RUNE")) add_operand(b, mk(N_RUNE, op, v, 0, 0));
    else if (!strcmp(key, "GOLD")) add_operand(b, mk(N_GOLD, op, v, 0, 0));
    else if (!strcmp(key, "GEMTYPE")) add_operand(b, mk(N_GEMTYPE, op, v, 0, 0));
    else if (!strcmp(key, "GEM")) add_operand(b, mk(N_GEMLVL, op, v, 0, 0));
    else if (!strcmp(key, "ED")) add_operand(b, mk(N_ED, op, v, 0, 0));
    else if (!strcmp(key, "DEF")) add_operand(b, mk(N_STAT, op, v, S_DEFENSE, 0));
    else if (!strcmp(key, "MAXDUR")) add_operand(b, mk(N_MAXDUR, op, v, 0, 0));
    else if (!strcmp(key, "RES")) add_operand(b, mk(N_RES, op, v, 0, 0));
    else if (!strcmp(key, "FRES")) add_operand(b, mk(N_STAT, op, v, S_FIRERES, 0));
    else if (!strcmp(key, "CRES")) add_operand(b, mk(N_STAT, op, v, S_COLDRES, 0));
    else if (!strcmp(key, "LRES")) add_operand(b, mk(N_STAT, op, v, S_LIGHTRES, 0));
    else if (!strcmp(key, "PRES")) add_operand(b, mk(N_STAT, op, v, S_POISRES, 0));
    else if (!strcmp(key, "IAS")) add_operand(b, mk(N_STAT, op, v, S_IAS, 0));
    else if (!strcmp(key, "FCR")) add_operand(b, mk(N_STAT, op, v, S_FCR, 0));
    else if (!strcmp(key, "FHR")) add_operand(b, mk(N_STAT, op, v, S_FHR, 0));
    else if (!strcmp(key, "FBR")) add_operand(b, mk(N_STAT, op, v, S_FBR, 0));
    else if (!strcmp(key, "LIFE")) add_operand(b, mk(N_STAT, op, v * 256u, S_MAXHP, 0));
    else if (!strcmp(key, "MANA")) add_operand(b, mk(N_STAT, op, v * 256u, S_MAXMANA, 0));
    else if (!strcmp(key, "GOODSK")) add_operand(b, mk(N_GOODCLS, op, v, 0, 0));
    else if (!strcmp(key, "GOODTBSK")) add_operand(b, mk(N_GOODTAB, op, v, 0, 0));
    else if (!strcmp(key, "FOOLS")) add_operand(b, mk(N_FOOLS, 0, 0, 0, 0));
    else if (!strcmp(key, "LVLREQ")) add_operand(b, mk(N_LVLREQ, op, v, 0, 0));
    else if (!strcmp(key, "ARPER")) add_operand(b, mk(N_STAT, op, v, S_TOHITPCT, 0));
    else if (!strcmp(key, "MFIND")) add_operand(b, mk(N_STAT, op, v, S_MF, 0));
    else if (!strcmp(key, "GFIND")) add_operand(b, mk(N_STAT, op, v, S_GF, 0));
    else if (!strcmp(key, "STR")) add_operand(b, mk(N_STAT, op, v, S_STR, 0));
    else if (!strcmp(key, "DEX")) add_operand(b, mk(N_STAT, op, v, S_DEX, 0));
    else if (!strcmp(key, "FRW")) add_operand(b, mk(N_STAT, op, v, S_FRW, 0));
    else if (!strcmp(key, "MINDMG")) add_operand(b, mk(N_STAT, op, v, S_MINDMG, 0));
    else if (!strcmp(key, "MAXDMG")) add_operand(b, mk(N_STAT, op, v, S_MAXDMG, 0));
    else if (!strcmp(key, "AR")) add_operand(b, mk(N_STAT, op, v, S_ATTACKRATING, 0));
    else if (!strcmp(key, "DTM")) add_operand(b, mk(N_STAT, op, v, S_DTM, 0));
    else if (!strcmp(key, "MAEK")) add_operand(b, mk(N_STAT, op, v, S_MAEK, 0));
    else if (!strcmp(key, "REPLIFE")) add_operand(b, mk(N_STAT, op, v, S_REPLIFE, 0));
    else if (!strcmp(key, "REPQUANT")) add_operand(b, mk(N_STAT, op, v, S_REPQUANT, 0));
    else if (!strcmp(key, "REPAIR")) add_operand(b, mk(N_STAT, op, v, S_REPAIR, 0));
    else if (!strcmp(key, "ARMOR")) add_operand(b, mk(N_GROUP, 0, FI_G_ALLARMOR, 0, 0));
    else if (!strcmp(key, "BELT")) add_operand(b, mk(N_GROUP, 0, FI_G_BELT, 0, 0));
    else if (!strcmp(key, "CHEST")) add_operand(b, mk(N_GROUP, 0, FI_G_ARMOR, 0, 0));
    else if (!strcmp(key, "HELM")) add_operand(b, mk(N_GROUP, 0, FI_G_HELM, 0, 0));
    else if (!strcmp(key, "SHIELD")) add_operand(b, mk(N_GROUP, 0, FI_G_SHIELD, 0, 0));
    else if (!strcmp(key, "GLOVES")) add_operand(b, mk(N_GROUP, 0, FI_G_GLOVES, 0, 0));
    else if (!strcmp(key, "BOOTS")) add_operand(b, mk(N_GROUP, 0, FI_G_BOOTS, 0, 0));
    else if (!strcmp(key, "CIRC")) add_operand(b, mk(N_GROUP, 0, FI_G_CIRCLET, 0, 0));
    else if (!strcmp(key, "DRU")) add_operand(b, mk(N_GROUP, 0, FI_G_DRUID, 0, 0));
    else if (!strcmp(key, "BAR")) add_operand(b, mk(N_GROUP, 0, FI_G_BARB, 0, 0));
    else if (!strcmp(key, "DIN")) add_operand(b, mk(N_GROUP, 0, FI_G_PALADIN, 0, 0));
    else if (!strcmp(key, "NEC")) add_operand(b, mk(N_GROUP, 0, FI_G_NECRO, 0, 0));
    else if (!strcmp(key, "SIN")) add_operand(b, mk(N_GROUP, 0, FI_G_KATAR, 0, 0));
    else if (!strcmp(key, "SOR")) add_operand(b, mk(N_GROUP, 0, FI_G_ORB, 0, 0));
    else if (!strcmp(key, "ZON")) add_operand(b, mk(N_GROUP, 0, FI_G_AMAZON, 0, 0));
    else if (!strcmp(key, "AXE")) add_operand(b, mk(N_GROUP, 0, FI_G_AXE, 0, 0));
    else if (!strcmp(key, "MACE")) add_operand(b, mk(N_GROUP, 0, FI_G_MACE, 0, 0));
    else if (!strcmp(key, "SWORD")) add_operand(b, mk(N_GROUP, 0, FI_G_SWORD, 0, 0));
    else if (!strcmp(key, "DAGGER")) add_operand(b, mk(N_GROUP, 0, FI_G_DAGGER, 0, 0));
    else if (!strcmp(key, "THROWING")) add_operand(b, mk(N_GROUP, 0, FI_G_THROWING, 0, 0));
    else if (!strcmp(key, "JAV")) add_operand(b, mk(N_GROUP, 0, FI_G_THROWING | FI_G_SPEAR, 0, 0));
    else if (!strcmp(key, "SPEAR")) add_operand(b, mk(N_GROUP, 0, FI_G_SPEAR, 0, 0));
    else if (!strcmp(key, "POLEARM")) add_operand(b, mk(N_GROUP, 0, FI_G_POLEARM, 0, 0));
    else if (!strcmp(key, "BOW")) add_operand(b, mk(N_GROUP, 0, FI_G_BOW, 0, 0));
    else if (!strcmp(key, "XBOW")) add_operand(b, mk(N_GROUP, 0, FI_G_XBOW, 0, 0));
    else if (!strcmp(key, "STAFF")) add_operand(b, mk(N_GROUP, 0, FI_G_STAFF, 0, 0));
    else if (!strcmp(key, "WAND")) add_operand(b, mk(N_GROUP, 0, FI_G_WAND, 0, 0));
    else if (!strcmp(key, "SCEPTER")) add_operand(b, mk(N_GROUP, 0, FI_G_SCEPTER, 0, 0));
    else if (kl == 3 && starts(key, "EQ")) {
        static const unsigned g[] = {FI_G_HELM, FI_G_ARMOR, FI_G_SHIELD, FI_G_GLOVES, FI_G_BOOTS, FI_G_BELT, FI_G_CIRCLET};
        if (key[2] >= '1' && key[2] <= '7') add_operand(b, mk(N_GROUP, 0, g[key[2] - '1'], 0, 0)); else added = 0;
    } else if (kl == 3 && starts(key, "CL")) {
        static const unsigned g[] = {FI_G_DRUID, FI_G_BARB, FI_G_PALADIN, FI_G_NECRO, FI_G_KATAR, FI_G_ORB, FI_G_AMAZON};
        if (key[2] >= '1' && key[2] <= '7') add_operand(b, mk(N_GROUP, 0, g[key[2] - '1'], 0, 0)); else added = 0;
    } else if (!strcmp(key, "WEAPON")) add_operand(b, mk(N_GROUP, 0, FI_G_ALLWEAPON, 0, 0));
    else if (starts(key, "WP")) {
        unsigned g = 0;
        if (kl >= 3) {
            switch (key[2]) {
            case '1': g = kl >= 4 && key[3] == '0' ? FI_G_XBOW : kl >= 4 && key[3] == '1' ? FI_G_STAFF : kl >= 4 && key[3] == '2' ? FI_G_WAND : kl >= 4 && key[3] == '3' ? FI_G_SCEPTER : FI_G_AXE; break;
            case '2': g = FI_G_MACE; break; case '3': g = FI_G_SWORD; break; case '4': g = FI_G_DAGGER; break; case '5': g = FI_G_THROWING; break;
            case '6': g = FI_G_THROWING | FI_G_SPEAR; break; case '7': g = FI_G_SPEAR; break; case '8': g = FI_G_POLEARM; break; case '9': g = FI_G_BOW; break;
            }
        }
        if (g) add_operand(b, mk(N_GROUP, 0, g, 0, 0)); else added = 0;
    } else if (starts(key, "SK") || starts(key, "OS") || starts(key, "CHSK") || starts(key, "CLSK") || starts(key, "TABSK") || starts(key, "STAT") || starts(key, "CHARSTAT")) {
        int off = starts(key, "SK") || starts(key, "OS") ? 2 : starts(key, "CHSK") || starts(key, "CLSK") ? 4 : starts(key, "TABSK") ? 5 : starts(key, "STAT") ? 4 : 8;
        int num = whole_num(key + off);
        if (num < 0) added = 0;
        else if (starts(key, "SK")) { if (num > SKILL_MAX) added = 0; else add_operand(b, mk(N_STAT, op, v, S_SINGLESK, num)); }
        else if (starts(key, "OS")) { if (num > SKILL_MAX) added = 0; else add_operand(b, mk(N_STAT, op, v, S_NONCLASSSK, num)); }
        else if (starts(key, "CHSK")) { if (num > SKILL_MAX) added = 0; else add_operand(b, mk(N_CHARGED, op, v, num, 0)); }
        else if (starts(key, "CLSK")) { if (num >= 7) added = 0; else add_operand(b, mk(N_STAT, op, v, S_CLASSSK, num)); }
        else if (starts(key, "TABSK")) { if (num > SKILLTAB_MAX) added = 0; else add_operand(b, mk(N_STAT, op, v, S_SKILLTAB, num)); }
        else if (starts(key, "STAT")) { if (num > S_STATMAX) added = 0; else add_operand(b, mk(N_STAT, op, v, num, 0)); }
        else { if (num > S_STATMAX) added = 0; else add_operand(b, mk(N_CHARSTAT, op, v, num, 0)); }
    } else if (starts(key, "ALLSK")) add_operand(b, mk(N_STAT, op, v, S_ALLSK, 0));
    else if (starts(key, "MULTI")) {
        int s1, s2;
        if (sscanf(key + 5, "%d,%d", &s1, &s2) == 2) add_operand(b, mk(N_STAT, op, v, s1, s2)); else added = 0;
    } else if (!strcmp(key, "PRICE")) add_operand(b, mk(N_PRICE, op, v, 0, 0));
    else if (!strcmp(key, "XP")) add_operand(b, mk(N_PTYPE, 0, 1, 0, 0));
    else if (!strcmp(key, "CLASSIC")) add_operand(b, mk(N_PTYPE, 0, 0, 0, 0));
    else if (starts(key, "COUNT")) {
        // COUNT=n,cond,cond,...   (inside a sub-condition '|' stands for a space)
        char *tmp = dups(valstr), *save = tmp; int first = 1, need = 0, ok = 1; fi_node n = mk(N_COUNT, op, 0, 0, 0);
        for (;;) {
            char *c = strchr(save, ','); if (c) *c = 0;
            if (first) { need = atoi(save); if ((unsigned)need != value) { ok = 0; break; } first = 0; }
            else {
                char *t = save; for (; *t; t++) if (*t == '|') *t = ' ';
                n.subs = (fi_rule *)realloc(n.subs, (size_t)(n.nsubs + 1) * sizeof(fi_rule));
                parse_rule_text(b->set, save, &n.subs[n.nsubs++], 1);
            }
            if (!c) break;
            save = c + 1;
        }
        free(tmp);
        if (!ok || first) added = 0; else { n.v = (unsigned)need; add_operand(b, n); }
    } else added = 0;

    if (!added && token[0]) {
        b->set->unknown++;
        if (!b->set->firstbad[0]) snprintf(b->set->firstbad, sizeof b->set->firstbad, "%s", tok0);
    }
done:
    for (i = 0; i < nend; i++) add_nonoperand(b, endc[i]);
}

// shunting-yard, exactly as BH does it (AND/OR share one precedence level)
static void to_rpn(blist *in, fi_rule *r) {
    fi_node *out = (fi_node *)malloc(((size_t)in->n + 1) * sizeof *out), *st = (fi_node *)malloc(((size_t)in->n + 1) * sizeof *st);
    int no = 0, ns = 0, i;
    for (i = 0; i < in->n; i++) {
        fi_node *c = &in->v[i]; int t = ctype(c->type);
        if (t == CT_OPERAND) out[no++] = *c;
        else if (t == CT_BIN || t == CT_NOT) {
            if (t == CT_BIN) while (ns > 0 && (ctype(st[ns - 1].type) == CT_NOT || ctype(st[ns - 1].type) == CT_BIN)) out[no++] = st[--ns];
            st[ns++] = *c;
        } else if (t == CT_LP) st[ns++] = *c;
        else if (t == CT_RP) {
            int found = 0;
            while (ns > 0) { fi_node s = st[--ns]; if (s.type == N_LP) { found = 1; break; } out[no++] = s; }
            if (!found) { r->rpn = out; r->n = no; free(st); return; }
        }
    }
    while (ns > 0) { fi_node s = st[--ns]; if (s.type == N_LP || s.type == N_RP) break; out[no++] = s; }
    free(st); r->rpn = out; r->n = no;
}

static const char *group_expand(fi_set *s, const char *tok) {
    for (int i = 0; i < s->ngroups; i++) if (!strcmp(s->groups[i].k, tok)) return s->groups[i].v;
    return NULL;
}
static void parse_rule_text(fi_set *s, const char *cond, fi_rule *out, int is_sub) {
    blist b; memset(&b, 0, sizeof b); b.set = s; b.last = CT_NONE;
    char *tmp = dups(cond), *p = tmp; (void)is_sub;
    for (;;) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *e = p; while (*e && !isspace((unsigned char)*e)) e++;
        char save = *e; *e = 0;
        const char *g = group_expand(s, p);
        if (g) {
            build_token(&b, "(");
            char *gt = dups(g), *q = gt;
            for (;;) { while (*q && isspace((unsigned char)*q)) q++; if (!*q) break; char *ge = q; while (*ge && !isspace((unsigned char)*ge)) ge++; char sv = *ge; *ge = 0; build_token(&b, q); *ge = sv; q = ge; }
            free(gt); build_token(&b, ")");
        } else build_token(&b, p);
        *e = save; p = e;
    }
    free(tmp);
    memset(out, 0, sizeof *out);
    to_rpn(&b, out); free(b.v);
}

// ---- actions ------------------------------------------------------------------------------------------------------------
static void upcase_macros(char *name) {                // text between % % that contains lowercase letters is upper-cased
    for (char *p = name; *p; p++) {
        if (*p != '%') continue;
        char *e = strchr(p + 1, '%'); if (!e) break;
        int word = 1, low = 0; for (char *q = p + 1; q < e; q++) { if (!(isalnum((unsigned char)*q) || *q == '_' || *q == '-')) word = 0; if (islower((unsigned char)*q)) low = 1; }
        if (word && low) for (char *q = p + 1; q < e; q++) *q = (char)toupper((unsigned char)*q);
        p = e;
    }
}
static void cut(char *s, size_t pos, size_t len) { memmove(s + pos, s + pos + len, strlen(s + pos + len) + 1); }
// removes the first %KEY-<hex>% (hex 1..4 digits) and returns the value, or `def`
static int take_hex(char *name, const char *key, int def) {
    char pat[32]; snprintf(pat, sizeof pat, "%%%s-", key); size_t pl = strlen(pat);
    for (char *p = name; *p; p++) {
        if (*p != '%' || ci_ncmp(p, pat, pl)) continue;
        char *q = p + pl; int n = 0; unsigned v = 0;
        while (n < 4 && isxdigit((unsigned char)q[n])) { v = v * 16 + (unsigned)(isdigit((unsigned char)q[n]) ? q[n] - '0' : (tolower((unsigned char)q[n]) - 'a' + 10)); n++; }
        if (n >= 1 && q[n] == '%') { cut(name, (size_t)(p - name), pl + (size_t)n + 1); return (int)v; }
    }
    return def;
}
static int take_tier(char *name) {
    for (char *p = name; *p; p++) if (*p == '%' && !ci_ncmp(p, "%TIER-", 6) && isdigit((unsigned char)p[6]) && p[7] == '%') { int v = p[6] - '0'; cut(name, (size_t)(p - name), 8); return v; }
    return 0;
}
static int take_word(char *name, const char *w) { char *p = strstr(name, w); if (!p) return 0; cut(name, (size_t)(p - name), strlen(w)); return 1; }

static void build_action(fi_action *a, const char *text) {
    char *name = (char *)malloc(strlen(text) + 1); strcpy(name, text);
    memset(a, 0, sizeof *a); a->stop = 1;
    upcase_macros(name);
    a->border = take_hex(name, "BORDER", FI_UNDEF); a->map = take_hex(name, "MAP", FI_UNDEF); a->dot = take_hex(name, "DOT", FI_UNDEF);
    a->px = take_hex(name, "PX", FI_UNDEF); a->line = take_hex(name, "LINE", FI_UNDEF); a->notify = take_hex(name, "NOTIFY", FI_UNDEF);
    a->ping = take_tier(name);
    char *l = strchr(name, '{'), *r = strchr(name, '}');
    if (l && r && l <= r) { a->desc = dupn(l + 1, (size_t)(r - l - 1)); cut(name, (size_t)(l - name), (size_t)(r - l + 1)); } else a->desc = dups("");
    if (take_word(name, "%NOTRACK%")) a->notrack = 1;
    char *m = strstr(name, "%MAP%");                      // legacy: the last colour name before it picks the map colour
    if (m) {
        int mc = 0x20; size_t last = 0;
        for (int i = 0; i < NCOLORS; i++) { char k[24]; snprintf(k, sizeof k, "%%%s%%", colors[i].k); char *p = strstr(name, k); if (p && p < m && (size_t)(p - name) >= last) { mc = colors[i].map; last = (size_t)(p - name); } }
        cut(name, (size_t)(m - name), 5); a->map = mc; if (a->border == FI_UNDEF) a->border = mc;
    }
    if (take_word(name, "%CONTINUE%")) a->stop = 0;
    a->name = name;
}
static int visible_text(const char *s) {              // text left once colour macros and spaces are removed
    char *t = dups(s); char k[24]; char *p;
    for (int i = 0; i < NCOLORS; i++) { snprintf(k, sizeof k, "%%%s%%", colors[i].k); while ((p = strstr(t, k))) cut(t, (size_t)(p - t), strlen(k)); }
    int n = 0; for (p = t; *p; p++) if (*p != ' ') n++;
    free(t); return n > 0;
}

// ---- evaluation -----------------------------------------------------------------------------------------------------------
static int st(const fi_item *it, int stat, int layer) { return it->stat ? it->stat(it->ctx, stat, layer) : 0; }
static unsigned char affix_level(unsigned char ilvl, unsigned char qlvl, unsigned char mlvl) {
    if (ilvl > 99) ilvl = 99;
    if (qlvl > ilvl) ilvl = qlvl;
    if (mlvl > 0) return (unsigned char)(ilvl + mlvl > 99 ? 99 : ilvl + mlvl);
    return (unsigned char)(ilvl < 99 - qlvl / 2 ? ilvl - qlvl / 2 : ilvl * 2 - 99);
}
static int rule_eval(fi_set *s, const fi_rule *r, const fi_item *it);
static int node_eval(fi_set *s, const fi_node *n, const fi_item *it) {
    switch (n->type) {
    case N_CODE: return it->code[0] == n->code[0] && it->code[1] == n->code[1] && it->code[2] == n->code[2];
    case N_QID: return it->fileindex == n->a && it->quality == (int)n->v;
    case N_FLAG: return (it->flags & n->v) != 0;
    case N_QUAL: return it->quality == (int)n->v;
    case N_NMAG: return it->quality == 1 || it->quality == 2 || it->quality == 3;
    case N_GEMLVL: return it->gem_level > 0 && cmp_u((unsigned)it->gem_level, n->op, n->v);
    case N_GEMTYPE: return it->gem_type > 0 && cmp_u((unsigned)it->gem_type, n->op, n->v);
    case N_RUNE: return it->rune_no > 0 && cmp_u((unsigned)it->rune_no, n->op, n->v);
    case N_FALSE: return 0;
    case N_ILVL: return cmp_u((unsigned)it->ilvl, n->op, n->v);
    case N_QLVL: return cmp_u((unsigned char)it->qlvl, n->op, n->v);
    case N_ALVL: return cmp_u(affix_level((unsigned char)it->ilvl, (unsigned char)it->qlvl, (unsigned char)it->magiclvl), n->op, n->v);
    case N_CRAFTALVL: return cmp_u(affix_level((unsigned char)(it->ilvl / 2 + it->clvl / 2), (unsigned char)it->qlvl, (unsigned char)it->magiclvl), n->op, n->v);
    case N_LVLREQ: return cmp_u((unsigned)it->lvlreq, n->op, n->v);
    case N_GROUP: return (it->groups & n->v) != 0;
    case N_STAT: return cmp_u((unsigned)st(it, n->a, n->b), n->op, n->v);
    case N_CHARSTAT: return cmp_u((unsigned)(it->pstat ? it->pstat(it->ctx, n->a, n->b) : 0), n->op, n->v);
    case N_CLVL: return cmp_u((unsigned)it->clvl, n->op, n->v);
    case N_FOOLS: return st(it, FI_STAT_FOOLS, 0) == 3;
    case N_ED: return cmp_u((unsigned)st(it, FI_STAT_ED, 0), n->op, n->v);
    case N_MAXDUR: return cmp_u((unsigned)st(it, FI_STAT_MAXDUR, 0), n->op, n->v);
    case N_GOODCLS: { int sum = 0; for (int i = 0; i < s->ngoodcls; i++) sum += st(it, S_CLASSSK, s->goodcls[i]); return cmp_u((unsigned)sum, n->op, n->v); }
    case N_GOODTAB: { int sum = 0; for (int i = 0; i < s->ngoodtab; i++) sum += st(it, S_SKILLTAB, s->goodtab[i]); return cmp_u((unsigned)sum, n->op, n->v); }
    case N_CHARGED: return cmp_u((unsigned)st(it, FI_STAT_CHARGE, n->a), n->op, n->v);
    case N_DIFF: return cmp_u((unsigned)it->difficulty, n->op, n->v);
    case N_FILT: return cmp_u((unsigned)it->filter_level, n->op, n->v);
    case N_PRICE: return cmp_u((unsigned)it->price, n->op, n->v);
    case N_PTYPE: return it->expansion == (int)n->v;
    case N_GOLD: return it->code[0] == 'g' && it->code[1] == 'l' && it->code[2] == 'd' && cmp_u((unsigned)st(it, 14, 0), n->op, n->v);   // size of a gold pile (item stat 14); BH reads it from the packet
    case N_RES: return cmp_u((unsigned)st(it, S_FIRERES, 0), n->op, n->v) && cmp_u((unsigned)st(it, S_LIGHTRES, 0), n->op, n->v) &&
                       cmp_u((unsigned)st(it, S_COLDRES, 0), n->op, n->v) && cmp_u((unsigned)st(it, S_POISRES, 0), n->op, n->v);
    case N_ADD: { int sum = 0; for (int i = 0; i < n->nstats; i++) { int t = st(it, n->stats[i], 0); if (n->stats[i] == S_MAXHP || n->stats[i] == S_MAXMANA) t /= 256; sum += t; } return cmp_u((unsigned)sum, n->op, n->v); }
    case N_COUNT: { int c = 0; for (int i = 0; i < n->nsubs; i++) if (rule_eval(s, &n->subs[i], it)) c++; return cmp_u((unsigned)c, n->op, n->v); }
    }
    return 0;
}
static int rule_eval(fi_set *s, const fi_rule *r, const fi_item *it) {
    if (r->dead) return 0;
    if (r->n == 0) return 1;
    int stack[64], sp = 0;
    for (int i = 0; i < r->n; i++) {
        const fi_node *c = &r->rpn[i]; int t = ctype(c->type);
        if (t == CT_OPERAND) { if (sp >= 64) return 0; stack[sp++] = node_eval(s, c, it); continue; }
        if (sp < 1) return 0;
        int a1 = stack[--sp], a2 = 0;
        if (t == CT_BIN) { if (sp < 1) return 0; a2 = stack[--sp]; }
        stack[sp++] = c->type == N_AND ? (a1 && a2) : c->type == N_OR ? (a1 || a2) : !a1;
    }
    return sp == 1 ? stack[0] : 0;
}

// ---- name building --------------------------------------------------------------------------------------------------------
static void replace_all(char *buf, size_t cap, const char *key, const char *val) {
    char pat[64]; snprintf(pat, sizeof pat, "%%%s%%", key); size_t pl = strlen(pat), vl = strlen(val);
    char *p = buf;
    while ((p = strstr(p, pat))) {
        size_t tail = strlen(p + pl);
        if (strlen(buf) - pl + vl + 1 > cap) return;
        memmove(p + vl, p + pl, tail + 1); memcpy(p, val, vl); p += vl;
    }
}
static void substitute(fi_set *s, const fi_item *it, char *name, size_t cap, const char *tmpl) {
    char orig[512]; snprintf(orig, sizeof orig, "%s", name);
    char sockets[16], ilvl[16], alvl[16], calvl[16], lvlreq[16], wpnspd[16], range[16], price[16] = "", runenum[16], runename[64] = "", gl[24] = "", gt[24] = "", code[4];
    memcpy(code, it->code, 3); code[3] = 0;
    snprintf(sockets, sizeof sockets, "%d", st(it, S_SOCKETS, 0)); snprintf(ilvl, sizeof ilvl, "%d", it->ilvl);
    snprintf(alvl, sizeof alvl, "%d", affix_level((unsigned char)it->ilvl, (unsigned char)it->qlvl, (unsigned char)it->magiclvl));
    snprintf(calvl, sizeof calvl, "%d", affix_level((unsigned char)(it->ilvl / 2 + it->clvl / 2), (unsigned char)it->qlvl, (unsigned char)it->magiclvl));
    snprintf(lvlreq, sizeof lvlreq, "%d", it->lvlreq); snprintf(wpnspd, sizeof wpnspd, "%d", it->wpnspd); snprintf(range, sizeof range, "%d", it->rangeadder);
    if (it->price >= 0) snprintf(price, sizeof price, "%d", it->price);
    snprintf(runenum, sizeof runenum, "%d", it->rune_no);
    if (it->rune_no > 0) { size_t k = strcspn(orig, " "); snprintf(runename, sizeof runename, "%.*s", (int)(k < 63 ? k : 63), orig); }
    else if (it->gem_level > 0) { snprintf(gl, sizeof gl, "%s", GEMLV[it->gem_level < 6 ? it->gem_level : 0]); snprintf(gt, sizeof gt, "%s", GEMTY[it->gem_type > 0 && it->gem_type < 8 ? it->gem_type : 0]); }
    snprintf(name, cap, "%s", tmpl);
    replace_all(name, cap, "NAME", orig); replace_all(name, cap, "BASENAME", it->basename ? it->basename : "");
    replace_all(name, cap, "SOCKETS", sockets); replace_all(name, cap, "RUNENUM", runenum); replace_all(name, cap, "RUNENAME", runename);
    replace_all(name, cap, "GEMLEVEL", gl); replace_all(name, cap, "GEMTYPE", gt); replace_all(name, cap, "ILVL", ilvl); replace_all(name, cap, "ALVL", alvl);
    replace_all(name, cap, "CRAFTALVL", calvl); replace_all(name, cap, "LVLREQ", lvlreq); replace_all(name, cap, "WPNSPD", wpnspd); replace_all(name, cap, "RANGE", range);
    replace_all(name, cap, "CODE", code); replace_all(name, cap, "NL", "\n"); replace_all(name, cap, "PRICE", price);
    for (int i = 0; i < NCOLORS; i++) {
        const char *v = colors[i].chat;
        if (!s->glide) { if (!strcmp(colors[i].k, "CORAL")) v = "\xFF" "c1"; else if (!strcmp(colors[i].k, "SAGE")) v = "\xFF" "c2"; else if (!strcmp(colors[i].k, "TEAL")) v = "\xFF" "c3"; else if (!strcmp(colors[i].k, "LIGHT_GRAY")) v = "\xFF" "c5"; }
        replace_all(name, cap, colors[i].k, v);
    }
    char *p;
    while ((p = strstr(name, "%STAT-")) && isdigit((unsigned char)p[6])) {
        char *e = p + 6; int num = 0, nd = 0; while (isdigit((unsigned char)*e) && nd < 4) { num = num * 10 + (*e - '0'); e++; nd++; }
        if (*e != '%') break;
        char val[16] = ""; if (num <= S_STATMAX) { int v = st(it, num, 0); if (num == 7 || num == 9) v /= 256; snprintf(val, sizeof val, "%d", v); }
        size_t pl = (size_t)(e - p) + 1, vl = strlen(val);
        memmove(p + vl, p + pl, strlen(p + pl) + 1); memcpy(p, val, vl);
    }
}

int fi_is_blocked(fi_set *s, const fi_item *it) {
    int ign = 0;
    for (int i = 0; i < s->nign && !ign; i++) if (rule_eval(s, &s->rules[s->ign_r[i]], it)) ign = 1;
    if (!ign) return 0;
    for (int i = 0; i < s->nmap; i++) if (rule_eval(s, &s->rules[s->map_r[i]], it)) return 0;      // a map action keeps it visible
    for (int i = 0; i < s->nwhite; i++) if (rule_eval(s, &s->rules[s->white_r[i]], it)) return 0;
    return 1;
}
int fi_name(fi_set *s, const fi_item *it, const char *name, char *out, size_t cap, int *blocked) {
    char cur[512]; snprintf(cur, sizeof cur, "%s", name);
    for (int i = 0; i < s->nname; i++) {
        const fi_rule *r = &s->rules[s->name_r[i]];
        if (rule_eval(s, r, it)) { substitute(s, it, cur, sizeof cur, r->act.name); if (r->act.stop) break; }
    }
    int b = fi_is_blocked(s, it); if (blocked) *blocked = b;
    if (b) { size_t l = strlen(cur); if (l + 11 < sizeof cur) strcpy(cur + l, " [blocked]"); }
    snprintf(out, cap, "%s", cur);                        // newlines stay: the game shows the base name on its own line
    return (int)strlen(out);
}
int fi_desc(fi_set *s, const fi_item *it, const char *name, char *out, size_t cap) {
    char cur[512] = ""; (void)name;
    for (int i = 0; i < s->ndesc; i++) {
        const fi_rule *r = &s->rules[s->desc_r[i]];
        if (rule_eval(s, r, it)) { char t[512]; snprintf(t, sizeof t, "%s", cur); substitute(s, it, t, sizeof t, r->act.desc); snprintf(cur, sizeof cur, "%s", t); if (r->act.stop) break; }
    }
    snprintf(out, cap, "%s", cur); return (int)strlen(out);
}
int fi_actions(fi_set *s, const fi_item *it, fi_action *out, int max) {
    int n = 0;
    for (int i = 0; i < s->nmap && n < max; i++) { const fi_rule *r = &s->rules[s->map_r[i]]; if (rule_eval(s, r, it)) out[n++] = r->act; }
    return n;
}

// ---- loading ------------------------------------------------------------------------------------------------------------
static void addidx(int **a, int *n, int v) { *a = (int *)realloc(*a, (size_t)(*n + 1) * sizeof(int)); (*a)[(*n)++] = v; }
static int is_true(const char *v) { return !ci_ncmp(v, "true", 4) || !ci_ncmp(v, "1", 1) || !ci_ncmp(v, "yes", 3); }

fi_set *fi_parse(const char *text, size_t len, int *unknown, char *firstbad, size_t firstbad_cap) {
    fi_set *s = (fi_set *)calloc(1, sizeof *s);
    s->glide = 1;
    // two passes: ConditionGroup lines first (rules may use them before they are defined further down), then everything else
    for (int pass = 0; pass < 2; pass++) {
        size_t i = 0;
        while (i < len) {
            size_t e = i; while (e < len && text[e] != '\n') e++;
            char *line = dupn(text + i, e - i); i = e + 1;
            char *cm = strstr(line, "//"); if (cm) *cm = 0;
            char *colon = strchr(line, ':');
            if (!colon || !*line) { free(line); continue; }
            *colon = 0; char *key = line, *val = colon + 1; trim(key); trim(val);
            char *lb = strchr(key, '['), *rb = strrchr(key, ']');
            if (!lb || !rb || rb < lb) { free(line); continue; }
            *lb = 0; *rb = 0; char *inner = lb + 1; trim(key);
            if (pass == 0 && !strcmp(key, "ConditionGroup")) {
                s->groups = realloc(s->groups, (size_t)(s->ngroups + 1) * sizeof *s->groups);
                char *k = dups(inner); trim(k); s->groups[s->ngroups].k = k; s->groups[s->ngroups].v = dups(val); s->ngroups++;
            } else if (pass == 1 && !strcmp(key, "ClassSkillsList")) { if (is_true(val)) addidx(&s->goodcls, &s->ngoodcls, atoi(inner)); }
            else if (pass == 1 && !strcmp(key, "TabSkillsList")) { if (is_true(val)) addidx(&s->goodtab, &s->ngoodtab, atoi(inner)); }
            else if (pass == 1 && !strcmp(key, "ItemDisplay")) {
                if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 256; s->rules = (fi_rule *)realloc(s->rules, (size_t)s->cap * sizeof *s->rules); }
                fi_rule *r = &s->rules[s->n]; parse_rule_text(s, inner, r, 0); build_action(&r->act, val);
                { const char *q = inner; while (*q && isspace((unsigned char)*q)) q++; if (*q && r->n == 0) r->dead = 1; }   // only unrecognised tokens: match nothing, not everything
                int idx = s->n++;
                const fi_action *a = &r->act;
                int has_desc = visible_text(a->desc), has_name = visible_text(a->name);
                int has_map = a->map != FI_UNDEF || a->border != FI_UNDEF || a->dot != FI_UNDEF || a->px != FI_UNDEF || a->line != FI_UNDEF;
                if (has_desc) addidx(&s->desc_r, &s->ndesc, idx);
                if (has_map) addidx(&s->map_r, &s->nmap, idx);
                if (has_name) { addidx(&s->name_r, &s->nname, idx); if (a->stop && !has_map) addidx(&s->white_r, &s->nwhite, idx); }
                if (!has_map && !has_name && !has_desc && a->stop) addidx(&s->ign_r, &s->nign, idx);
            }
            free(line);
        }
    }
    if (unknown) *unknown = s->unknown;
    if (firstbad && firstbad_cap) snprintf(firstbad, firstbad_cap, "%s", s->firstbad);
    return s;
}
static void free_rule(fi_rule *r) {
    for (int i = 0; i < r->n; i++) { free(r->rpn[i].stats); for (int j = 0; j < r->rpn[i].nsubs; j++) free_rule(&r->rpn[i].subs[j]); free(r->rpn[i].subs); }
    free(r->rpn); free(r->act.name); free(r->act.desc);
}
void fi_free(fi_set *s) {
    if (!s) return;
    for (int i = 0; i < s->n; i++) free_rule(&s->rules[i]);
    for (int i = 0; i < s->ngroups; i++) { free(s->groups[i].k); free(s->groups[i].v); }
    free(s->rules); free(s->groups); free(s->name_r); free(s->desc_r); free(s->map_r); free(s->ign_r); free(s->white_r); free(s->goodcls); free(s->goodtab); free(s);
}
int fi_rule_count(const fi_set *s) { return s ? s->n : 0; }
void fi_set_video(fi_set *s, int glide) { if (s) s->glide = glide; }
