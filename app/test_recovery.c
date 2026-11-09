#include "runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    unsigned cases = 0;
    for (int slot = 0; slot < 4; slot++) {
        Recipe r = {slot, 15u ^ (1u << slot)};
        JointState s;
        memset(&s, 0, sizeof(s));
        s.truth.q[0] = 80 + 70 * slot;
        s.truth.q[1] = 70;
        s.sensor = s.truth;
        JointCmd c = {.magic = MAGIC, .op = 1, .recipe = r, .vmax = 150, .amax = 400, .jmax = 1400};
        Motion m;
        int steps = (int)(planner(r, 0, &m) / DT) + 1;
        for (int i = 0; i < steps; i++) {
            planner(r, (i + 1) * DT, &c.ref);
            joint_step(&s, r, &c);
            if (i % 5)
                continue;
            JointState f = s;
            Home h;
            assert(home_init(&h, &f.truth, r));
            JointCmd k = c;
            k.jmax = 1500;
            int settle = 0;
            for (int z = 0; z < 1200; z++) {
                home_at(&h, (z + 1) * DT, &k.ref);
                joint_step(&f, r, &k);
                assert(!f.collision && !f.jerk_breach);
                if (hypot(f.truth.q[0] - 250, f.truth.q[1] - 180) <= 8 && fabs(f.truth.v[0]) <= 5 &&
                    fabs(f.truth.v[1]) <= 5 && fabs(f.truth.a[0]) <= 50 && fabs(f.truth.a[1]) <= 50)
                    settle++;
                else
                    settle = 0;
                if (settle == 10)
                    break;
            }
            if (settle != 10) {
                fprintf(stderr, "HOME failed slot=%d phase=%f\n", slot, i * DT);
                return 1;
            }
            cases++;
        }
    }
    printf("PASS: %u actual-plant HOME recoveries at 50ms planner phase intervals across all four "
           "recipes\n",
           cases);
}
