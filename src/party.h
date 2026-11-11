// Auto party decision engine (ported from BH Modules/Party/Party.cpp CheckParty). Pure C, no game memory: the game side (autoparty.h) fills
// the member array from the roster and carries out the result.
#ifndef UI_PARTY_H
#define UI_PARTY_H

#define PARTY_NONE        0xFFFFu   // BH INVALID_PARTY_ID: not in a party
#define PF_IN_PARTY       0x01u     // dwPartyFlags (BH PARTY_*)
#define PF_INVITED_YOU    0x02u
#define PF_INVITED_BY_YOU 0x04u
#define PARTY_MAX         32

typedef struct { unsigned unit_id, level, party_id, flags; } party_member;

// m[0..n) is the whole roster including yourself (the entry whose unit_id is me_id). On return click[i] = 1 for every other player to
// "click" in the party panel (accept their invite or invite them), and *leave = 1 when you should leave your party first (then no clicks).
// Returns 0 when it ran, -1 when it did nothing this time (yourself not found, or a half-updated roster: BH's guard against crashes when
// several clients run auto party at once).
int party_decide(const party_member *m, int n, unsigned me_id, int *click, int *leave);

#endif
