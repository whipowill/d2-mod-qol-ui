#include "../src/itemdb.h"
#include "../src/filter.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } } while (0)
int main(void) {
    const char *types = "ItemType\tCode\tEquiv1\tEquiv2\nArmor\tarmo\t\t\nHelm\thelm\tarmo\t\nPelt\tpelt\thelm\t\nWeapon\tweap\t\t\nSword\tswor\tweap\t\nThrowing Knife\ttkni\tweap\t\nGem\tgem\t\t\n";
    const char *armor = "name\tcode\ttype\tlevel\tlevelreq\tnormcode\tubercode\tultracode\tspeed\nCap\tcap\thelm\t1\t0\tcap\txap\tuap\t0\nWolf Head\tdr1\tpelt\t5\t0\tdr1\tdr6\tdrb\t0\n";
    const char *weap = "name\tcode\ttype\tlevel\tlevelreq\tnormcode\tubercode\tultracode\tspeed\trangeadder\nBlade\tbld\tswor\t9\t20\tbld\t9bl\t7bl\t10\t0\n";
    const char *misc = "name\tcode\ttype\tlevel\tlevelreq\tspeed\nPerfect Ruby\tgpr\tgem\t1\t0\t0\nEl Rune\tr01\trune\t1\t0\t0\nSkull\tskz\tgem\t1\t0\t0\n";
    itemdb_reset();
    CHECK(itemdb_parse_types(types, (int)strlen(types)) == 7, "types");
    CHECK(itemdb_parse_items(armor, (int)strlen(armor), 0) == 2, "armor");
    CHECK(itemdb_parse_items(weap, (int)strlen(weap), 1) == 1, "weapons");
    CHECK(itemdb_parse_items(misc, (int)strlen(misc), 2) == 3, "misc");
    const itemdb_t *c = itemdb_find("cap"), *w = itemdb_find("dr1"), *s = itemdb_find("bld"), *g = itemdb_find("gpr"), *r = itemdb_find("r01"), *k = itemdb_find("skz");
    CHECK(c && (c->groups & FI_G_HELM) && (c->groups & FI_G_ALLARMOR) && (c->groups & FI_G_NORMAL), "cap is a normal helm");
    CHECK(w && (w->groups & FI_G_DRUID) && (w->groups & FI_G_HELM), "pelt counts as druid item and as helm (parent type)");
    CHECK(s && (s->groups & FI_G_SWORD) && (s->groups & FI_G_ALLWEAPON) && s->levelreq == 20 && s->speed == 10, "sword fields");
    CHECK(g && g->gem_level == 5 && g->gem_type == 4, "perfect ruby");
    CHECK(r && r->rune_no == 1, "El rune");
    CHECK(k && k->gem_level == 5 && k->gem_type == 7, "perfect skull");
    {   // MonStats.txt: Align by hcIdx (the "Expansion" divider row has none); without hcIdx the row number without the divider
        const char *ms = "Id\thcIdx\tAlign\tnpc\r\nskeleton1\t0\t\t\r\nrat\t151\t1\t\r\nExpansion\t\t\t\r\ncow\t179\t2\t\r\n";
        CHECK(itemdb_parse_monstats(ms, (int)strlen(ms)) == 180, "monstats read");
        CHECK(itemdb_mon_align(0) == 0 && itemdb_mon_align(151) == 1 && itemdb_mon_align(179) == 2 && itemdb_mon_align(5000) == 0, "align by hcIdx");
        const char *m2 = "Id\tAlign\tnpc\r\na\t\t\r\nb\t1\t\r\nExpansion\t\t\r\nc\t2\t\r\n";
        CHECK(itemdb_parse_monstats(m2, (int)strlen(m2)) == 3 && itemdb_mon_align(1) == 1 && itemdb_mon_align(2) == 2, "align by row number");
    }
    printf(fails ? "%d FAIL\n" : "itemdb ok\n", fails);
    return fails != 0;
}
