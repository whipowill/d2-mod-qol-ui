// Offline tests for the auto party decision engine.
#include "../src/party.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)
#define N PARTY_NONE
static const unsigned ME = 1;

static int run(const party_member *m, int n, int *click, int *leave) { return party_decide(m, n, ME, click, leave); }

int main(void) {
    int c[PARTY_MAX], lv;
    // alone: nothing to do
    { party_member m[] = {{1, 80, N, 0}}; CHECK(run(m, 1, c, &lv) == 0 && !lv, "alone runs"); }
    // a loner next to me, nobody in a party: invite him
    { party_member m[] = {{1, 80, N, 0}, {2, 70, N, 0}}; CHECK(run(m, 2, c, &lv) == 0 && c[1] && !lv, "invite a loner"); }
    // already invited by me: do not invite again
    { party_member m[] = {{1, 80, N, 0}, {2, 70, N, PF_INVITED_BY_YOU}}; run(m, 2, c, &lv); CHECK(!c[1], "no second invite"); }
    // he invited me and is in party 3: accept
    { party_member m[] = {{1, 80, N, 0}, {2, 70, 3, PF_IN_PARTY | PF_INVITED_YOU}}; run(m, 2, c, &lv); CHECK(c[1] && !lv, "accept the master party's invite"); }
    // two parties, 3 and 5; I am in neither: accept only party 3's invite, invite nobody who is... loner is invited only from the master party
    { party_member m[] = {{1, 80, N, 0}, {2, 70, 3, PF_IN_PARTY | PF_INVITED_YOU}, {3, 70, 5, PF_IN_PARTY | PF_INVITED_YOU}, {4, 60, N, 0}};
      run(m, 4, c, &lv); CHECK(c[1] && !c[2] && !c[3] && !lv, "only the master party's invite is accepted, no loner invited from outside it"); }
    // I am in the master party: invite loners
    { party_member m[] = {{1, 80, 3, PF_IN_PARTY}, {2, 70, 3, PF_IN_PARTY}, {3, 60, N, 0}}; run(m, 3, c, &lv); CHECK(!c[1] && c[2] && !lv, "invite a loner from the master party"); }
    // I am in a different party than the lowest id: leave, and click nothing
    { party_member m[] = {{1, 80, 7, PF_IN_PARTY}, {2, 70, 3, PF_IN_PARTY}, {3, 60, N, 0}}; run(m, 3, c, &lv); CHECK(lv && !c[1] && !c[2], "leave the wrong party"); }
    // I am in a party of my own with nobody else in any: not a reason to leave; invite
    { party_member m[] = {{1, 80, 4, PF_IN_PARTY}, {2, 70, N, 0}}; run(m, 2, c, &lv); CHECK(!lv && c[1], "my own party, invite"); }
    // a player with level 0 (still loading): do nothing this time
    { party_member m[] = {{1, 80, N, 0}, {2, 0, N, 0}}; CHECK(run(m, 2, c, &lv) == -1, "level 0 aborts"); }
    // party id and flag disagree: abort
    { party_member m[] = {{1, 80, N, 0}, {2, 70, 3, 0}}; CHECK(run(m, 2, c, &lv) == -1, "id without flag aborts"); }
    { party_member m[] = {{1, 80, N, 0}, {2, 70, N, PF_IN_PARTY}}; CHECK(run(m, 2, c, &lv) == -1, "flag without id aborts"); }
    // I am not in the list
    { party_member m[] = {{5, 80, N, 0}, {2, 70, N, 0}}; CHECK(run(m, 2, c, &lv) == -1, "self not found"); }
    // an invite from a loner while no party exists: accept
    { party_member m[] = {{1, 80, N, 0}, {2, 70, N, PF_INVITED_YOU}}; run(m, 2, c, &lv); CHECK(c[1], "accept when no party exists"); }
    printf(fails ? "%d FAIL\n" : "party ok\n", fails);
    return fails != 0;
}
