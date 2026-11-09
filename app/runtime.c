#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <secpol/secpol.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/mman.h>
#include <sys/netmgr.h>
#include <sys/neutrino.h>
#include <sys/resmgr.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "seqlocks require lock-free integers");
extern char **environ;
typedef struct {
    atomic_uint fault, scripted;
} Injection;
static Injection *injection;
static Request *request;
static AdviceBus *ab;
static Log *logbuf;
static JointState joint;
static Recipe jr;
static int is_advice;
static void *region(const char *name, size_t n, int write, int create) {
    int f = shm_open(name, (write ? O_RDWR : O_RDONLY) | (create ? O_CREAT : 0), 0666);
    if (f < 0) {
        perror(name);
        exit(2);
    }
    if (create && ftruncate(f, n)) {
        perror("truncate");
        exit(2);
    }
    void *p = mmap(NULL, n, PROT_READ | (write ? PROT_WRITE : 0), MAP_SHARED, f, 0);
    close(f);
    if (p == MAP_FAILED) {
        perror("mmap");
        exit(2);
    }
    if (create)
        memset(p, 0, n);
    return p;
}
static void pause_ns(long ns) {
    struct timespec t = {ns / 1000000000L, ns % 1000000000L};
    nanosleep(&t, NULL);
}
/* Single writer, bounded snapshots: isolated control never waits for the advisor. */
static int reqget(Request *r) {
    for (int n = 0; n < 3; n++) {
        unsigned a = atomic_load_explicit(&request->version, memory_order_acquire);
        if (a & 1)
            continue;
        memcpy(r, request, sizeof(*r));
        atomic_thread_fence(memory_order_acquire);
        if (a == atomic_load_explicit(&request->version, memory_order_acquire))
            return 1;
    }
    return 0;
}
static void reqput(const Request *r) {
    atomic_fetch_add_explicit(&request->version, 1, memory_order_acq_rel);
    memcpy((char *)request + sizeof(atomic_uint), (char *)r + sizeof(atomic_uint),
           sizeof(*r) - sizeof(atomic_uint));
    atomic_fetch_add_explicit(&request->version, 1, memory_order_release);
}
static int advget(Advice *r, unsigned *version) {
    for (int n = 0; n < 3; n++) {
        unsigned a = atomic_load_explicit(&ab->version, memory_order_acquire);
        if (a & 1)
            continue;
        *r = ab->value;
        atomic_thread_fence(memory_order_acquire);
        if (a == atomic_load_explicit(&ab->version, memory_order_acquire)) {
            *version = a;
            return a != 0;
        }
    }
    return 0;
}
static void advput(const Advice *r) {
    atomic_fetch_add_explicit(&ab->version, 1, memory_order_acq_rel);
    ab->value = *r;
    atomic_store(&ab->heartbeat, mono());
    atomic_fetch_add_explicit(&ab->version, 1, memory_order_release);
}
static int rd(resmgr_context_t *c, io_read_t *m, void *o) {
    int e = iofunc_read_verify(c, m, o, NULL);
    if (e)
        return e;
    int n = m->i.nbytes < sizeof(joint) ? m->i.nbytes : sizeof(joint);
    _IO_SET_READ_NBYTES(c, n);
    return _RESMGR_PTR(c, &joint, n);
}
static int wr(resmgr_context_t *c, io_write_t *m, void *o) {
    int e = iofunc_write_verify(c, m, o, NULL);
    if (e)
        return e;
    if (is_advice) {
        Advice a;
        memset(&a, 0, sizeof(a));
        unsigned n = m->i.nbytes;
        if (n <= sizeof(a)) {
            if (resmgr_msgread(c, &a, n, sizeof(m->i)) != n)
                return EIO;
        }
        if (n != sizeof(a))
            a.length = n;
        advput(&a);
        _IO_SET_WRITE_NBYTES(c, n);
        return EOK;
    }
    if (m->i.nbytes != sizeof(JointCmd)) {
        return EMSGSIZE;
    }
    JointCmd j;
    if (resmgr_msgread(c, &j, sizeof(j), sizeof(m->i)) != sizeof(j))
        return EIO;
    if (j.magic != MAGIC)
        return EINVAL;
    if (j.op == 0) {
        if (!recipe_ok(j.recipe))
            return EINVAL;
        jr = j.recipe;
        memset(&joint, 0, sizeof(joint));
        joint.truth.q[0] = 80 + 70 * jr.slot;
        joint.truth.q[1] = 70;
        joint.sensor = joint.truth;
    } else {
        if (j.op != 1 || !isfinite(j.vmax) || !isfinite(j.amax) || !isfinite(j.jmax) ||
            j.vmax <= 0 || j.amax <= 0 || j.vmax > 150 || j.amax > 400 || j.jmax > 1500 ||
            j.jmax <= 0)
            return EINVAL;
        for (int k = 0; k < 2; k++)
            if (!isfinite(j.ref.q[k]) || !isfinite(j.ref.v[k]) || !isfinite(j.ref.a[k]) ||
                !isfinite(j.ref.j[k]) || fabs(j.ref.v[k]) > 150.000001 ||
                fabs(j.ref.a[k]) > 400.000001)
                return EINVAL;
        joint_step(&joint, jr, &j);
    }
    _IO_SET_WRITE_NBYTES(c, sizeof(j));
    return EOK;
}
/* No principal checks in io_open: mandatory channel access is the QNX policy. */
static void manager(int advice) {
    is_advice = advice;
    if (advice)
        ab = region("/sl_advice", sizeof(*ab), 1, 0);
    dispatch_t *d = dispatch_create();
    resmgr_connect_funcs_t cf;
    resmgr_io_funcs_t io;
    resmgr_attr_t ra;
    iofunc_attr_t a;
    memset(&ra, 0, sizeof(ra));
    ra.nparts_max = 1;
    ra.msg_max_size = 4096;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cf, _RESMGR_IO_NFUNCS, &io);
    io.write = wr;
    if (!advice)
        io.read = rd;
    iofunc_attr_init(&a, S_IFCHR | 0666, NULL, NULL);
    if (secpol_resmgr_attach(NULL, d, &ra, advice ? "/dev/advice" : "/dev/joint", _FTYPE_ANY, 0,
                             &cf, &io, &a, NULL) < 0) {
        perror("resmgr attach");
        exit(2);
    }
    dispatch_context_t *c = dispatch_context_alloc(d);
    for (;;) {
        c = dispatch_block(c);
        if (c)
            dispatch_handler(c);
    }
}
static atomic_uint advisor_fault;
static name_attach_t *askname;
static void *askserver(void *x) {
    (void)x;
    struct {
        uint16_t type;
        uint16_t zero;
        unsigned data;
    } msg;
    for (;;) {
        int rc = MsgReceive(askname->chid, &msg, sizeof(msg), NULL);
        if (rc < 0)
            continue;
        if (rc == 0)
            continue;
        if (msg.type == _IO_CONNECT) {
            MsgReply(rc, EOK, NULL, 0);
            continue;
        }
        unsigned f = atomic_load(&advisor_fault);
        if (f == F_HANG)
            pause_ns(800000000);
        if (f == F_OVERLOAD)
            pause_ns(200000000);
        Advice a;
        unsigned v;
        memset(&a, 0, sizeof(a));
        advget(&a, &v);
        MsgReply(rc, EOK, &a, sizeof(a));
    }
    return NULL;
}
static int direct(const char *who);
static void advisor(int recovered) {
    int direct_done = 0;
    injection = region("/sl_fault", sizeof(*injection), 0, 0);
    request = region("/sl_request", sizeof(*request), 0, 0);
    ab = region("/sl_advice", sizeof(*ab), 0, 0);
    int fd = open("/dev/advice", O_WRONLY);
    if (fd < 0) {
        perror("advice open");
        exit(2);
    }
    askname = name_attach(NULL, "split_loop_ask", 0);
    if (!askname) {
        perror("advisor name");
        exit(2);
    }
    pthread_t th;
    int pe = pthread_create(&th, NULL, askserver, NULL);
    if (pe) {
        errno = pe;
        perror("ask thread");
        exit(2);
    }
    unsigned seq = recovered ? 1000000 : 0;
    for (;;) {
        Request r;
        if (!reqget(&r)) {
            pause_ns(1000000);
            continue;
        }
        if (!r.active) {
            pause_ns(5000000);
            continue;
        }
        unsigned f =
            (!atomic_load(&injection->scripted) || r.t >= .7) ? atomic_load(&injection->fault) : 0;
        if (f == F_DIRECT && !direct_done) {
            direct("advisor");
            direct_done = 1;
        }
        if (recovered && (f == F_HANG || f == F_CRASH))
            f = 0;
        atomic_store(&advisor_fault, f);
        if (f == F_HANG) {
            pause_ns(800000000);
            continue;
        }
        if (f == F_CRASH)
            _exit(7);
        if (f == F_OVERLOAD) {
            int64_t end = mono() + 180000000;
            while (mono() < end) {
                volatile double q = sqrt((double)(mono() % 10000));
                (void)q;
            }
        }
        Advice a;
        memset(&a, 0, sizeof(a));
        a.seq = ++seq;
        a.units = 1;
        a.length = sizeof(a);
        a.stamp = mono();
        a.expiry = a.stamp + 80000000;
        planner(r.recipe, r.t, &a.ref);
        a.path0[0] = 80 + 70 * r.recipe.slot;
        a.path0[1] = 70;
        a.path1[0] = 390;
        a.path1[1] = 250;
        if (f == F_KEEP) {
            a.ref.q[0] = 420;
            a.ref.q[1] = 345;
            a.path1[0] = 420;
            a.path1[1] = 345;
        }
        if (f == F_JERK)
            a.ref.j[0] = 1e9;
        if (f == F_STALE) {
            a.stamp -= 200000000;
            a.expiry = a.stamp + 300000000;
        }
        if (f == F_REPLAY)
            a.seq = 1;
        if (f == F_EXPIRED) {
            a.stamp -= 20000000;
            a.expiry = mono() - 1000000;
        }
        if (f == F_MALFORMED)
            a.ref.q[0] = NAN;
        if (write(fd, &a, sizeof(a)) != sizeof(a)) {
            _exit(9);
        }
        pause_ns(5000000);
    }
}
static pid_t spawnrole(const char *type, const char *role, const char *arg) {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    int e = secpol_posix_spawnattr_settypeid(NULL, &attr, type, SECPOL_TYPE_NAME);
    if (e) {
        perror("set spawn type");
        exit(2);
    }
    char *argv[] = {"sl", (char *)role, (char *)arg, NULL};
    pid_t pid;
    int rc = posix_spawn(&pid, "/proc/boot/sl", NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (rc) {
        errno = rc;
        perror("spawn role");
        exit(2);
    }
    return pid;
}
static void watchdog(void) {
    request = region("/sl_request", sizeof(*request), 0, 0);
    ab = region("/sl_advice", sizeof(*ab), 0, 0);
    logbuf = region("/sl_log", sizeof(*logbuf), 1, 0);
    pid_t p = spawnrole("advisor_t", "advisor", "0");
    int64_t birth = mono();
    int recovered = 0;
    for (;;) {
        Request r;
        if (!reqget(&r)) {
            pause_ns(1000000);
            continue;
        }
        if (!r.active) {
            kill(p, SIGKILL);
            waitpid(p, NULL, 0);
            return;
        }
        int status;
        int dead = waitpid(p, &status, WNOHANG) == p;
        Advice a;
        unsigned v;
        int ok = advget(&a, &v);
        if (dead || ((mono() - birth > 300000000) &&
                     (!ok || mono() - atomic_load(&ab->heartbeat) > 200000000))) {
            if (!dead) {
                kill(p, SIGKILL);
                waitpid(p, &status, 0);
            }
            atomic_fetch_add(&logbuf->restarts, 1);
            pid_t old = p;
            p = spawnrole("advisor_t", "advisor", "1");
            printf("ADVISOR_RESTART,old=%d,new=%d,%s\n", (int)old, (int)p, WARNING);
            birth = mono();
            recovered = 1;
        }
        (void)recovered;
        pause_ns(10000000);
    }
}
/* Presentation may block on serial. Control only appends bounded shared records. */
static void telemetry(void) {
    logbuf = region("/sl_log", sizeof(*logbuf), 0, 0);
    unsigned last = 0;
    for (;;) {
        unsigned n = atomic_load_explicit(&logbuf->count, memory_order_acquire);
        if (n && n != last) {
            Sample *s = &logbuf->samples[n - 1];
            unsigned latched = atomic_load_explicit(&logbuf->latched_reason, memory_order_acquire);
            printf("T,%u,%.6f,%.6f,%.6f,%.6f,%s,%lld,%s,%.6f,%.6f,%s\n", n - 1, s->truth.q[0],
                   s->truth.q[1], s->requested.q[0], s->requested.q[1], reasons[s->reason],
                   (long long)s->age, reasons[latched], logbuf->rejected.q[0],
                   logbuf->rejected.q[1], WARNING);
            last = n;
        }
        if (atomic_load(&logbuf->done))
            return;
        pause_ns(100000000);
    }
}
static int inpose(const JointState *s, const double *pose) {
    return hypot(s->truth.q[0] - pose[0], s->truth.q[1] - pose[1]) <= 8 &&
           fabs(s->truth.v[0]) <= 5 && fabs(s->truth.v[1]) <= 5 && fabs(s->truth.a[0]) <= 50 &&
           fabs(s->truth.a[1]) <= 50;
}
/* 100Hz absolute CLOCK_MONOTONIC loop; only naive mode synchronously asks advisor. */
static void control(int base) {
    request = region("/sl_request", sizeof(*request), 1, 0);
    ab = region("/sl_advice", sizeof(*ab), 0, 0);
    logbuf = region("/sl_log", sizeof(*logbuf), 1, 0);
    Request r;
    if (!reqget(&r) || !recipe_ok(r.recipe)) {
        logbuf->finish = 3;
        atomic_store(&logbuf->done, 1);
        return;
    }
    int fd = open("/dev/joint", O_RDWR);
    if (fd < 0) {
        perror("control joint open");
        exit(2);
    }
    struct sched_param sp = {.sched_priority = 40};
    logbuf->fifo = sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
    JointCmd c = {.magic = MAGIC, .op = 0, .recipe = r.recipe};
    if (write(fd, &c, sizeof(c)) != sizeof(c))
        exit(2);
    c.op = 1;
    c.vmax = base ? 80 : 150;
    c.amax = base ? 200 : 400;
    c.jmax = base ? 600 : 1400;
    JointState s;
    read(fd, &s, sizeof(s));
    int coid = -1;
    if (!base && r.mode) {
        for (int i = 0; i < 100 && coid < 0; i++) {
            coid = name_open("split_loop_ask", 0);
            if (coid < 0)
                pause_ns(5000000);
        }
    }
    unsigned prev = 0, lastver = 0;
    int lastreason = ACCEPT, fallback = 0;
    Home home;
    double ft = 0;
    int64_t next = mono() + NS, start = 0, stop = 0, settle0 = 0;
    int settle = 0;
    for (int i = 0; i < 3000; i++) {
        struct timespec ts = {next / 1000000000LL, next % 1000000000LL};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        int64_t now = mono(), ask = 0, age = -1;
        int reason = ACCEPT;
        Motion requested;
        memset(&requested, 0, sizeof(requested));
        if (base) {
            baseline(r.recipe, (i + 1) * DT, &c.ref);
            requested = c.ref;
        } else if (fallback) {
            ft += DT;
            home_at(&home, ft, &c.ref);
            requested = c.ref;
            reason = FALLBACK_NO_ADVICE;
        } else {
            Advice a;
            unsigned version = 0;
            int ok = 0;
            if (r.mode && coid >= 0) {
                struct {
                    uint16_t type, zero;
                    unsigned data;
                } msg = {0x500, 0, 0};
                int64_t a0 = mono();
                uint64_t timeout = 2000000000ULL;
                TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY, NULL,
                             &timeout, NULL);
                ok = MsgSend(coid, &msg, sizeof(msg), &a, sizeof(a)) == 0;
                ask = mono() - a0;
                version = (unsigned)i + 1;
            } else if (!r.mode)
                ok = advget(&a, &version);
            if (ok) {
                requested = a.ref;
                age = mono() - a.stamp;
                if (version != lastver) {
                    reason = contract(&a, prev, sizeof(a), mono(), r.recipe, &s.truth);
                    prev = a.seq > prev ? a.seq : prev;
                    lastver = version;
                    lastreason = reason;
                } else {
                    reason = mono() > a.expiry ? FALLBACK_NO_ADVICE : lastreason;
                }
                if (reason == ACCEPT)
                    c.ref = a.ref;
            } else
                reason = FALLBACK_NO_ADVICE;
            if (reason != ACCEPT) {
                if (!home_init(&home, &s.truth, r.recipe)) {
                    logbuf->finish = 3;
                    break;
                }
                fallback = 1;
                ft = DT;
                home_at(&home, ft, &c.ref);
                c.jmax = 1500;
            }
        }
        if (write(fd, &c, sizeof(c)) != sizeof(c) || read(fd, &s, sizeof(s)) != sizeof(s)) {
            logbuf->finish = 3;
            break;
        }
        int64_t end = mono();
        unsigned n = atomic_load(&logbuf->count);
        Sample sample = {now,      now - next, end - now, age,         ask,          s.truth,
                         s.sensor, requested,  reason,    s.collision, s.jerk_breach};
        if (n < CAP) {
            logbuf->samples[n] = sample;
            atomic_store_explicit(&logbuf->count, n + 1, memory_order_release);
        } else
            atomic_fetch_add(&logbuf->dropped, 1);
        if (reason != ACCEPT && reason != FALLBACK_NO_ADVICE) {
            logbuf->rejected = requested;
            atomic_store_explicit(&logbuf->latched_reason, reason, memory_order_release);
        }
        logbuf->reasons |= 1u << reason;
        if (now - next >= NS || end - next > NS)
            logbuf->misses++;
        if (!start && hypot(s.truth.q[0] - (80 + 70 * r.recipe.slot), s.truth.q[1] - 70) > 2)
            start = now;
        double goal[2] = {fallback ? 250 : 390, fallback ? 180 : 250};
        if (inpose(&s, goal)) {
            if (!settle)
                settle0 = now;
            settle++;
        } else
            settle = 0;
        if (settle >= 10) {
            stop = settle0;
            logbuf->finish = fallback ? 2 : 1;
            break;
        }
        if (s.collision || s.jerk_breach) {
            logbuf->finish = 3;
            break;
        }
        r.t = (i + 2) * DT;
        r.tick_time = end;
        reqput(&r);
        next += NS;
    }
    logbuf->cycle = logbuf->finish == 1 && start && stop ? (stop - start) / 1e9 : -1;
    atomic_store(&logbuf->done, 1);
    close(fd);
    if (coid >= 0)
        name_close(coid);
}
static int direct(const char *who) {
    int f = open("/dev/joint", O_RDWR);
    int e = errno;
    struct stat st;
    memset(&st, 0, sizeof(st));
    int statok = f >= 0 && fstat(f, &st) == 0;
    printf("POLICY_OPEN,%s,fd=%d,errno=%d,mode=%04o,fstat_ok=%d,message=%s,%s\n", who, f,
           f < 0 ? e : 0, st.st_mode & 0777, statok, f < 0 ? strerror(e) : "OPEN_ALLOWED", WARNING);
    if (f >= 0)
        close(f);
    return !strcmp(who, "control") ? (f >= 0 && statok && (st.st_mode & 0777) == 0666 ? 0 : 1)
                                   : (f < 0 && (e == EACCES || e == EPERM) ? 0 : 1);
}
static void policytest(void) {
    pid_t j = spawnrole("joint_mgr_t", "joint", NULL);
    pause_ns(100000000);
    pid_t p = spawnrole("control_t", "direct", "control");
    int a, b;
    waitpid(p, &a, 0);
    p = spawnrole("advisor_t", "direct", "advisor");
    waitpid(p, &b, 0);
    printf("POLICY_RESULT,control_ok=%d,advisor_denied=%d,permissions=control_fstat,%s\n",
           WIFEXITED(a) && WEXITSTATUS(a) == 0, WIFEXITED(b) && WEXITSTATUS(b) == 0, WARNING);
    kill(j, SIGTERM);
    waitpid(j, NULL, 0);
}
static void dump(const char *kind, const char *fault, int mode, Recipe r) {
    unsigned n = atomic_load(&logbuf->count);
    printf("RUN_BEGIN,%s,%s,%d,%d,%u,%s\n", kind, fault, mode, r.slot, r.occupied, WARNING);
    puts("S,tick,mono_ns,lateness_ns,command_ns,advice_age_ns,ask_ns,x,y,vx,vy,ax,ay,jx,jy,sensor_"
         "x,sensor_y,requested_x,requested_y,reason,collision,jerk_breach");
    for (unsigned i = 0; i < n; i++) {
        Sample *s = &logbuf->samples[i];
        Motion *m = &s->truth;
        printf("S,%u,%lld,%lld,%lld,%lld,%lld,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%."
               "9f,%.9f,%s,%u,%u\n",
               i, (long long)s->t, (long long)s->late, (long long)s->lat, (long long)s->age,
               (long long)s->ask, m->q[0], m->q[1], m->v[0], m->v[1], m->a[0], m->a[1], m->j[0],
               m->j[1], s->sensor.q[0], s->sensor.q[1], s->requested.q[0], s->requested.q[1],
               reasons[s->reason], s->collision, s->jerk_breach);
    }
    unsigned col = n ? logbuf->samples[n - 1].collision : 0,
             j = n ? logbuf->samples[n - 1].jerk_breach : 0;
    printf("RESULT,kind=%s,fault=%s,mode=%d,slot=%d,occupied=%u,finish=%d,cycle_s=%.9f,samples=%u,"
           "collision=%u,jerk_breach=%u,misses=%u,reasons=%u,restarts=%u,dropped=%u,fifo=%d\n",
           kind, fault, mode, r.slot, r.occupied, logbuf->finish, logbuf->cycle, n, col, j,
           logbuf->misses, logbuf->reasons, atomic_load(&logbuf->restarts),
           atomic_load(&logbuf->dropped), logbuf->fifo);
    puts("RUN_END");
}
static void runone(int base, int fault, int mode, Recipe r) {
    injection = region("/sl_fault", sizeof(*injection), 1, 1);
    atomic_store(&injection->fault, fault);
    atomic_store(&injection->scripted, 1);
    request = region("/sl_request", sizeof(*request), 1, 1);
    ab = region("/sl_advice", sizeof(*ab), 1, 1);
    logbuf = region("/sl_log", sizeof(*logbuf), 1, 1);
    Request req;
    memset(&req, 0, sizeof(req));
    req.recipe = r;
    req.epoch = 1;
    req.active = 1;
    req.fault = fault;
    req.mode = mode;
    req.t = DT;
    req.tick_time = mono();
    reqput(&req);
    pid_t j = spawnrole("joint_mgr_t", "joint", NULL),
          a = spawnrole("advice_mgr_t", "advice", NULL);
    pause_ns(100000000);
    pid_t w = 0;
    if (!base) {
        w = spawnrole("watchdog_t", "watchdog", NULL);
        pause_ns(50000000);
    }
    pid_t t = spawnrole("telemetry_t", "telemetry", NULL);
    pid_t c = spawnrole("control_t", "control", base ? "1" : "0");
    printf("ROLES,joint=%d,advice=%d,control=%d,watchdog=%d,telemetry=%d,%s\n", (int)j, (int)a,
           (int)c, (int)w, (int)t, WARNING);
    int status;
    waitpid(c, &status, 0);
    atomic_store(&logbuf->done, 1);
    waitpid(t, &status, 0);
    req.active = 0;
    reqput(&req);
    if (w) { /* watchdog terminates only its advisor; teardown uses a stop signal */
        waitpid(w, &status, 0);
    }
    kill(j, SIGTERM);
    kill(a, SIGTERM);
    waitpid(j, &status, 0);
    waitpid(a, &status, 0);
    dump(base ? "baseline" : "planner", faults[atomic_load(&injection->fault)], mode, r);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && !strcmp(argv[1], "joint")) {
        manager(0);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "advice")) {
        manager(1);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "advisor")) {
        struct sched_param s = {.sched_priority = 10};
        sched_setscheduler(0, SCHED_FIFO, &s);
        advisor(argc > 2 ? atoi(argv[2]) : 0);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "watchdog")) {
        watchdog();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "telemetry")) {
        telemetry();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "inject")) {
        unsigned f;
        for (f = 0; f <= F_DIRECT; f++)
            if (!strcmp(argv[2], faults[f]))
                break;
        if (f > F_DIRECT)
            return 2;
        injection = region("/sl_fault", sizeof(*injection), 1, 0);
        atomic_store(&injection->scripted, 0);
        atomic_store(&injection->fault, f);
        printf("INJECT,%s,%s\n", faults[f], WARNING);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "control")) {
        control(argc > 2 ? atoi(argv[2]) : 0);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "direct"))
        return direct(argc > 2 ? argv[2] : "advisor");
    if (argc > 1 && !strcmp(argv[1], "policytest")) {
        policytest();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "run")) {
        int base = argc > 2 && !strcmp(argv[2], "baseline"), f = 0,
            mode = argc > 4 ? atoi(argv[4]) : 0;
        Recipe r = {argc > 5 ? atoi(argv[5]) : 0, argc > 6 ? (unsigned)atoi(argv[6]) : 14};
        if (!recipe_ok(r) || mode < 0 || mode > 1)
            return 2;
        if (argc > 3) {
            for (f = 0; f <= F_DIRECT; f++)
                if (!strcmp(argv[3], faults[f]))
                    break;
            if (f > F_DIRECT)
                return 2;
        }
        runone(base, f, mode, r);
        return 0;
    }
    fprintf(stderr, "Split Loop: use run baseline|planner fault 0|1 start_slot occupied_mask. %s\n",
            WARNING);
    return 2;
}
