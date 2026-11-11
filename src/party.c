// Auto party decisions, after BH Modules/Party/Party.cpp (CheckParty). See party.h.
#include "party.h"
#include <string.h>

int party_decide(const party_member *m, int n, unsigned me_id, int *click, int *leave) {
    int me = -1;
    *leave = 0;
    if (n <= 0 || n > PARTY_MAX) return -1;
    memset(click, 0, (size_t)n * sizeof *click);
    for (int i = 0; i < n; i++) if (m[i].unit_id == me_id) { me = i; break; }
    if (me < 0) return -1;
    unsigned master = PARTY_NONE;                                   // the lowest party id among the other players
    for (int i = 0; i < n; i++) {
        if (i == me) continue;
        // sanity: a player who has just joined has level 0, and a party id has to agree with the "in party" flag; try again next time
        if (m[i].level == 0) return -1;
        if ((m[i].party_id != PARTY_NONE) != ((m[i].flags & PF_IN_PARTY) != 0)) return -1;
        if (m[i].party_id < master) master = m[i].party_id;
    }
    unsigned mine = m[me].party_id;
    if (mine != PARTY_NONE && master != PARTY_NONE && mine != master) { *leave = 1; return 0; }
    for (int i = 0; i < n; i++) {
        if (i == me) continue;
        if (m[i].flags & PF_INVITED_YOU) {                          // accept an invite only into the master party (or when there is none)
            if (master == PARTY_NONE || m[i].party_id == master) click[i] = 1;
        } else if (m[i].party_id == PARTY_NONE && !(m[i].flags & PF_INVITED_BY_YOU)) {
            if (master == PARTY_NONE || mine == master) click[i] = 1;   // invite a loner when we are in the master party (or nobody is in one)
        }
    }
    return 0;
}
