#ifndef RUNTIME_H
#define RUNTIME_H
#include "sl.h"
#include <sys/types.h>
typedef struct {
    unsigned seq, units, length;
    int64_t stamp, expiry;
    Motion ref;
    double path0[2], path1[2];
} Advice;
typedef struct {
    atomic_uint version;
    atomic_llong heartbeat;
    Advice value;
} AdviceBus;
typedef struct {
    atomic_uint version;
    Recipe recipe;
    unsigned epoch, active, fault, mode;
    double t;
    int64_t tick_time;
} Request;
typedef struct {
    int64_t t, late, lat, age, ask;
    Motion truth, sensor, requested;
    int reason;
    unsigned collision, jerk_breach;
} Sample;
typedef struct {
    atomic_uint count, dropped, done, restarts, latched_reason;
    Motion rejected;
    int fifo, finish;
    double cycle;
    unsigned reasons, misses;
    Sample samples[CAP];
} Log;
enum {
    F_NONE,
    F_KEEP,
    F_JERK,
    F_STALE,
    F_REPLAY,
    F_EXPIRED,
    F_MALFORMED,
    F_OVERLOAD,
    F_HANG,
    F_CRASH,
    F_DIRECT
};
extern const char *faults[];
int contract(const Advice *, unsigned, uint64_t, int64_t, Recipe, const Motion *);
double planner(Recipe, double, Motion *);
typedef struct {
    double c[2][6];
    double total;
} Home;
int home_init(Home *, const Motion *, Recipe);
void home_at(const Home *, double, Motion *);
#endif
