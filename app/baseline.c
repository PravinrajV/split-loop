#include "sl.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/resmgr.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static JointState state;
static Recipe recipe;
static int rd(resmgr_context_t *c, io_read_t *m, void *o) {
    int rc = iofunc_read_verify(c, m, o, NULL);
    if (rc)
        return rc;
    int n = m->i.nbytes < sizeof(state) ? m->i.nbytes : sizeof(state);
    _IO_SET_READ_NBYTES(c, n);
    return _RESMGR_PTR(c, &state, n);
}
static int wr(resmgr_context_t *c, io_write_t *m, void *o) {
    int rc = iofunc_write_verify(c, m, o, NULL);
    if (rc)
        return rc;
    if (m->i.nbytes != sizeof(JointCmd))
        return EMSGSIZE;
    JointCmd j;
    if (resmgr_msgread(c, &j, sizeof(j), sizeof(m->i)) != sizeof(j))
        return EIO;
    if (j.magic != MAGIC)
        return EINVAL;
    if (j.op == 0) {
        if (!recipe_ok(j.recipe))
            return EINVAL;
        recipe = j.recipe;
        memset(&state, 0, sizeof(state));
        state.truth.q[0] = 80 + 70 * recipe.slot;
        state.truth.q[1] = 70;
        state.sensor = state.truth;
    } else {
        if (j.vmax > 150 || j.amax > 400 || j.jmax > 1500 || j.jmax <= 0)
            return EINVAL;
        for (int k = 0; k < 2; k++)
            if (!isfinite(j.ref.q[k]) || !isfinite(j.ref.v[k]) || !isfinite(j.ref.a[k]) ||
                !isfinite(j.ref.j[k]))
                return EINVAL;
        joint_step(&state, recipe, &j);
    }
    _IO_SET_WRITE_NBYTES(c, sizeof(j));
    return EOK;
}
static void manager(void) {
    dispatch_t *d = dispatch_create();
    resmgr_connect_funcs_t cf;
    resmgr_io_funcs_t io;
    resmgr_attr_t ra;
    iofunc_attr_t a;
    memset(&ra, 0, sizeof(ra));
    ra.nparts_max = 1;
    ra.msg_max_size = 4096;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cf, _RESMGR_IO_NFUNCS, &io);
    io.read = rd;
    io.write = wr;
    iofunc_attr_init(&a, S_IFCHR | 0666, NULL, NULL);
    if (resmgr_attach(d, &ra, "/dev/joint", _FTYPE_ANY, 0, &cf, &io, &a) < 0) {
        perror("joint attach");
        exit(2);
    }
    dispatch_context_t *c = dispatch_context_alloc(d);
    for (;;) {
        c = dispatch_block(c);
        if (c)
            dispatch_handler(c);
    }
}
static void run(int id) {
    int fd = open("/dev/joint", O_RDWR);
    if (fd < 0) {
        perror("joint open");
        exit(2);
    }
    struct sched_param sp = {.sched_priority = 40};
    int fifo = sched_setscheduler(0, SCHED_FIFO, &sp);
    JointCmd c = {.magic = MAGIC, .recipe = {0, 14}};
    write(fd, &c, sizeof(c));
    c.op = 1;
    c.vmax = 80;
    c.amax = 200;
    c.jmax = 600;
    JointState s;
    read(fd, &s, sizeof(s));
    double total = baseline(c.recipe, 0, &c.ref);
    int64_t next = mono() + NS, start = 0, stop = 0, settle0 = 0;
    int settle = 0;
    unsigned misses = 0;
    printf("BASELINE_BEGIN,%d,FIFO=%d,%s\n", id, fifo, WARNING);
    printf("tick,mono_ns,wake_lateness_ns,command_latency_ns,x,y,vx,vy,ax,ay,jx,jy,collision,jerk_"
           "breach\n"); /* bounded local capture: printf only after control finishes */
    struct Sample {
        int64_t t, late, lat;
        JointState s;
    } samples[2000];
    int count = 0;
    for (int i = 0; i < 2000; i++) {
        struct timespec ts = {next / 1000000000LL, next % 1000000000LL};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        int64_t now = mono();
        baseline(c.recipe, (i + 1) * DT, &c.ref);
        int64_t a = mono();
        if (write(fd, &c, sizeof(c)) != sizeof(c) || read(fd, &s, sizeof(s)) != sizeof(s)) {
            perror("joint io");
            break;
        }
        int64_t b = mono();
        samples[count++] = (struct Sample){now, now - next, b - a, s};
        if (now - next >= NS || b - next > NS)
            misses++;
        if (!start && hypot(s.truth.q[0] - 80, s.truth.q[1] - 70) > 2)
            start = now;
        if (hypot(s.truth.q[0] - 390, s.truth.q[1] - 250) <= 8 && fabs(s.truth.v[0]) <= 5 &&
            fabs(s.truth.v[1]) <= 5 && fabs(s.truth.a[0]) <= 50 && fabs(s.truth.a[1]) <= 50) {
            if (!settle)
                settle0 = now;
            settle++;
        } else
            settle = 0;
        if (settle >= 10) {
            stop = settle0;
            break;
        }
        if (s.collision || s.jerk_breach)
            break;
        next += NS;
    }
    for (int i = 0; i < count; i++) {
        Motion *m = &samples[i].s.truth;
        printf("%d,%lld,%lld,%lld,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%u,%u\n", i,
               (long long)samples[i].t, (long long)samples[i].late, (long long)samples[i].lat,
               m->q[0], m->q[1], m->v[0], m->v[1], m->a[0], m->a[1], m->j[0], m->j[1],
               samples[i].s.collision, samples[i].s.jerk_breach);
    }
    printf("BASELINE_RESULT,%d,success=%d,cycle_s=%.9f,samples=%d,collision=%u,jerk_breach=%u,"
           "misses=%u,profile_s=%.9f\n",
           id, stop > start && start && fifo == 0, (stop && start) ? (stop - start) / 1e9 : -1.,
           count, s.collision, s.jerk_breach, misses, total);
    printf("BASELINE_END\n");
    close(fd);
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "joint")) {
        manager();
        return 0;
    }
    for (int i = 0; i < 2; i++)
        run(i);
    return 0;
}
