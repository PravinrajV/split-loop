#include "runtime.h"
#include <math.h>
#include <string.h>
const char *faults[] = {"none",      "keepout",  "jerk", "stale", "replay", "expired",
                        "malformed", "overload", "hang", "crash", "direct"};
int contract(const Advice *a, unsigned prev, uint64_t len, int64_t now, Recipe r,
             const Motion *actual) {
    if (len != sizeof(*a) || a->length != sizeof(*a) || a->units != 1 || a->stamp > now + NS ||
        a->expiry <= a->stamp)
        return REJECT_MALFORMED;
    for (int k = 0; k < 2; k++)
        if (!isfinite(a->ref.q[k]) || !isfinite(a->ref.v[k]) || !isfinite(a->ref.a[k]) ||
            !isfinite(a->ref.j[k]) || !isfinite(a->path0[k]) || !isfinite(a->path1[k]) ||
            fabs(a->ref.v[k]) > 150 || fabs(a->ref.a[k]) > 400)
            return REJECT_MALFORMED;
    if (now - a->stamp > 100000000LL)
        return REJECT_STALE;
    if (a->expiry <= now)
        return REJECT_EXPIRED;
    if (a->seq <= prev)
        return REJECT_REPLAY;
    if (swept(r, actual->q, a->ref.q) || swept(r, a->path0, a->path1))
        return REJECT_KEEP_OUT;
    for (int k = 0; k < 2; k++)
        if (fabs(a->ref.j[k]) > 1500)
            return CLAMP_JERK;
    return ACCEPT;
}
/* Native on-target scalar S-curve profiler. Equal scalar time synchronizes axes. */
double planner(Recipe r, double t, Motion *m) {
    double start[2] = {80 + 70 * r.slot, 70}, d[2] = {390 - start[0], 180};
    double l = hypot(d[0], d[1]);
    Profile p;
    profile_init(&p, 0, l, 140, 350, 1400);
    double q, v, a, j;
    profile_at(&p, t, &q, &v, &a, &j);
    memset(m, 0, sizeof(*m));
    for (int k = 0; k < 2; k++) {
        m->q[k] = start[k] + q * d[k] / l;
        m->v[k] = v * d[k] / l;
        m->a[k] = a * d[k] / l;
        m->j[k] = j * d[k] / l;
    }
    return p.total;
}
void home_at(const Home *h, double t, Motion *m) {
    if (t > h->total)
        t = h->total;
    for (int k = 0; k < 2; k++) {
        const double *c = h->c[k];
        m->q[k] = c[0] + t * (c[1] + t * (c[2] + t * (c[3] + t * (c[4] + t * c[5]))));
        m->v[k] = c[1] + t * (2 * c[2] + t * (3 * c[3] + t * (4 * c[4] + t * 5 * c[5])));
        m->a[k] = 2 * c[2] + t * (6 * c[3] + t * (12 * c[4] + t * 20 * c[5]));
        m->j[k] = 6 * c[3] + t * (24 * c[4] + t * 60 * c[5]);
    }
}
int home_init(Home *h, const Motion *m, Recipe r) {
    const double durations[] = {4, 3, 2.5, 2, 1.5, 1, 5, 6, 7, 8, 10};
    for (unsigned attempt = 0; attempt < sizeof(durations) / sizeof(durations[0]); attempt++) {
        double T = durations[attempt], goal[2] = {250, 180};
        h->total = T;
        for (int k = 0; k < 2; k++) {
            double d = goal[k] - m->q[k], v = m->v[k], a = m->a[k];
            h->c[k][0] = m->q[k];
            h->c[k][1] = v;
            h->c[k][2] = a / 2;
            h->c[k][3] = (20 * d - 12 * v * T - 3 * a * T * T) / (2 * T * T * T);
            h->c[k][4] = (-30 * d + 16 * v * T + 3 * a * T * T) / (2 * T * T * T * T);
            h->c[k][5] = (12 * d - 6 * v * T - a * T * T) / (2 * T * T * T * T * T);
        }
        Motion a = *m, b;
        int valid = 1;
        int steps = (int)(T * 1000);
        for (int i = 1; i <= steps; i++) {
            home_at(h, i * .001, &b);
            if (swept(r, a.q, b.q)) {
                valid = 0;
                break;
            }
            for (int k = 0; k < 2; k++)
                if (fabs(b.v[k]) > 150 || fabs(b.a[k]) > 400 || fabs(b.j[k]) > 1500) {
                    valid = 0;
                    break;
                }
            if (!valid)
                break;
            a = b;
        }
        if (valid)
            return 1;
    }
    return 0;
}
