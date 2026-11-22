// Item facts read from the game's own data tables (ItemTypes/Armor/Weapons/Misc.txt), so mods that change items are followed.
#ifndef QT_ITEMDB_H
#define QT_ITEMDB_H
typedef struct {
    char code[4];
    int qlvl, levelreq, magiclvl, speed, rangeadder, quest;
    unsigned groups;          // FI_G_* bits derived from the item type hierarchy and the normal/exceptional/elite codes
    int rune_no, gem_level, gem_type;
} itemdb_t;

int itemdb_parse_types(const char *txt, int len);                 // ItemTypes.txt (call first)
int itemdb_parse_items(const char *txt, int len, int kind);       // kind: 0 armor, 1 weapons, 2 misc; returns items read
const itemdb_t *itemdb_find(const char *code);                    // 3-letter code, or NULL
int itemdb_count(void);
void itemdb_reset(void);
#endif
