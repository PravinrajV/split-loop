#include "sl.h"
#include <math.h>
#include <string.h>
#include <time.h>
const char *reasons[] = {"ACCEPT",           "CLAMP_JERK",        "REJECT_KEEP_OUT",
                         "REJECT_STALE",     "REJECT_REPLAY",     "REJECT_EXPIRED",
                         "REJECT_MALFORMED", "FALLBACK_NO_ADVICE"};
static double clamp(double x, double lo, double hi) {
    return fmin(hi, fmax(lo, x));
}
int64_t mono(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}
int recipe_ok(Recipe r) {
    return r.slot >= 0 && r.slot < 4 && r.occupied < 16 && !(r.occupied & (1u << r.slot));
}
int keepout(Recipe r, double x, double y) {
    if (x < 0 || x > 500 || y < 0 || y > 400)
        return 1;
    if (x >= 360 && x <= 480 && y >= 300 && y <= 390)
        return 1;
    for (int i = 0; i < 4; i++)
        if ((r.occupied >> i) & 1) {
            double c = 80 + 70 * i;
            if (x >= c - 25 && x <= c + 25 && y >= 45 && y <= 95)
                return 1;
        }
    return 0;
}
/* Exact segment/closed rectangle intersection; no point-sampling shortcut. */
static int rect(const double *a, const double *b, double x0, double x1, double y0, double y1) {
    double lo = 0, hi = 1, d[2] = {b[0] - a[0], b[1] - a[1]}, mn[2] = {x0, y0}, mx[2] = {x1, y1};
    for (int k = 0; k < 2; k++) {
        if (fabs(d[k]) < 1e-12) {
            if (a[k] < mn[k] || a[k] > mx[k])
                return 0;
        } else {
            double u = (mn[k] - a[k]) / d[k], v = (mx[k] - a[k]) / d[k];
            if (u > v) {
                double t = u;
                u = v;
                v = t;
            }
            lo = fmax(lo, u);
            hi = fmin(hi, v);
            if (lo > hi)
                return 0;
        }
    }
    return 1;
}
int swept(Recipe r, const double *q, const double *e) {
    if (keepout(r, q[0], q[1]) || keepout(r, e[0], e[1]))
        return 1;
    if (rect(q, e, 360, 480, 300, 390))
        return 1;
    for (int i = 0; i < 4; i++)
        if (r.occupied & (1u << i)) {
            double c = 80 + 70 * i;
            if (rect(q, e, c - 25, c + 25, 45, 95))
                return 1;
        }
    return 0;
}
void profile_init(Profile *p, double from, double to, double vmax, double amax, double jmax) {
    memset(p, 0, sizeof(*p));
    p->start = from;
    p->end = to;
    p->sign = to >= from ? 1 : -1;
    double d = fabs(to - from);
    if (d < 1e-12)
        return;
    p->jerk = jmax;
    double tj = amax / jmax, ta = vmax / amax - tj;
    if (ta < 0) {
        tj = sqrt(vmax / jmax);
        ta = 0;
        amax = jmax * tj;
    }
    double v = amax * (tj + ta), da = v * (2 * tj + ta), tv = 0;
    if (d >= da)
        tv = (d - da) / v;
    else {
        ta = (-3 * tj + sqrt(tj * tj + 4 * d / amax)) / 2;
        if (ta < 0) {
            tj = cbrt(d / (2 * jmax));
            ta = 0;
        }
    }
    p->tj = tj;
    p->ta = ta;
    p->tv = tv;
    p->total = 4 * tj + 2 * ta + tv;
}
void profile_at(const Profile *p, double t, double *q, double *v, double *a, double *j) {
    double x = 0, u = 0, b = 0;
    double ds[7] = {p->tj, p->ta, p->tj, p->tv, p->tj, p->ta, p->tj},
           js[7] = {p->jerk, 0, -p->jerk, 0, -p->jerk, 0, p->jerk};
    *j = 0;
    if (t <= 0) {
        *q = p->start;
        *v = *a = 0;
        return;
    }
    if (t >= p->total) {
        *q = p->end;
        *v = *a = *j = 0;
        return;
    }
    for (int i = 0; i < 7; i++) {
        double h = fmin(t, ds[i]);
        x += u * h + b * h * h / 2 + js[i] * h * h * h / 6;
        u += b * h + js[i] * h * h / 2;
        b += js[i] * h;
        t -= h;
        if (t < 1e-10) {
            *j = js[i] * p->sign;
            break;
        }
    }
    *q = p->start + p->sign * x;
    *v = p->sign * u;
    *a = p->sign * b;
}
double baseline(Recipe r, double t, Motion *m) {
    Profile p[3];
    double sx = 80 + 70 * r.slot;
    profile_init(&p[0], 70, 180, 80, 200, 600);
    profile_init(&p[1], sx, 390, 80, 200, 600);
    profile_init(&p[2], 180, 250, 80, 200, 600);
    memset(m, 0, sizeof(*m));
    m->q[0] = sx;
    m->q[1] = 70;
    double end = p[0].total + p[1].total + p[2].total;
    if (t < p[0].total)
        profile_at(&p[0], t, &m->q[1], &m->v[1], &m->a[1], &m->j[1]);
    else if ((t -= p[0].total) < p[1].total) {
        m->q[1] = 180;
        profile_at(&p[1], t, &m->q[0], &m->v[0], &m->a[0], &m->j[0]);
    } else {
        m->q[0] = 390;
        profile_at(&p[2], t - p[1].total, &m->q[1], &m->v[1], &m->a[1], &m->j[1]);
    }
    return end;
}
void joint_step(JointState *s, Recipe r, const JointCmd *c) {
    Motion n = s->truth;
    for (int k = 0; k < 2; k++) {
        double err = c->ref.q[k] - s->sensor.q[k],
               ev = c->ref.v[k] - s->sensor.v[k]; /* unit mass, viscous friction 1.2 N/(mm/s),
                                                     feedforward compensated */
        double force = c->ref.a[k] + 1.2 * n.v[k] + 4 * err + 4 * ev;
        double target = clamp(force - 1.2 * n.v[k], -c->amax, c->amax);
        double a = clamp(
            target, n.a[k] - c->jmax * DT,
            n.a[k] + c->jmax * DT); /* Preserve a jerk-feasible braking envelope before reaching the
velocity cap. The profile constants remain unchanged; this prevents feedback overshoot. */
        double lo = n.a[k] - c->jmax * DT, hi = a;
        if (n.v[k] + .5 * (n.a[k] + a) * DT + fmax(a, 0) * fmax(a, 0) / (2 * c->jmax) > c->vmax) {
            for (int z = 0; z < 40; z++) {
                double mid = (lo + hi) / 2;
                double v = n.v[k] + .5 * (n.a[k] + mid) * DT;
                if (v + fmax(mid, 0) * fmax(mid, 0) / (2 * c->jmax) > c->vmax)
                    hi = mid;
                else
                    lo = mid;
            }
            a = lo;
        }
        lo = a;
        hi = n.a[k] + c->jmax * DT;
        if (-(n.v[k] + .5 * (n.a[k] + a) * DT) + fmax(-a, 0) * fmax(-a, 0) / (2 * c->jmax) >
            c->vmax) {
            for (int z = 0; z < 40; z++) {
                double mid = (lo + hi) / 2;
                double v = n.v[k] + .5 * (n.a[k] + mid) * DT;
                if (-v + fmax(-mid, 0) * fmax(-mid, 0) / (2 * c->jmax) > c->vmax)
                    lo = mid;
                else
                    hi = mid;
            }
            a = hi;
        }
        double j = (a - n.a[k]) / DT;
        n.q[k] += n.v[k] * DT + n.a[k] * DT * DT / 2 + j * DT * DT * DT / 6;
        n.v[k] += n.a[k] * DT + j * DT * DT / 2;
        n.a[k] = a;
        n.j[k] = j;
        if (fabs(j) > 1500.000001)
            s->jerk_breach++;
    }
    if (swept(r, s->truth.q, n.q)) {
        s->collision++;
        s->reason = REJECT_KEEP_OUT;
        return;
    }
    s->truth = n;
    s->tick++;
    s->sensor = n;
    for (int k = 0; k < 2; k++) {
        s->sensor.q[k] += .005 * sin((double)s->tick * .17 + k);
        s->sensor.v[k] += .002 * sin((double)s->tick * .23 + k);
    }
    s->reason = ACCEPT;
}
