#include "runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    for (int type = 0; type < 2; type++)
        for (int slot = 0; slot < 4; slot++) {
            Recipe r = {slot, 15u ^ (1u << slot)};
            JointState s;
            memset(&s, 0, sizeof(s));
            s.truth.q[0] = 80 + 70 * slot;
            s.truth.q[1] = 70;
            s.sensor = s.truth;
            JointCmd c = {.magic = MAGIC,
                          .op = 1,
                          .recipe = r,
                          .vmax = type ? 150 : 80,
                          .amax = type ? 400 : 200,
                          .jmax = type ? 1400 : 600};
            int settle = 0;
            for (int i = 0; i < 1500; i++) {
                if (type)
                    planner(r, (i + 1) * DT, &c.ref);
                else
                    baseline(r, (i + 1) * DT, &c.ref);
                joint_step(&s, r, &c);
                assert(!s.collision && !s.jerk_breach);
                for (int k = 0; k < 2; k++) {
                    assert(fabs(s.truth.v[k]) <= c.vmax + 1e-6);
                    assert(fabs(s.truth.a[k]) <= c.amax + 1e-6);
                    assert(fabs(s.truth.j[k]) <= c.jmax + 1e-6);
                }
                if (type) {
                    Home h;
                    assert(home_init(&h, &s.truth, r));
                }
                if (hypot(s.truth.q[0] - 390, s.truth.q[1] - 250) <= 8 && fabs(s.truth.v[0]) <= 5 &&
                    fabs(s.truth.v[1]) <= 5 && fabs(s.truth.a[0]) <= 50 && fabs(s.truth.a[1]) <= 50)
                    settle++;
                else
                    settle = 0;
                if (settle == 10)
                    break;
            }
            assert(settle == 10);
        }
    puts("PASS: all 4 recipes, frozen baseline & native profiler; actual v/a/j caps, zero "
         "collisions, ten-period settle, legal HOME from every planner tick");
}
