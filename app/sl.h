#ifndef SL_H
#define SL_H
#include <stdatomic.h>
#include <stdint.h>
#define DT .01
#define NS 10000000LL
#define MAGIC 0x534c3031u
#define CAP 4096
#define WARNING "Not a medical device; not certified; not a patient-treatment product."
typedef struct {
    double q[2], v[2], a[2], j[2];
} Motion;
typedef struct {
    double tj, ta, tv, jerk, sign, total, start, end;
} Profile;
typedef struct {
    int slot;
    unsigned occupied;
} Recipe;
typedef struct {
    uint32_t magic, op;
    Recipe recipe;
    Motion ref;
    double vmax, amax, jmax;
} JointCmd;
typedef struct {
    Motion truth, sensor;
    uint64_t tick;
    unsigned collision, jerk_breach, invalid;
    int reason;
} JointState;
enum {
    ACCEPT,
    CLAMP_JERK,
    REJECT_KEEP_OUT,
    REJECT_STALE,
    REJECT_REPLAY,
    REJECT_EXPIRED,
    REJECT_MALFORMED,
    FALLBACK_NO_ADVICE
};
extern const char *reasons[];
int recipe_ok(Recipe r);
int keepout(Recipe r, double x, double y);
int swept(Recipe r, const double *q, const double *end);
void profile_init(Profile *, double, double, double, double, double);
void profile_at(const Profile *, double, double *, double *, double *, double *);
double baseline(Recipe, double, Motion *);
void joint_step(JointState *, Recipe, const JointCmd *);
int64_t mono(void);
#endif
