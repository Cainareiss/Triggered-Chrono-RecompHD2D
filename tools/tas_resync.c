/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* ct_boot --resync REF LAG OUT: keep a movie's inputs in step with a video
 * of the movie when the run's timing differs from the emulator the movie
 * was made on (a frame of lag more or less here and there and a press
 * lands before the game takes it, after which nothing matches).
 *
 * Every frame's picture is compared with the video's (64x56 gray, gain and
 * offset fitted per frame, the mean absolute difference, best of the video
 * frames s+LAG-1..s+LAG+1 for script frame s). A frame differs when that
 * passes BAD; the run has fallen out of step when DETECT of the last WIN
 * frames differ. Then the inputs are shifted: for each of the last few
 * presses before that point (latest first), insert 1..MAX_DELAY empty
 * frames before it, or drop up to MAX_SKIP script frames there, and play
 * each choice from a checkpoint to TRIAL_AHEAD frames past the point. The
 * smallest shift that stays in step all the way wins; failing that, the
 * one that stays in step longest, if it gets clearly further. If none
 * does, resynchronising stops and the inputs play on unshifted.
 *
 * Checkpoints are processes: every CK_EVERY frames the run forks, and the
 * child waits on a pipe for commands: TRY forks a trial, a copy of the
 * checkpoint that plays one shift, reports how far it stayed in step and
 * exits; GO makes the checkpoint itself the run, with the shift applied,
 * and the run that sent it exits. The process ct_boot started only waits
 * (as the subreaper of all of them) for whichever run is the last, and
 * exits with its status. OUT gets the inputs as played (replay.h), so a
 * plain `ct_boot --script OUT` repeats the run.
 *
 * Linux only (fork, prctl). */
#include "tas_resync.h"

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sched.h"

#define TW 64
#define TH 56
#define TSIZE (TW * TH)
#define BAD 15.0
#define WIN 60
#define DETECT 40
#define CK_EVERY 300
#define MAXCK 12
#define LOOKBACK 900       /* presses this far before the point are candidates */
#define MAX_EVENTS 6
#define MAX_DELAY 40
#define MAX_SKIP 15
#define TRIAL_AHEAD 900

typedef struct {
    long at;    /* our frame */
    int delta;  /* > 0: that many empty frames from `at`; < 0: script frames dropped */
} edit;

enum { CMD_TRY = 1, CMD_GO, CMD_QUIT };
typedef struct {
    int type;
    int delta;
    long at, stop, last_point;
} command;

typedef struct {
    long at;
    int delta;
    long fail;     /* frame it fell out of step, 0: stayed in step to its stop */
    double score;  /* summed error, the tie break */
} result;

typedef struct {
    long frame;
    pid_t pid;
    int cmd, res;   /* write end, read end */
} checkpoint;

static int active;
static const uint8_t *ref;
static long ref_n, lag;
static const char *out_path;
static int jobs = 8;
static int announce_fd = -1;

static int trial;
static long trial_stop;
static int trial_res = -1;
static result trial_result;

#define MAX_EDITS 8192
static edit edits[MAX_EDITS];
static int n_edits;

static uint16_t *applied;
static long applied_cap;

static uint8_t bad_ring[WIN];
static int bad_count;
static long last_in_step;
static long last_point;   /* the last point a search started from */
static int gave_up;

static checkpoint ck[MAXCK];
static int n_ck;

int resync_active(void)
{
    return active;
}

static void put_all(int fd, const void *p, size_t n)
{
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            _exit(3);
        c += w;
        n -= (size_t)w;
    }
}

static int get_all(int fd, void *p, size_t n)
{
    char *c = p;
    while (n) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

/* The waiting process: reap everything, follow the announced runs, exit
   with the last one's status. */
static void wait_for_runs(int fd, pid_t first)
{
    pid_t run = first;
    int status = 0;
    signal(SIGTERM, SIG_IGN);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    for (;;) {
        pid_t p = waitpid(-1, &status, 0);
        pid_t a;
        while (read(fd, &a, sizeof a) == (ssize_t)sizeof a)
            run = a;
        if (p < 0) {
            if (errno == EINTR)
                continue;
            exit(1);   /* no children left, the run never reported */
        }
        if (p == run)
            break;
    }
    kill(0, SIGTERM);   /* checkpoints and trials left over */
    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR)
        ;
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
}

int resync_init(const char *ref_path, long lag_frames, const char *out, int njobs)
{
    int fd = open(ref_path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size < TSIZE) {
        fprintf(stderr, "ct_boot: cannot read %s\n", ref_path);
        return -1;
    }
    ref = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (ref == MAP_FAILED) {
        fprintf(stderr, "ct_boot: cannot map %s\n", ref_path);
        return -1;
    }
    ref_n = (long)(st.st_size / TSIZE);
    lag = lag_frames;
    out_path = out;
    if (njobs > 0)
        jobs = njobs;
    int p[2];
    if (pipe(p)) {
        perror("ct_boot: pipe");
        return -1;
    }
    setpgid(0, 0);
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        perror("ct_boot: fork");
        return -1;
    }
    if (pid > 0) {
        close(p[1]);
        wait_for_runs(p[0], pid);
    }
    close(p[0]);
    announce_fd = p[1];
    active = 1;
    return 0;
}

static long shift_at(long f, int *empty)
{
    long s = 0;
    *empty = 0;
    for (int k = 0; k < n_edits; k++)
        if (edits[k].at <= f) {
            if (edits[k].delta > 0 && f < edits[k].at + edits[k].delta)
                *empty = 1;
            s += edits[k].delta;
        }
    return s;
}

static long script_frame(long f)
{
    int empty;
    return f - shift_at(f, &empty);
}

uint16_t resync_buttons(const replay *r, long f)
{
    int empty;
    long s = f - shift_at(f, &empty);
    uint16_t b = empty || s < 1 ? 0 : replay_buttons(r, s);
    if (f >= applied_cap) {
        long cap = applied_cap ? applied_cap * 2 : 1 << 16;
        while (cap <= f)
            cap *= 2;
        uint16_t *n = realloc(applied, (size_t)cap * sizeof *n);
        if (!n) {
            fprintf(stderr, "ct_boot: out of memory\n");
            exit(2);
        }
        memset(n + applied_cap, 0, (size_t)(cap - applied_cap) * sizeof *n);
        applied = n;
        applied_cap = cap;
    }
    applied[f] = b;
    return b;
}

static void thumb(const uint8_t *bgrx, float *t)
{
    for (int ty = 0; ty < TH; ty++)
        for (int tx = 0; tx < TW; tx++) {
            unsigned sum = 0;
            for (int y = ty * 4; y < ty * 4 + 4; y++)
                for (int x = tx * 4; x < tx * 4 + 4; x++) {
                    const uint8_t *p = bgrx + ((size_t)y * SCHED_WIDTH + x) * 4;
                    sum += p[0] + 2u * p[1] + p[2];
                }
            t[ty * TW + tx] = (float)sum / 64.0f;
        }
}

/* Mean absolute difference after fitting gain and offset (palettes and the
   video's levels differ). */
static double frame_error(const float *a, const uint8_t *b)
{
    double ma = 0, mb = 0;
    for (int k = 0; k < TSIZE; k++) {
        ma += a[k];
        mb += b[k];
    }
    ma /= TSIZE;
    mb /= TSIZE;
    double cov = 0, va = 0;
    for (int k = 0; k < TSIZE; k++) {
        cov += (a[k] - ma) * (b[k] - mb);
        va += (a[k] - ma) * (a[k] - ma);
    }
    double g = va > 1e-3 ? cov / va : 1.0;
    if (g < 0.3 || g > 3.0)
        g = 1.0;
    double c = mb - g * ma, e = 0;
    for (int k = 0; k < TSIZE; k++) {
        double d = g * a[k] + c - b[k];
        e += d < 0 ? -d : d;
    }
    return e / TSIZE;
}

/* -1: past the video's end. */
static double error_at(long f, const uint8_t *bgrx)
{
    static float t[TSIZE];
    long v = script_frame(f) + lag;
    if (v < 1 || v + 1 >= ref_n)
        return -1;
    thumb(bgrx, t);
    double best = 1e9;
    for (long k = v - 1; k <= v + 1; k++) {
        double e = frame_error(t, ref + (size_t)k * TSIZE);
        if (e < best)
            best = e;
    }
    return best;
}

static void drop_checkpoint(int k)
{
    kill(ck[k].pid, SIGKILL);
    close(ck[k].cmd);
    close(ck[k].res);
    memmove(&ck[k], &ck[k + 1], (size_t)(n_ck - k - 1) * sizeof *ck);
    n_ck--;
}

static void add_edit(long at, int delta)
{
    if (n_edits < MAX_EDITS)
        edits[n_edits++] = (edit){at, delta};
}

/* The checkpoint's side: returns when it becomes a trial or the run. */
static void serve(int cmd_fd, int res_fd)
{
    signal(SIGCHLD, SIG_IGN);   /* trials reap themselves */
    for (;;) {
        command c;
        if (get_all(cmd_fd, &c, sizeof c) || c.type == CMD_QUIT)
            _exit(0);
        if (c.type == CMD_TRY) {
            pid_t p = fork();
            if (p == 0) {
                close(cmd_fd);
                add_edit(c.at, c.delta);
                trial = 1;
                trial_stop = c.stop;
                trial_res = res_fd;
                trial_result = (result){c.at, c.delta, 0, 0};
                return;
            }
            continue;
        }
        if (c.type == CMD_GO) {
            add_edit(c.at, c.delta);
            last_point = c.last_point;
            pid_t me = getpid();
            put_all(announce_fd, &me, sizeof me);
            result ack = {0};
            put_all(res_fd, &ack, sizeof ack);
            close(cmd_fd);
            close(res_fd);
            return;
        }
    }
}

static void make_checkpoint(long f)
{
    int c[2], r[2];
    if (pipe(c))
        return;
    if (pipe(r)) {
        close(c[0]);
        close(c[1]);
        return;
    }
    fflush(stdout);
    fflush(stderr);
    pid_t p = fork();
    if (p < 0) {
        close(c[0]); close(c[1]); close(r[0]); close(r[1]);
        return;
    }
    if (p == 0) {
        close(c[1]);
        close(r[0]);
        serve(c[0], r[1]);
        return;   /* a trial, or the run from here */
    }
    close(c[0]);
    close(r[1]);
    if (n_ck == MAXCK)
        drop_checkpoint(0);
    ck[n_ck++] = (checkpoint){f, p, c[1], r[0]};
}

static int better(const result *a, const result *b)
{
    if (!b)
        return 1;
    long fa = a->fail ? a->fail : 1L << 40, fb = b->fail ? b->fail : 1L << 40;
    if (fa != fb)
        return fa > fb;
    if (!a->fail) {   /* both stayed in step: the smallest shift */
        int da = abs(a->delta), db = abs(b->delta);
        if (da != db)
            return da < db;
    }
    return a->score < b->score;
}

/* Out of step at frame d: find a shift, make it the run (this process
   exits), or give up. */
static void search(long d)
{
    long events[MAX_EVENTS];
    int n_ev = 0;
    for (long g = d; g > d - LOOKBACK && g > 1 && n_ev < MAX_EVENTS; g--)
        if (g < applied_cap && (applied[g] & ~applied[g - 1]))
            events[n_ev++] = g;
    long stop = d + TRIAL_AHEAD;   /* a trial that reaches the video's end passes */
    result best = {0};
    int have = 0, best_ck = -1;
    printf("resync: frame %ld out of step (in step through %ld); %d presses to try\n", d,
           last_in_step, n_ev);
    fflush(stdout);
    for (int e = 0; e < n_ev; e++) {
        int k = n_ck - 1;
        while (k >= 0 && ck[k].frame >= events[e] - MAX_SKIP)
            k--;
        if (k < 0)
            break;
        int deltas[MAX_DELAY + MAX_SKIP], nd = 0;
        for (int x = 1; x <= MAX_DELAY; x++)
            deltas[nd++] = x;
        for (int x = 1; x <= MAX_SKIP; x++)
            deltas[nd++] = -x;
        int sent = 0, got = 0, passed = 0;
        while (got < nd) {
            while (sent < nd && sent - got < jobs) {
                /* a drop goes before the press: it comes earlier */
                long at = deltas[sent] < 0 ? events[e] + deltas[sent] : events[e];
                command c = {CMD_TRY, deltas[sent], at, stop, d};
                put_all(ck[k].cmd, &c, sizeof c);
                sent++;
            }
            result r;
            if (get_all(ck[k].res, &r, sizeof r))
                break;
            got++;
            if (!r.fail)
                passed = 1;
            if (!have || better(&r, &best)) {
                best = r;
                have = 1;
                best_ck = k;
            }
        }
        if (passed)
            break;
    }
    long reach = have ? (best.fail ? best.fail : stop) : 0;
    if (!have || reach <= d + WIN) {
        printf("resync: no shift gets past frame %ld; playing on unshifted\n", d);
        fflush(stdout);
        gave_up = 1;
        return;
    }
    printf("resync: %+d frames at frame %ld (from the checkpoint at %ld): in step %s %ld\n",
           best.delta, best.at, ck[best_ck].frame, best.fail ? "to" : "through", reach);
    fflush(stdout);
    command go = {CMD_GO, best.delta, best.at, 0, d};
    put_all(ck[best_ck].cmd, &go, sizeof go);
    result ack;
    get_all(ck[best_ck].res, &ack, sizeof ack);
    /* The new run owns the older checkpoints; the newer ones go. */
    for (int k = n_ck - 1; k >= best_ck; k--)
        if (k > best_ck)
            kill(ck[k].pid, SIGKILL);
    _exit(0);
}

void resync_frame(long f, const uint8_t *bgrx)
{
    if (!active)
        return;
    double e = error_at(f, bgrx);
    int bad = e > BAD;
    bad_count += bad - bad_ring[f % WIN];
    bad_ring[f % WIN] = (uint8_t)bad;
    if (trial) {
        if (e >= 0)
            trial_result.score += e;
        if ((bad_count >= DETECT && f > trial_result.at) || f >= trial_stop || e < 0) {
            trial_result.fail = f >= trial_stop || e < 0 ? 0 : f;
            put_all(trial_res, &trial_result, sizeof trial_result);
            _exit(0);
        }
        return;
    }
    if (gave_up)
        return;
    if (e < 0) {
        printf("resync: the video ends at frame %ld\n", f);
        gave_up = 1;
        return;
    }
    if (bad_count == 0)
        last_in_step = f;
    if (bad_count >= DETECT) {
        if (f <= last_point) {
            /* The shift chosen last time got no further. */
            printf("resync: out of step again at frame %ld; playing on unshifted\n", f);
            gave_up = 1;
            return;
        }
        search(f);
        return;
    }
    if (f % CK_EVERY == 0)
        make_checkpoint(f);
}

void resync_fatal(long f)
{
    if (active && trial) {
        trial_result.fail = f;
        put_all(trial_res, &trial_result, sizeof trial_result);
        _exit(0);
    }
}

void resync_finish(long f)
{
    if (!active || trial)
        return;
    for (int k = n_ck - 1; k >= 0; k--)
        drop_checkpoint(k);
    FILE *o = fopen(out_path, "w");
    if (!o) {
        fprintf(stderr, "ct_boot: cannot write %s\n", out_path);
        return;
    }
    fprintf(o, "# Played by ct_boot --resync (lag %ld): %d shifts; in step with the video "
               "through frame %ld%s.\n", lag, n_edits, last_in_step,
            gave_up ? ", then unshifted" : "");
    for (int k = 0; k < n_edits; k++)
        fprintf(o, "# shift %+d at frame %ld\n", edits[k].delta, edits[k].at);
    long end = f < applied_cap ? f : applied_cap - 1;
    for (long g = 1; g <= end; g++) {
        if (!applied[g])
            continue;
        long h = g;
        while (h + 1 <= end && applied[h + 1] == applied[g])
            h++;
        fprintf(o, "%ld-%ld:0x%04X\n", g, h, applied[g]);
        g = h;
    }
    fclose(o);
    printf("resync: %d shifts, in step with the video through frame %ld; wrote %s\n", n_edits,
           last_in_step, out_path);
}

#else   /* not Linux */
#include <stdio.h>

int resync_init(const char *ref, long lag, const char *out, int jobs)
{
    (void)ref; (void)lag; (void)out; (void)jobs;
    fprintf(stderr, "ct_boot: --resync needs Linux\n");
    return -1;
}
int resync_active(void) { return 0; }
uint16_t resync_buttons(const replay *r, long f) { return replay_buttons(r, f); }
void resync_frame(long f, const uint8_t *bgrx) { (void)f; (void)bgrx; }
void resync_fatal(long f) { (void)f; }
void resync_finish(long f) { (void)f; }
#endif
