#include "runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    Recipe r = {0, 14};
    Motion m = {.q = {80, 70}};
    Advice a = {.seq = 2,
                .units = 1,
                .length = sizeof(Advice),
                .stamp = 1000000,
                .expiry = 81000000,
                .ref = {.q = {81, 71}},
                .path0 = {80, 70},
                .path1 = {390, 250}};
    assert(contract(&a, 1, sizeof(a), 2000000, r, &m) == ACCEPT);
    Advice b = a;
    b.ref.q[0] = NAN;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_MALFORMED);
    b = a;
    b.ref.v[0] = 151;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_MALFORMED);
    b = a;
    b.ref.a[0] = 401;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_MALFORMED);
    b = a;
    b.length++;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_MALFORMED);
    assert(contract(&a, 1, sizeof(a) + 1, 2000000, r, &m) == REJECT_MALFORMED);
    b = a;
    b.stamp = -200000000;
    b.expiry = 100000000;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_STALE);
    assert(contract(&a, 2, sizeof(a), 2000000, r, &m) == REJECT_REPLAY);
    b = a;
    b.expiry = 1500000;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_EXPIRED);
    b = a;
    b.ref.q[0] = 150;
    b.ref.q[1] = 70;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == REJECT_KEEP_OUT);
    b = a;
    b.ref.j[0] = 1501;
    assert(contract(&b, 1, sizeof(b), 2000000, r, &m) == CLAMP_JERK);
    for (int slot = 0; slot < 4; slot++) {
        r = (Recipe){slot, 15u ^ (1u << slot)};
        Motion last;
        double T = planner(r, 0, &last);
        for (double t = .001; t < T + .1; t += .001) {
            Motion n;
            planner(r, t, &n);
            assert(!swept(r, last.q, n.q));
            for (int k = 0; k < 2; k++) {
                assert(fabs(n.v[k]) < 150);
                assert(fabs(n.a[k]) < 400);
                assert(fabs(n.j[k]) < 1500);
            }
            last = n;
        }
        Motion q;
        planner(r, .7, &q);
        Home h;
        assert(home_init(&h, &q, r));
    }
    puts("PASS: contract reason codes, wire sizes, v/a/j, four planner recipes and HOME "
         "construction");
}
