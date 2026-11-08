#include "itemdb.h"
#include "filter.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXT 400
#define MAXI 2500
static struct { char code[8]; char eq1[8], eq2[8]; } types[MAXT]; static int ntypes;
static itemdb_t items[MAXI]; static int nitems;
static char itype[MAXI][8];                     // each item's type code
static char normc[MAXI][4], uberc[MAXI][4], ultrac[MAXI][4];

void itemdb_reset(void) { ntypes = nitems = 0; }
int itemdb_count(void) { return nitems; }

// ---- tiny TSV reader: header row gives column names ----
typedef struct { const char *p, *end; const char *cols[160]; int ncols; } tsv;
static int line_cells(const char *s, const char *e, const char **cell, int *clen, int max) {
    int n = 0; const char *c = s;
    for (const char *q = s; ; q++) {
        if (q == e || *q == '\t') { if (n < max) { cell[n] = c; clen[n] = (int)(q - c); n++; } c = q + 1; if (q == e) break; }
    }
    return n;
}
static int find_col(const char *hdr, const char *hend, const char *name) {
    const char *cell[160]; int clen[160]; int n = line_cells(hdr, hend, cell, clen, 160);
    for (int i = 0; i < n; i++) if ((int)strlen(name) == clen[i] && !strncmp(cell[i], name, (size_t)clen[i])) return i;
    return -1;
}
static void cellcopy(char *dst, size_t cap, const char *c, int l) { if (l >= (int)cap) l = (int)cap - 1; if (l < 0) l = 0; memcpy(dst, c, (size_t)l); dst[l] = 0; while (l > 0 && (dst[l - 1] == '\r' || dst[l - 1] == ' ')) dst[--l] = 0; }
static int cellint(const char *c, int l) { char b[24]; cellcopy(b, sizeof b, c, l); return atoi(b); }

int itemdb_parse_types(const char *txt, int len) {
    const char *end = txt + len, *p = txt;
    const char *he = memchr(p, '\n', (size_t)len); if (!he) return 0;
    int cc = find_col(p, he, "Code"), c1 = find_col(p, he, "Equiv1"), c2 = find_col(p, he, "Equiv2");
    if (cc < 0) return 0;
    ntypes = 0; p = he + 1;
    while (p < end && ntypes < MAXT) {
        const char *e = memchr(p, '\n', (size_t)(end - p)); if (!e) e = end;
        const char *cell[160]; int clen[160]; int n = line_cells(p, e, cell, clen, 160);
        if (n > cc && clen[cc] > 0) {
            cellcopy(types[ntypes].code, sizeof types[0].code, cell[cc], clen[cc]);
            types[ntypes].eq1[0] = types[ntypes].eq2[0] = 0;
            if (c1 >= 0 && n > c1) cellcopy(types[ntypes].eq1, sizeof types[0].eq1, cell[c1], clen[c1]);
            if (c2 >= 0 && n > c2) cellcopy(types[ntypes].eq2, sizeof types[0].eq2, cell[c2], clen[c2]);
            ntypes++;
        }
        p = e + 1;
    }
    return ntypes;
}
static int isa(const char *t, const char *want, int depth) {
    if (!*t || depth > 8) return 0;
    if (!strcmp(t, want)) return 1;
    for (int i = 0; i < ntypes; i++) if (!strcmp(types[i].code, t)) return isa(types[i].eq1, want, depth + 1) || isa(types[i].eq2, want, depth + 1);
    return 0;
}
static unsigned groups_of(const char *t) {
    static const struct { const char *ty; unsigned bit; } m[] = {
        {"helm", FI_G_HELM}, {"tors", FI_G_ARMOR}, {"shld", FI_G_SHIELD}, {"glov", FI_G_GLOVES}, {"boot", FI_G_BOOTS}, {"belt", FI_G_BELT},
        {"pelt", FI_G_DRUID}, {"phlm", FI_G_BARB}, {"ashd", FI_G_PALADIN}, {"head", FI_G_NECRO}, {"circ", FI_G_CIRCLET},
        {"axe", FI_G_AXE}, {"mace", FI_G_MACE}, {"swor", FI_G_SWORD}, {"knif", FI_G_DAGGER}, {"tkni", FI_G_THROWING}, {"taxe", FI_G_THROWING},
        {"jave", FI_G_JAVELIN}, {"spea", FI_G_SPEAR}, {"pole", FI_G_POLEARM}, {"bow", FI_G_BOW}, {"xbow", FI_G_XBOW}, {"staf", FI_G_STAFF},
        {"wand", FI_G_WAND}, {"scep", FI_G_SCEPTER}, {"h2h", FI_G_KATAR}, {"h2h2", FI_G_KATAR}, {"orb", FI_G_ORB}, {"abow", FI_G_AMAZON},
        {"aspe", FI_G_AMAZON}, {"ajav", FI_G_AMAZON}, {"armo", FI_G_ALLARMOR}, {"weap", FI_G_ALLWEAPON} };
    unsigned g = 0;
    for (size_t i = 0; i < sizeof m / sizeof *m; i++) if (isa(t, m[i].ty, 0)) g |= m[i].bit;
    if (g & (FI_G_HELM | FI_G_ARMOR | FI_G_SHIELD | FI_G_GLOVES | FI_G_BOOTS | FI_G_BELT | FI_G_DRUID | FI_G_BARB | FI_G_PALADIN | FI_G_NECRO | FI_G_CIRCLET)) g |= FI_G_ALLARMOR;
    if (g & (FI_G_AXE | FI_G_MACE | FI_G_SWORD | FI_G_DAGGER | FI_G_THROWING | FI_G_JAVELIN | FI_G_SPEAR | FI_G_POLEARM | FI_G_BOW | FI_G_XBOW | FI_G_STAFF | FI_G_WAND | FI_G_SCEPTER | FI_G_KATAR | FI_G_ORB | FI_G_AMAZON)) g |= FI_G_ALLWEAPON;
    return g;
}
static void gem_rune(itemdb_t *it) {
    const char *c = it->code;
    if (c[0] == 'r' && isdigit((unsigned char)c[1]) && isdigit((unsigned char)c[2])) it->rune_no = (c[1] - '0') * 10 + (c[2] - '0');
    else if ((c[0] == 'g' && strchr("cfslzp", c[1])) || (c[0] == 's' && c[1] == 'k' && strchr("cfulz", c[2]))) {
        int lv; char ty;
        if (c[0] == 'g') { lv = c[1] == 'c' ? 1 : c[1] == 'f' ? 2 : c[1] == 's' ? 3 : c[1] == 'l' ? 4 : 5; ty = c[2]; }
        else { lv = c[2] == 'c' ? 1 : c[2] == 'f' ? 2 : c[2] == 'u' ? 3 : c[2] == 'l' ? 4 : 5; ty = 'k'; }
        int t = ty == 'v' ? 1 : ty == 'w' ? 2 : ty == 'g' ? 3 : ty == 'r' ? 4 : ty == 'b' ? 5 : ty == 'y' ? 6 : ty == 'k' ? 7 : 0;
        if (t) { it->gem_level = lv; it->gem_type = t; }
    }
}
int itemdb_parse_items(const char *txt, int len, int kind) {
    const char *end = txt + len, *p = txt;
    const char *he = memchr(p, '\n', (size_t)len); if (!he) return 0;
    int cc = find_col(p, he, "code"), ct = find_col(p, he, "type"), cl = find_col(p, he, "level"), cr = find_col(p, he, "levelreq"),
        cm = find_col(p, he, "magic lvl"), cs = find_col(p, he, "speed"), cra = find_col(p, he, "rangeadder"), cq = find_col(p, he, "quest"),
        cn = find_col(p, he, "normcode"), cu = find_col(p, he, "ubercode"), cx = find_col(p, he, "ultracode");
    if (cc < 0 || ct < 0) return 0;
    int read = 0; p = he + 1;
    while (p < end && nitems < MAXI) {
        const char *e = memchr(p, '\n', (size_t)(end - p)); if (!e) e = end;
        const char *cell[160]; int clen[160]; int n = line_cells(p, e, cell, clen, 160);
        if (n > cc && n > ct && clen[cc] >= 3) {
            itemdb_t *it = &items[nitems]; memset(it, 0, sizeof *it);
            memcpy(it->code, cell[cc], 3); it->code[3] = 0;
            cellcopy(itype[nitems], sizeof itype[0], cell[ct], clen[ct]);
            if (cl >= 0 && n > cl) it->qlvl = cellint(cell[cl], clen[cl]);
            if (cr >= 0 && n > cr) it->levelreq = cellint(cell[cr], clen[cr]);
            if (cm >= 0 && n > cm) it->magiclvl = cellint(cell[cm], clen[cm]);
            if (cs >= 0 && n > cs) it->speed = cellint(cell[cs], clen[cs]);
            if (cra >= 0 && n > cra) it->rangeadder = cellint(cell[cra], clen[cra]);
            if (cq >= 0 && n > cq) it->quest = cellint(cell[cq], clen[cq]);
            normc[nitems][0] = uberc[nitems][0] = ultrac[nitems][0] = 0;
            if (cn >= 0 && n > cn) cellcopy(normc[nitems], 4, cell[cn], clen[cn]);
            if (cu >= 0 && n > cu) cellcopy(uberc[nitems], 4, cell[cu], clen[cu]);
            if (cx >= 0 && n > cx) cellcopy(ultrac[nitems], 4, cell[cx], clen[cx]);
            it->groups = groups_of(itype[nitems]);
            if (kind == 2) gem_rune(it);
            if (kind < 2) {      // tier: which of its own normal/exceptional/elite codes it is
                if (!strncmp(normc[nitems], it->code, 3)) it->groups |= FI_G_NORMAL;
                if (!strncmp(uberc[nitems], it->code, 3)) it->groups |= FI_G_EXCEPTIONAL;
                if (!strncmp(ultrac[nitems], it->code, 3)) it->groups |= FI_G_ELITE;
            }
            nitems++; read++;
        }
        p = e + 1;
    }
    return read;
}
const itemdb_t *itemdb_find(const char *code) {
    for (int i = 0; i < nitems; i++) if (!strncmp(items[i].code, code, 3)) return &items[i];
    return NULL;
}
