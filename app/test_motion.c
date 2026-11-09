#include "sl.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
    for (int slot = 0; slot < 4; slot++) {
        Recipe r = {slot, 15u ^ (1u << slot)};
        Motion m, old;
        double total = baseline(r, 0, &m);
        old = m;
        for (double t = 0; t < total + .1; t += .001) {
            baseline(r, t, &m);
            assert(!swept(r, old.q, m.q));
            for (int k = 0; k < 2; k++) {
                assert(fabs(m.v[k]) <= 80.000001);
                assert(fabs(m.a[k]) <= 200.000001);
                assert(fabs(m.j[k]) <= 600.000001);
            }
            old = m;
        }
        assert(fabs(m.q[0] - 390) < 1e-6 && fabs(m.q[1] - 250) < 1e-6);
    }
    assert(!recipe_ok((Recipe){0, 1}));
    assert(keepout((Recipe){0, 14}, 150, 70));
    assert(swept((Recipe){0, 14}, (double[]){80, 70}, (double[]){290, 70}));
    puts("PASS frozen geometry and four recipe baseline profile limit/path tests");
}
