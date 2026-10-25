#include "regex.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"

/* POSIX BRE/ERE with leftmost-longest semantics.
 *
 * The pattern is compiled into a small tree (sequences, alternation, groups,
 * classes, repetition) and matched by a backtracking search driven by
 * *continuations*: every node, after matching, calls the continuation of the
 * rest of the pattern.  Because every path is explored, the top-level
 * continuation can pick the longest match at the leftmost start position —
 * which is what POSIX requires (a first-success walk would give Perl's
 * leftmost-first result for `a|ab`).
 *
 * Compatibility notes are in the README: subexpression spans for patterns where
 * several paths reach the same longest match are the first such path. */

#define REG_NGROUPS    32
#define REG_STEP_LIMIT 4000000L

struct cseq;

enum {
    OP_CHAR = 1, OP_ANY, OP_CLASS, OP_BOL, OP_EOL,
    OP_GROUP, OP_BACKREF, OP_ALT, OP_REP
};

struct cnode {
    unsigned char  op;
    int            ch;
    struct cseq   *sub;                 /* GROUP / REP operand */
    struct cseq   *alts;                /* ALT alternatives */
    int            nalts;
    int            min, max;            /* REP; max < 0 = unbounded */
    int            idx;                 /* GROUP / BACKREF: 1-based */
    unsigned char  cls[32];             /* CLASS bitmap */
    int            neg;
    struct cnode   *next_alloc;
};

struct cseq { struct cnode **v; int n; };

struct program {
    struct cnode *allocs;               /* arena chain, for regfree */
    struct cseq   top;
    int           ngroup;
    int           cflags;               /* kept for the matcher */
};

/* ── compiling ──────────────────────────────────────────────────────────── */

struct cstate {
    const char    *p;
    int            extended;
    int            depth;               /* nested groups; a ')' at depth 0 is a literal */
    struct program *prog;
    int            err;
};

static struct cnode *node_new(struct cstate *cs, int op) {
    struct cnode *n = (struct cnode *)calloc(1, sizeof(*n));
    if (!n) { cs->err = REG_ESPACE; return 0; }
    n->op = (unsigned char)op;
    n->max = 0;
    n->next_alloc = cs->prog->allocs;
    cs->prog->allocs = n;
    return n;
}

static int seq_add(struct cseq *q, struct cnode *n) {
    if (q->n % 8 == 0) {
        struct cnode **v = (struct cnode **)realloc(q->v, (size_t)(q->n + 8) * sizeof(*v));
        if (!v) return -1;
        q->v = v;
    }
    q->v[q->n++] = n;
    return 0;
}

static struct cseq *new_branch(struct cstate *cs) {
    struct cseq *b = (struct cseq *)calloc(1, sizeof(*b));
    if (!b) cs->err = REG_ESPACE;
    return b;
}

/* Is the cursor at the end of the current sequence? */
static int at_seq_end(struct cstate *cs) {
    char c = *cs->p;
    if (c == '\0') return 1;
    if (cs->extended) return (c == '|' || (c == ')' && cs->depth > 0));
    return (c == '\\' && (cs->p[1] == '|' ||
                          (cs->p[1] == ')' && cs->depth > 0)));
}

/* ...and after one character (used to recognise a trailing '$'). */
static int at_seq_end_after_one(struct cstate *cs) {
    char c = cs->p[1];
    if (c == '\0') return 1;
    if (cs->extended) return (c == '|' || (c == ')' && cs->depth > 0));
    return (c == '\\' && (cs->p[2] == '|' ||
                          (cs->p[2] == ')' && cs->depth > 0)));
}

static int at_alt(struct cstate *cs) {
    if (cs->extended) return *cs->p == '|';
    return (cs->p[0] == '\\' && cs->p[1] == '|');
}

/* A repetition operator where an operand is required: "*" / "+" / "?" / "{m,n}"
 * (with the backslash spellings in BRE).  Used only to pick the error code. */
static int parse_bounds(struct cstate *cs, int *min, int *max);

static int at_rep_op(struct cstate *cs) {
    char c = *cs->p;
    if (cs->extended) {
        if (c == '*' || c == '+' || c == '?') return 1;
        if (c == '{') {
            struct cstate probe = *cs;
            int lo, hi;
            return parse_bounds(&probe, &lo, &hi) == 1;
        }
        return 0;
    }
    if (c == '*') return 1;
    if (c != '\\') return 0;
    if (cs->p[1] == '+' || cs->p[1] == '?') return 1;
    if (cs->p[1] == '{') {
        struct cstate probe = *cs;
        int lo, hi;
        return parse_bounds(&probe, &lo, &hi) == 1;
    }
    return 0;
}

static void eat_alt(struct cstate *cs) { cs->p += cs->extended ? 1 : 2; }

/* ── bracket expressions ── */

struct clsinfo { const char *name; unsigned int len; };

static const struct clsinfo posix_classes[] = {
    { "alpha", 5 }, { "digit", 5 }, { "alnum", 5 }, { "upper", 5 }, { "lower", 5 },
    { "space", 5 }, { "blank", 5 }, { "punct", 5 }, { "print", 5 }, { "graph", 5 },
    { "cntrl", 5 }, { "xdigit", 6 }, { 0, 0 }
};

static int class_has(const char *name, unsigned int len, int c) {
    unsigned char uc = (unsigned char)c;
    if (len == 5 && !strncmp(name, "alpha", 5))  return isalpha(uc) != 0;
    if (len == 5 && !strncmp(name, "digit", 5))  return isdigit(uc) != 0;
    if (len == 5 && !strncmp(name, "alnum", 5))  return isalnum(uc) != 0;
    if (len == 5 && !strncmp(name, "upper", 5))  return isupper(uc) != 0;
    if (len == 5 && !strncmp(name, "lower", 5))  return islower(uc) != 0;
    if (len == 5 && !strncmp(name, "space", 5))  return isspace(uc) != 0;
    if (len == 5 && !strncmp(name, "blank", 5))  return (uc == ' ' || uc == '\t');
    if (len == 5 && !strncmp(name, "punct", 5))  return ispunct(uc) != 0;
    if (len == 5 && !strncmp(name, "print", 5))  return isprint(uc) != 0;
    if (len == 5 && !strncmp(name, "graph", 5))  return isgraph(uc) != 0;
    if (len == 5 && !strncmp(name, "cntrl", 5))  return iscntrl(uc) != 0;
    if (len == 6 && !strncmp(name, "xdigit", 6)) return isxdigit(uc) != 0;
    return -1;                          /* unknown class name */
}

static void set_bit(unsigned char *b, int c) { b[(unsigned char)c >> 3] |= (unsigned char)(1u << (c & 7)); }
static int  get_bit(const unsigned char *b, int c) { return (b[(unsigned char)c >> 3] >> (c & 7)) & 1; }

static struct cnode *parse_class(struct cstate *cs) {
    struct cnode *n = node_new(cs, OP_CLASS);
    if (!n) return 0;

    cs->p++;                                    /* '[' */
    if (*cs->p == '^') { n->neg = 1; cs->p++; }

    int first = 1;
    for (;;) {
        /* glibc reports an unterminated bracket as REG_BADPAT; POSIX names it
         * REG_EBRACK.  We follow glibc. */
        if (*cs->p == '\0') { cs->err = REG_BADPAT; return 0; }
        if (first && *cs->p == ']') { set_bit(n->cls, ']'); cs->p++; first = 0; continue; }
        if (*cs->p == ']' && !first) { cs->p++; break; }
        first = 0;

        if (cs->p[0] == '[' && cs->p[1] == ':') {
            const char *name = cs->p + 2;
            const char *close = name;
            while (*close && !(close[0] == ':' && close[1] == ']')) close++;
            if (*close == '\0') { cs->err = REG_BADPAT; return 0; }
            unsigned int len = (unsigned int)(close - name);
            int known = 0;
            for (const struct clsinfo *ci = posix_classes; ci->name; ci++)
                if (ci->len == len && !strncmp(ci->name, name, len)) { known = 1; break; }
            if (!known) { cs->err = REG_ECTYPE; return 0; }
            for (int c = 0; c < 256; c++)
                if (class_has(name, len, c) == 1) set_bit(n->cls, c);
            cs->p = close + 2;
            continue;
        }

        int lo = (unsigned char)*cs->p++;
        if (*cs->p == '-' && cs->p[1] != ']' && cs->p[1] != '\0') {
            cs->p++;
            int hi = (unsigned char)*cs->p++;
            if (lo > hi) { cs->err = REG_ERANGE; return 0; }
            for (int c = lo; c <= hi; c++) set_bit(n->cls, c);
        } else {
            set_bit(n->cls, lo);
        }
    }
    return n;
}

/* ── repetition bounds ── */

/* Parse "{m,n}" / "\{m,n\}" at the cursor.  Returns 1 on success, 0 when the
 * cursor holds no brace at all (the caller then treats it as a literal), and
 * -1 with cs->err set when a brace is present but malformed. */
static int parse_bounds(struct cstate *cs, int *min, int *max) {
    int ere = cs->extended;
    if (ere) {
        if (*cs->p != '{') return 0;
    } else {
        if (!(cs->p[0] == '\\' && cs->p[1] == '{')) return 0;
    }
    cs->p += ere ? 1 : 2;

    int lo = 0, hi = -1, have_lo = 0, have_hi = 0;
    while (isdigit((unsigned char)*cs->p)) { lo = lo * 10 + (*cs->p - '0'); cs->p++; have_lo = 1; }
    if (*cs->p == ',') {
        cs->p++;
        hi = 0;
        while (isdigit((unsigned char)*cs->p)) { hi = hi * 10 + (*cs->p - '0'); cs->p++; have_hi = 1; }
        if (!have_hi) hi = -1;                  /* {m,} */
    } else {
        hi = lo;                                /* {m} */
        have_hi = 1;
    }

    int closed = ere ? (*cs->p == '}') : (cs->p[0] == '\\' && cs->p[1] == '}');
    if (!have_lo || !closed) { cs->err = REG_EBRACE; return -1; }
    cs->p += ere ? 1 : 2;

    if (have_hi && hi >= 0 && lo > hi) { cs->err = REG_BADBR; return -1; }
    *min = lo;
    *max = have_hi && hi >= 0 ? hi : -1;
    return 1;
}

/* ── the parser ── */

static struct cseq parse_alt(struct cstate *cs);

/* One atom, then any repetition operators. */
static struct cnode *parse_piece(struct cstate *cs, int first) {
    struct cnode *atom = 0;

    char c = *cs->p;
    if (cs->extended) {
        if (c == '(') {
            cs->p++;
            if (++cs->prog->ngroup >= REG_NGROUPS) { cs->err = REG_ESUBREG; return 0; }
            int idx = cs->prog->ngroup;
            struct cseq *sub = new_branch(cs);
            if (!sub) return 0;
            cs->depth++;
            *sub = parse_alt(cs);
            cs->depth--;
            if (cs->err) return 0;
            if (*cs->p != ')') { cs->err = REG_EPAREN; return 0; }
            cs->p++;
            atom = node_new(cs, OP_GROUP);
            if (!atom) return 0;
            atom->idx = idx;
            atom->sub = sub;
        } else if (c == ')' && cs->depth > 0) {
            return 0;                            /* handled by the caller */
        } else if ((c == '*' || c == '+' || c == '?') && first) {
            cs->err = REG_BADRPT;                /* no operand */
            return 0;
        } else if (c == '{') {
            /* In an ERE an unescaped '{' is never literal: either a repetition
             * without an operand (BADRPT) or malformed (EBRACE). */
            int lo, hi;
            (void)parse_bounds(cs, &lo, &hi);
            if (!cs->err) cs->err = REG_BADRPT;
            return 0;
        } else if (c == '.') {
            cs->p++;
            atom = node_new(cs, OP_ANY);
        } else if (c == '^' && first) {
            cs->p++;
            atom = node_new(cs, OP_BOL);
        } else if (c == '$' && at_seq_end_after_one(cs)) {
            cs->p++;
            atom = node_new(cs, OP_EOL);
        } else if (c == '[') {
            atom = parse_class(cs);
        } else if (c == '\\') {
            if (cs->p[1] == '\0') { cs->err = REG_EESCAPE; return 0; }
            if (isdigit((unsigned char)cs->p[1])) {          /* GNU extension: backref */
                int idx = cs->p[1] - '0';
                if (idx > cs->prog->ngroup) { cs->err = REG_ESUBREG; return 0; }
                cs->p += 2;
                atom = node_new(cs, OP_BACKREF);
                if (!atom) return 0;
                atom->idx = idx;
            } else {
                cs->p++;
                atom = node_new(cs, OP_CHAR);
                if (!atom) return 0;
                atom->ch = (unsigned char)*cs->p++;
            }
        } else {
            cs->p++;
            atom = node_new(cs, OP_CHAR);
            if (!atom) return 0;
            atom->ch = (unsigned char)c;
        }
    } else {
        /* BRE */
        if (c == '\\') {
            char d = cs->p[1];
            if (d == '\0') { cs->err = REG_EESCAPE; return 0; }
            if (d == '(') {
                cs->p += 2;
                if (++cs->prog->ngroup >= REG_NGROUPS) { cs->err = REG_ESUBREG; return 0; }
                int idx = cs->prog->ngroup;
                struct cseq *sub = new_branch(cs);
                if (!sub) return 0;
                cs->depth++;
                *sub = parse_alt(cs);
                cs->depth--;
                if (cs->err) return 0;
                if (!(cs->p[0] == '\\' && cs->p[1] == ')')) { cs->err = REG_EPAREN; return 0; }
                cs->p += 2;
                atom = node_new(cs, OP_GROUP);
                if (!atom) return 0;
                atom->idx = idx;
                atom->sub = sub;
            } else if (d == ')') {
                if (cs->depth > 0) return 0;     /* end of a group */
                cs->p += 2;                      /* otherwise a literal ')' */
                atom = node_new(cs, OP_CHAR);
                if (!atom) return 0;
                atom->ch = ')';
            } else if (d == '|') {
                return 0;                        /* handled by the caller */
            } else if (d == '+' || d == '?') {
                return 0;                        /* repetition, without an operand */
            } else if (d == '{') {
                int lo, hi;
                (void)parse_bounds(cs, &lo, &hi);
                if (!cs->err) cs->err = REG_BADRPT;
                return 0;
            } else if (isdigit((unsigned char)d)) {
                int idx = d - '0';
                if (idx > cs->prog->ngroup) { cs->err = REG_ESUBREG; return 0; }
                cs->p += 2;
                atom = node_new(cs, OP_BACKREF);
                if (!atom) return 0;
                atom->idx = idx;
            } else {
                cs->p += 2;
                atom = node_new(cs, OP_CHAR);
                if (!atom) return 0;
                atom->ch = (unsigned char)d;
            }
        } else if (c == '.') {
            cs->p++;
            atom = node_new(cs, OP_ANY);
        } else if (c == '^' && first) {
            cs->p++;
            atom = node_new(cs, OP_BOL);
        } else if (c == '$' && at_seq_end_after_one(cs)) {
            cs->p++;
            atom = node_new(cs, OP_EOL);
        } else if (c == '[') {
            atom = parse_class(cs);
        } else if (c == '*' && first) {
            cs->p++;
            atom = node_new(cs, OP_CHAR);        /* a leading '*' is literal */
            if (!atom) return 0;
            atom->ch = '*';
        } else {
            cs->p++;
            atom = node_new(cs, OP_CHAR);
            if (!atom) return 0;
            atom->ch = (unsigned char)c;
        }
    }

    if (!atom || cs->err) return 0;

    /* One repetition operator per atom (POSIX).  A second one is then read as
     * whatever it means literally, rather than silently rebuilding `a**`. */
    int min = 0, max = -1, has_rep = 0;
    if (cs->extended) {
        if (*cs->p == '*')                                  { cs->p++; has_rep = 1; }
        else if (*cs->p == '+')                             { min = 1; cs->p++; has_rep = 1; }
        else if (*cs->p == '?')                             { max = 1; cs->p++; has_rep = 1; }
        else {
            int r = parse_bounds(cs, &min, &max);
            if (r < 0) return 0;
            has_rep = (r == 1);
        }
    } else {
        if (*cs->p == '*')                                  { cs->p++; has_rep = 1; }
        else if (cs->p[0] == '\\' && cs->p[1] == '+')       { min = 1; cs->p += 2; has_rep = 1; }
        else if (cs->p[0] == '\\' && cs->p[1] == '?')       { max = 1; cs->p += 2; has_rep = 1; }
        else {
            int r = parse_bounds(cs, &min, &max);
            if (r < 0) return 0;
            has_rep = (r == 1);
        }
    }

    if (has_rep) {
        struct cnode *r = node_new(cs, OP_REP);
        if (!r) return 0;
        r->min = min;
        r->max = max;
        struct cseq *sub = new_branch(cs);
        if (!sub) return 0;
        if (seq_add(sub, atom) != 0) { cs->err = REG_ESPACE; return 0; }
        r->sub = sub;
        atom = r;
    }
    return atom;
}

static struct cseq parse_seq(struct cstate *cs) {
    struct cseq q = { 0, 0 };
    int first = 1;

    while (!at_seq_end(cs)) {
        if (at_alt(cs)) break;
        struct cnode *n = parse_piece(cs, first);
        if (!n) {
            if (!cs->err && at_rep_op(cs)) cs->err = REG_BADRPT;
            break;
        }
        first = 0;
        if (seq_add(&q, n) != 0) { cs->err = REG_ESPACE; break; }
    }
    return q;
}

static struct cseq parse_alt(struct cstate *cs) {
    struct cseq first = parse_seq(cs);
    if (cs->err) return first;
    if (!at_alt(cs)) return first;

    struct cnode *alt = node_new(cs, OP_ALT);
    if (!alt) return first;

    int cap = 4;
    alt->alts = (struct cseq *)calloc((size_t)cap, sizeof(*alt->alts));
    if (!alt->alts) { cs->err = REG_ESPACE; return first; }
    alt->alts[alt->nalts++] = first;

    while (at_alt(cs)) {
        eat_alt(cs);
        struct cseq b = parse_seq(cs);
        if (cs->err) return first;
        if (alt->nalts == cap) {
            cap *= 2;
            struct cseq *grown = (struct cseq *)realloc(alt->alts, (size_t)cap * sizeof(*grown));
            if (!grown) { cs->err = REG_ESPACE; return first; }
            alt->alts = grown;
        }
        alt->alts[alt->nalts++] = b;
    }

    struct cseq out = { 0, 0 };
    if (seq_add(&out, alt) != 0) cs->err = REG_ESPACE;
    return out;
}

/* ── the matcher ────────────────────────────────────────────────────────── */

struct re_ctx {
    const unsigned char *s;
    int  len;
    int  cflags, eflags;
    int  gstart[REG_NGROUPS + 1];
    int  gend[REG_NGROUPS + 1];
    long steps;
};

typedef int (*cont_fn)(void *ctx, int pos);

struct kseq_ctx {
    const struct cseq *q;
    int      next;
    cont_fn  k;
    void    *kctx;
    struct re_ctx *rc;
};

static int m_seq(const struct cseq *q, int i, int pos, cont_fn k, void *kctx, struct re_ctx *rc);

static int ch_eq(int a, int b, int cflags) {
    if (a == b) return 1;
    if (!(cflags & REG_ICASE)) return 0;
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int kseq_fn(void *ctx, int pos) {
    struct kseq_ctx *ks = (struct kseq_ctx *)ctx;
    return m_seq(ks->q, ks->next, pos, ks->k, ks->kctx, ks->rc);
}

struct kgrp_ctx { int idx; cont_fn k; void *kctx; struct re_ctx *rc; };

static int kgrp_fn(void *ctx, int pos) {
    struct kgrp_ctx *g = (struct kgrp_ctx *)ctx;
    int old = g->rc->gend[g->idx];
    g->rc->gend[g->idx] = pos;
    int r = g->k(g->kctx, pos);
    if (!r) g->rc->gend[g->idx] = old;
    return r;
}

struct krep_ctx {
    const struct cnode *n;
    int done, prev, min, max;
    cont_fn k;
    void *kctx;
    struct re_ctx *rc;
};

static int krep_fn(void *ctx, int pos);

static int m_rep(struct krep_ctx *st, int pos) {
    if (st->done >= st->min) {
        if (st->k(st->kctx, pos)) return 1;      /* this many repetitions is allowed */
    }
    if (st->max >= 0 && st->done >= st->max) return 0;
    struct krep_ctx next = *st;
    next.prev = pos;                             /* remember where the body started */
    return m_seq(st->n->sub, 0, pos, krep_fn, &next, st->rc);
}

static int krep_fn(void *ctx, int pos) {
    struct krep_ctx *st = (struct krep_ctx *)ctx;
    if (pos == st->prev) {
        /* The body matched nothing: another iteration cannot make progress, so
         * only the final continuation is tried. */
        return (st->done + 1 >= st->min) ? st->k(st->kctx, pos) : 0;
    }
    struct krep_ctx next = *st;
    next.done = st->done + 1;
    return m_rep(&next, pos);
}

static int m_node(const struct cnode *n, int pos, cont_fn k, void *kctx, struct re_ctx *rc) {
    if (rc->steps++ > REG_STEP_LIMIT) return 1;

    switch (n->op) {
    case OP_CHAR:
        if (pos >= rc->len) return 0;
        if (!ch_eq(rc->s[pos], n->ch, rc->cflags)) return 0;
        return k(kctx, pos + 1);

    case OP_ANY:
        if (pos >= rc->len) return 0;
        if ((rc->cflags & REG_NEWLINE) && rc->s[pos] == '\n') return 0;
        return k(kctx, pos + 1);

    case OP_CLASS: {
        if (pos >= rc->len) return 0;
        int c = rc->s[pos];
        int hit = get_bit(n->cls, c);
        if (rc->cflags & REG_ICASE) {
            hit = hit || get_bit(n->cls, tolower((unsigned char)c))
                      || get_bit(n->cls, toupper((unsigned char)c));
        }
        if (n->neg) {
            if ((rc->cflags & REG_NEWLINE) && c == '\n') return 0;
            hit = !hit;
        }
        return hit ? k(kctx, pos + 1) : 0;
    }

    case OP_BOL:
        if (pos == 0 && !(rc->eflags & REG_NOTBOL)) return k(kctx, pos);
        if ((rc->cflags & REG_NEWLINE) && pos > 0 && rc->s[pos - 1] == '\n')
            return k(kctx, pos);
        return 0;

    case OP_EOL:
        if (pos == rc->len && !(rc->eflags & REG_NOTEOL)) return k(kctx, pos);
        if ((rc->cflags & REG_NEWLINE) && pos < rc->len && rc->s[pos] == '\n')
            return k(kctx, pos);
        return 0;

    case OP_GROUP: {
        int idx = n->idx;
        int old_s = rc->gstart[idx], old_e = rc->gend[idx];
        rc->gstart[idx] = pos;
        struct kgrp_ctx g = { idx, k, kctx, rc };
        int r = m_seq(n->sub, 0, pos, kgrp_fn, &g, rc);
        if (!r) { rc->gstart[idx] = old_s; rc->gend[idx] = old_e; }
        return r;
    }

    case OP_BACKREF: {
        int idx = n->idx;
        int gs = rc->gstart[idx], ge = rc->gend[idx];
        if (gs < 0 || ge < 0 || ge < gs) return 0;      /* unset: no match */
        int gl = ge - gs;
        if (pos + gl > rc->len) return 0;
        for (int i = 0; i < gl; i++)
            if (!ch_eq(rc->s[gs + i], rc->s[pos + i], rc->cflags)) return 0;
        return k(kctx, pos + gl);
    }

    case OP_ALT:
        for (int a = 0; a < n->nalts; a++)
            if (m_seq(&n->alts[a], 0, pos, k, kctx, rc)) return 1;
        return 0;

    case OP_REP: {
        struct krep_ctx st = { n, 0, -1, n->min, n->max, k, kctx, rc };
        return m_rep(&st, pos);
    }
    }
    return 0;
}

static int m_seq(const struct cseq *q, int i, int pos, cont_fn k, void *kctx, struct re_ctx *rc) {
    if (i >= q->n) return k(kctx, pos);
    struct kseq_ctx ks = { q, i + 1, k, kctx, rc };
    return m_node(q->v[i], pos, kseq_fn, &ks, rc);
}

/* ── the top-level continuation: keep the longest end ── */

struct top_ctx {
    struct re_ctx *rc;
    int  best_end;
    int  best_g[REG_NGROUPS + 1];
    int  best_ge[REG_NGROUPS + 1];
};

/* POSIX 9.1: matches of the same length are ordered by their subexpressions,
 * each as long as possible, with earlier subexpressions taking priority. */
static int better_subgroups(const struct top_ctx *t) {
    for (int i = 1; i <= REG_NGROUPS; i++) {
        int a_so = t->rc->gstart[i], a_eo = t->rc->gend[i];
        int b_so = t->best_g[i],    b_eo = t->best_ge[i];
        int a_len = (a_so >= 0 && a_eo >= a_so) ? a_eo - a_so : -1;
        int b_len = (b_so >= 0 && b_eo >= b_so) ? b_eo - b_so : -1;

        if (a_len != b_len) return a_len > b_len;
        if (a_len > 0 && a_so != b_so) return a_so < b_so;
    }
    return 0;
}

static int top_fn(void *ctx, int pos) {
    struct top_ctx *t = (struct top_ctx *)ctx;
    if (pos > t->best_end ||
        (pos == t->best_end && t->best_end >= 0 && better_subgroups(t))) {
        t->best_end = pos;
        memcpy(t->best_g, t->rc->gstart, sizeof(t->best_g));
        memcpy(t->best_ge, t->rc->gend, sizeof(t->best_ge));
    }
    return 0;                                   /* keep exploring: longest wins */
}

/* ── public API ─────────────────────────────────────────────────────────── */

static void program_free(struct program *p) {
    if (!p) return;
    struct cnode *n = p->allocs;
    while (n) {
        struct cnode *next = n->next_alloc;
        if (n->op == OP_ALT) {
            for (int i = 0; i < n->nalts; i++) free(n->alts[i].v);
            free(n->alts);
        } else if (n->sub) {
            free(n->sub->v);
            free(n->sub);
        }
        free(n);
        n = next;
    }
    free(p->top.v);
    free(p);
}

int regcomp(regex_t *preg, const char *pattern, int cflags) {
    if (!preg || !pattern) return REG_BADPAT;

    preg->re_nsub = 0;
    preg->re_priv = 0;

    struct program *prog = (struct program *)calloc(1, sizeof(*prog));
    if (!prog) return REG_ESPACE;
    prog->cflags = cflags;

    struct cstate cs;
    cs.p        = pattern;
    cs.extended = (cflags & REG_EXTENDED) != 0;
    cs.depth    = 0;
    cs.prog     = prog;
    cs.err      = 0;

    prog->top = parse_alt(&cs);

    if (!cs.err && *cs.p != '\0') cs.err = REG_EPAREN;    /* stray ')' */

    if (cs.err) {
        int e = cs.err;
        program_free(prog);
        return e;
    }

    preg->re_nsub = (size_t)prog->ngroup;
    preg->re_priv = prog;
    return 0;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags) {
    if (!preg || !preg->re_priv || !string) return REG_NOMATCH;

    struct program *prog = (struct program *)preg->re_priv;
    const unsigned char *s = (const unsigned char *)string;

    int start_off = 0;
    int len = (int)strlen(string);
    if ((eflags & REG_STARTEND) && pmatch && nmatch > 0) {
        start_off = (int)pmatch[0].rm_so;
        len       = (int)pmatch[0].rm_eo;
        if (start_off < 0 || len < start_off) return REG_NOMATCH;
    }

    for (int start = start_off; start <= len; start++) {
        struct re_ctx rc;
        rc.s = s;
        rc.len = len;
        rc.cflags = prog->cflags;
        rc.eflags = eflags;
        rc.steps = 0;
        for (int i = 0; i <= REG_NGROUPS; i++) { rc.gstart[i] = -1; rc.gend[i] = -1; }

        struct top_ctx t;
        t.rc = &rc;
        t.best_end = -1;

        m_seq(&prog->top, 0, start, top_fn, &t, &rc);

        if (t.best_end >= 0) {
            if (nmatch > 0 && pmatch) {
                pmatch[0].rm_so = (regoff_t)start;
                pmatch[0].rm_eo = (regoff_t)t.best_end;
                for (size_t i = 1; i < nmatch; i++) {
                    if ((int)i <= prog->ngroup) {
                        pmatch[i].rm_so = (regoff_t)t.best_g[i];
                        pmatch[i].rm_eo = (regoff_t)t.best_ge[i];
                    } else {
                        pmatch[i].rm_so = -1;
                        pmatch[i].rm_eo = -1;
                    }
                }
            }
            return 0;
        }
    }
    return REG_NOMATCH;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) {
    (void)preg;
    const char *msg;
    switch (errcode) {
    case 0:             msg = "no error"; break;
    case REG_NOMATCH:   msg = "no match"; break;
    case REG_BADPAT:    msg = "invalid regular expression"; break;
    case REG_ECOLLATE:  msg = "invalid collating element"; break;
    case REG_ECTYPE:    msg = "invalid character class"; break;
    case REG_EESCAPE:   msg = "trailing backslash"; break;
    case REG_ESUBREG:   msg = "invalid back reference"; break;
    case REG_EBRACK:    msg = "unbalanced brackets"; break;
    case REG_EPAREN:    msg = "unbalanced parentheses"; break;
    case REG_EBRACE:    msg = "unbalanced braces"; break;
    case REG_BADBR:     msg = "invalid repetition count"; break;
    case REG_ERANGE:    msg = "invalid character range"; break;
    case REG_ESPACE:    msg = "out of memory"; break;
    case REG_BADRPT:    msg = "repetition operator with no operand"; break;
    default:            msg = "unknown error"; break;
    }

    size_t need = strlen(msg) + 1;
    if (errbuf && errbuf_size > 0) {
        size_t copy = (need <= errbuf_size) ? need - 1 : errbuf_size - 1;
        memcpy(errbuf, msg, copy);
        errbuf[copy] = '\0';
    }
    return need;
}

void regfree(regex_t *preg) {
    if (!preg) return;
    program_free((struct program *)preg->re_priv);
    preg->re_priv = 0;
    preg->re_nsub = 0;
}
