// Item filter rule engine: reads ItemDisplay rules (the syntax BH uses) and evaluates them against an item.
// Pure C with no game dependency, so it is unit-tested offline; the game side fills in `fi_item`.
#ifndef QT_FILTER_H
#define QT_FILTER_H
#include <stddef.h>

#define FI_UNDEF 0xBEEF                 // "no colour given" for the map actions

// Pseudo stat ids passed to fi_item.stat for values that are not plain stats (the game side computes them)
#define FI_STAT_ED      100000          // enhanced damage (weapons) / enhanced defense (armor)
#define FI_STAT_MAXDUR  100001          // enhanced max durability
#define FI_STAT_FOOLS   100002          // 1 = max damage per level, 2 = attack rating per level (sum 3 = Fool's)
#define FI_STAT_GOODCLS 100003          // layer = class id, summed over the configured "good" class skills by the engine
#define FI_STAT_CHARGE  100004          // layer = skill id: highest charged level of that skill

// Item facts (the game side fills these in)
typedef struct fi_item {
    char code[4];                       // 3-letter item code
    int quality;                        // 1 inferior .. 8 crafted
    unsigned flags;                     // the game's item flags (identified 0x10, ethereal 0x400000, runeword 0x4000000)
    int ilvl, qlvl, magiclvl, lvlreq, fileindex, price;
    unsigned groups;                    // ITEM_GROUP_* bits (see below), built from the item type hierarchy
    int rune_no;                        // 1..33 for runes, else 0
    int gem_level, gem_type;            // 1..5 / 1..7 for gems, else 0
    int wpnspd, rangeadder;
    int clvl, difficulty, expansion;    // the player: level, difficulty (0..2), 1 = expansion character
    int filter_level;                   // the configured Filter Level
    const char *basename;               // localized base item name
    int (*stat)(void *ctx, int stat, int layer);    // an item stat (sum of all its sources)
    int (*pstat)(void *ctx, int stat, int layer);   // a stat of the player (CHARSTATn)
    void *ctx;
} fi_item;

// Same values BH uses
enum {
    FI_G_HELM = 0x1, FI_G_ARMOR = 0x2, FI_G_SHIELD = 0x4, FI_G_GLOVES = 0x8, FI_G_BOOTS = 0x10, FI_G_BELT = 0x20,
    FI_G_DRUID = 0x40, FI_G_BARB = 0x80, FI_G_PALADIN = 0x100, FI_G_NECRO = 0x200, FI_G_AXE = 0x400, FI_G_MACE = 0x800,
    FI_G_SWORD = 0x1000, FI_G_DAGGER = 0x2000, FI_G_THROWING = 0x4000, FI_G_JAVELIN = 0x8000, FI_G_SPEAR = 0x10000,
    FI_G_POLEARM = 0x20000, FI_G_BOW = 0x40000, FI_G_XBOW = 0x80000, FI_G_STAFF = 0x100000, FI_G_WAND = 0x200000,
    FI_G_SCEPTER = 0x400000, FI_G_KATAR = 0x800000, FI_G_ORB = 0x1000000, FI_G_AMAZON = 0x2000000, FI_G_NORMAL = 0x4000000,
    FI_G_EXCEPTIONAL = 0x8000000, FI_G_ELITE = 0x10000000, FI_G_ALLARMOR = 0x20000000, FI_G_ALLWEAPON = 0x40000000,
    FI_G_CIRCLET = 0x80000000u
};

typedef struct fi_action {
    char *name;                         // text template (macros not yet substituted)
    char *desc;                         // {description} text, or ""
    int border, map, dot, px, line, notify;   // map colours, FI_UNDEF when not given
    int ping;                           // %TIER-n% (0 when absent)
    int stop;                           // 1 unless %CONTINUE%
    int notrack;
} fi_action;

typedef struct fi_set fi_set;

// Parse a config file's text. Lines that are not ItemDisplay / ConditionGroup / ClassSkillsList / TabSkillsList are ignored.
// `unknown` receives the number of condition tokens that were not understood (BH silently ignores them); may be NULL.
fi_set *fi_parse(const char *text, size_t len, int *unknown, char *firstbad, size_t firstbad_cap);
void fi_free(fi_set *s);
int fi_rule_count(const fi_set *s);
void fi_set_video(fi_set *s, int glide);        // 1 when the Glide renderer is active (changes four colour codes)

// The item's displayed name: runs the name rules in order over `name` (the game's own text). Returns the length written.
// Appends " [blocked]" like BH when the item is on the ignore list (unless `blocked_out` is requested separately).
int fi_name(fi_set *s, const fi_item *it, const char *name, char *out, size_t cap, int *blocked);
// {description} text for the item
int fi_desc(fi_set *s, const fi_item *it, const char *name, char *out, size_t cap);
// All matching rules that carry a map action (dots, notifications, ping level), in file order. Returns how many were stored.
int fi_actions(fi_set *s, const fi_item *it, fi_action *out, int max);
// 1 when an ignore rule matches and nothing whitelists the item
int fi_is_blocked(fi_set *s, const fi_item *it);

#endif
