/* atfork host test: the ordering pthread_atfork() promises and fork() relies
 * on — prepare handlers newest-first, then parent/child handlers in the order
 * they were registered.  (fork() itself is not called here: the shipped fork is
 * the raw SYS_FORK trap, so the wiring that calls these hooks is exercised on
 * the target, not on the host.  What is checked is the registry it consults.)
 *
 * The libc's <pthread.h> cannot be included (its <sched.h>/<unistd.h> clash with
 * the system headers), so the prototypes are mirrored, as in the other units. */

#include <stdio.h>
#include <string.h>

int  cact_pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
void cact_atfork_prepare(void);
void cact_atfork_parent(void);
void cact_atfork_child(void);

static char trace[64];
static int  trace_n;

static void t_prepare1(void) { trace[trace_n++] = 'P'; trace[trace_n++] = '1'; }
static void t_parent1(void)  { trace[trace_n++] = 'p'; trace[trace_n++] = '1'; }
static void t_child1(void)   { trace[trace_n++] = 'c'; trace[trace_n++] = '1'; }
static void t_prepare2(void) { trace[trace_n++] = 'P'; trace[trace_n++] = '2'; }
static void t_parent2(void)  { trace[trace_n++] = 'p'; trace[trace_n++] = '2'; }
static void t_child2(void)   { trace[trace_n++] = 'c'; trace[trace_n++] = '2'; }

/* A registration with holes: every handler is optional. */
static int  holes;
static void t_hole(void) { holes++; }

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void reset(void) { memset(trace, 0, sizeof trace); trace_n = 0; }

static int trace_is(const char *want) {
    return strcmp(trace, want) == 0;
}

int main(void) {
    /* An empty registry: every hook is a no-op. */
    reset();
    cact_atfork_prepare();
    cact_atfork_parent();
    cact_atfork_child();
    CK(trace_n == 0, "an empty registry ran \"%s\"", trace);

    /* Two registrations. */
    CK(cact_pthread_atfork(t_prepare1, t_parent1, t_child1) == 0, "atfork 1");
    CK(cact_pthread_atfork(t_prepare2, t_parent2, t_child2) == 0, "atfork 2");

    reset();
    cact_atfork_prepare();
    CK(trace_is("P2P1"), "prepare order is \"%s\", want newest-first \"P2P1\"", trace);

    reset();
    cact_atfork_parent();
    CK(trace_is("p1p2"), "parent order is \"%s\", want registration order \"p1p2\"", trace);

    reset();
    cact_atfork_child();
    CK(trace_is("c1c2"), "child order is \"%s\", want registration order \"c1c2\"", trace);

    /* A registration may leave handlers out; the others still all run. */
    CK(cact_pthread_atfork(NULL, t_hole, NULL) == 0, "atfork with holes");
    holes = 0;
    reset();
    cact_atfork_prepare();
    CK(trace_is("P2P1"), "a NULL prepare changed the prepare order (\"%s\")", trace);
    cact_atfork_parent();
    CK(holes == 1, "the parent handler with holes ran %d times", holes);
    CK(trace_is("P2P1p1p2"), "parent trace is \"%s\"", trace);
    cact_atfork_child();
    CK(trace_is("P2P1p1p2c1c2"), "child trace is \"%s\"", trace);

    printf("atfork: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
