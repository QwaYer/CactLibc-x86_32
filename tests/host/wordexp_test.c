/* wordexp host test: the shipped src/wordexp.c against the expected expansion
 * results.  Command substitution runs through the host's /bin/sh (glibc
 * popen), pathname expansion through a GLOB_NOCHECK stand-in (see shim.c). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
} wordexp_t;

int  cact_wordexp(const char *words, wordexp_t *we, int flags);
void cact_wordfree(wordexp_t *we);

#define WRDE_APPEND  0x01
#define WRDE_DOOFFS  0x02
#define WRDE_NOCMD   0x04
#define WRDE_REUSE   0x08
#define WRDE_UNDEF   0x20

#define WRDE_NOSPACE 1
#define WRDE_BADVAL  3
#define WRDE_CMDSUB  4
#define WRDE_SYNTAX  5

static int checks, fails;

static void expect_words(const char *lbl, const char *in, int flags,
                         const char *const *want, int nwant) {
    wordexp_t we;
    memset(&we, 0, sizeof we);
    int r = cact_wordexp(in, &we, flags);
    checks++;
    if (r != 0) { fails++; printf("FAIL %-20s rc=%d want 0\n", lbl, r); return; }
    if ((int)we.we_wordc != nwant) {
        fails++;
        printf("FAIL %-20s got %zu words want %d\n", lbl, we.we_wordc, nwant);
        cact_wordfree(&we);
        return;
    }
    for (int i = 0; i < nwant; i++) {
        const char *g = we.we_wordv[we.we_offs + i];
        if (strcmp(g, want[i])) {
            fails++;
            printf("FAIL %-20s word%d=\"%s\" want \"%s\"\n", lbl, i, g, want[i]);
        }
    }
    cact_wordfree(&we);
}

static void expect_rc(const char *lbl, const char *in, int flags, int want) {
    wordexp_t we;
    memset(&we, 0, sizeof we);
    int r = cact_wordexp(in, &we, flags);
    checks++;
    if (r != want) { fails++; printf("FAIL %-20s rc=%d want %d\n", lbl, r, want); }
    cact_wordfree(&we);
}

int main(void) {
    setenv("HOME", "/h", 1);
    setenv("MYVAR", "p q", 1);
    expect_words("simple", "a b c", 0, (const char *const[]){ "a","b","c" }, 3);
    expect_words("collapse ws", "  a \t b \n c  ", 0, (const char *const[]){ "a","b","c" }, 3);
    expect_words("empty", "", 0, 0, 0);
    expect_words("ws only", "   \t ", 0, 0, 0);
    expect_words("empty words", "'' \"\"", 0, (const char *const[]){ "","" }, 2);
    expect_words("single quotes", "'a b' c", 0, (const char *const[]){ "a b","c" }, 2);
    expect_words("double quotes", "\"a b\" c", 0, (const char *const[]){ "a b","c" }, 2);
    expect_words("backslash", "a\\ b", 0, (const char *const[]){ "a b" }, 1);
    expect_words("escaped $", "\\$HOME", 0, (const char *const[]){ "$HOME" }, 1);
    expect_words("dq escapes", "\"a\\\"b\"", 0, (const char *const[]){ "a\"b" }, 1);
    expect_words("punct kept", "a;b|c", 0, (const char *const[]){ "a;b|c" }, 1);

    expect_words("var split", "$MYVAR", 0, (const char *const[]){ "p","q" }, 2);
    expect_words("var joined", "x$MYVAR", 0, (const char *const[]){ "xp","q" }, 2);
    expect_words("var quoted", "\"$MYVAR\"", 0, (const char *const[]){ "p q" }, 1);
    expect_words("var braced", "${MYVAR}", 0, (const char *const[]){ "p","q" }, 2);
    expect_words("var braced q", "\"${MYVAR}\"", 0, (const char *const[]){ "p q" }, 1);
    expect_words("undef empty", "$NOPE_XYZ-", 0, (const char *const[]){ "-" }, 1);

    /* The braced parameter expansions, expectations checked against glibc. */
    setenv("W", "", 1);
    setenv("F", "a/b/c.txt", 1);
    expect_words("brace plain", "${MYVAR}", 0, (const char *const[]){ "p","q" }, 2);
    expect_words("brace default", "${MYVAR:-d}", 0, (const char *const[]){ "p","q" }, 2);
    expect_words("brace default unset", "${NOPE_XYZ:-d}", 0, (const char *const[]){ "d" }, 1);
    expect_words("brace default dash", "${NOPE_XYZ-d}", 0, (const char *const[]){ "d" }, 1);
    /* An unquoted empty expansion yields no field at all, which is why this is
     * zero words rather than one empty one (same as `$W`). */
    expect_words("brace empty not unset", "${W-d}", 0, 0, 0);
    expect_words("brace alt", "${MYVAR:+y}", 0, (const char *const[]){ "y" }, 1);
    expect_words("brace alt unset", "${NOPE_XYZ:+y}", 0, 0, 0);
    expect_words("brace len", "${#MYVAR}", 0, (const char *const[]){ "3" }, 1);
    expect_words("brace len empty", "${#W}", 0, (const char *const[]){ "0" }, 1);
    expect_words("brace short prefix", "${F#*/}", 0, (const char *const[]){ "b/c.txt" }, 1);
    expect_words("brace longest prefix", "${F##*/}", 0, (const char *const[]){ "c.txt" }, 1);
    expect_words("brace short suffix", "${F%/*}", 0, (const char *const[]){ "a/b" }, 1);
    expect_words("brace longest suffix", "${F%%/*}", 0, (const char *const[]){ "a" }, 1);
    expect_words("brace nomatch", "${F#zzz}", 0, (const char *const[]){ "a/b/c.txt" }, 1);
    expect_rc("brace undef", "${NOPE_XYZ}", WRDE_UNDEF, WRDE_BADVAL);
    expect_rc("brace question", "${NOPE_XYZ?}", 0, WRDE_BADVAL);
    expect_rc("brace bad", "${MYVAR@}", 0, WRDE_SYNTAX);

    expect_words("tilde", "~", 0, (const char *const[]){ "/h" }, 1);
    expect_words("tilde path", "~/x", 0, (const char *const[]){ "/h/x" }, 1);
    expect_words("tilde user", "~root", 0, (const char *const[]){ "~root" }, 1);
    expect_words("glob literal", "*.c", 0, (const char *const[]){ "*.c" }, 1);

    expect_words("cmdsub", "$(echo hi there)", 0, (const char *const[]){ "hi","there" }, 2);
    expect_words("cmdsub joined", "x$(echo y)", 0, (const char *const[]){ "xy" }, 1);
    expect_words("backtick", "`echo hi`", 0, (const char *const[]){ "hi" }, 1);
    expect_words("dq cmdsub", "\"$(echo a b)\"", 0, (const char *const[]){ "a b" }, 1);

    expect_rc("undef bad", "$NOPE_XYZ", WRDE_UNDEF, WRDE_BADVAL);
    expect_rc("nocmd", "$(echo hi)", WRDE_NOCMD, WRDE_CMDSUB);
    expect_rc("unterminated sq", "'abc", 0, WRDE_SYNTAX);
    expect_rc("unterminated dq", "\"abc", 0, WRDE_SYNTAX);

    /* Arithmetic.  The literal cases are checked against glibc (its wordexp
     * evaluates the same expressions); the variable cases follow POSIX, which
     * glibc's wordexp rejects with WRDE_SYNTAX and this one deliberately does
     * not copy. */
    setenv("MYN", "21", 1);
    expect_words("arith add", "$((1+2*3))", 0, (const char *const[]){ "7" }, 1);
    expect_words("arith paren", "$(( (1+2)*3 ))", 0, (const char *const[]){ "9" }, 1);
    expect_words("arith div", "$(( 7 / 2 ))", 0, (const char *const[]){ "3" }, 1);
    expect_words("arith mod", "$(( 7 % 2 ))", 0, (const char *const[]){ "1" }, 1);
    expect_words("arith neg", "$(( 0 - 5 ))", 0, (const char *const[]){ "-5" }, 1);
    expect_words("arith shift", "$(( 1 << 4 ))", 0, (const char *const[]){ "16" }, 1);
    expect_words("arith cmp", "$(( 3 > 2 ))", 0, (const char *const[]){ "1" }, 1);
    expect_words("arith unary", "$(( ~0 + 1 ))", 0, (const char *const[]){ "0" }, 1);
    expect_words("arith var", "$(( MYN * 2 ))", 0, (const char *const[]){ "42" }, 1);
    expect_words("arith var dollar", "$(( $MYN + 1 ))", 0, (const char *const[]){ "22" }, 1);
    expect_words("arith quoted", "\"$((2+2))\"", 0, (const char *const[]){ "4" }, 1);
    expect_rc("arith div0", "$(( 1 / 0 ))", 0, WRDE_SYNTAX);
    expect_rc("arith bad", "$(( 1 + ))", 0, WRDE_SYNTAX);
    expect_rc("arith unclosed", "$(( 1 + 2", 0, WRDE_SYNTAX);

    /* Substrings ${NAME:off[:len]}.  glibc's own wordexp reports WRDE_SYNTAX for
     * every one of these (it does not implement them), so the expectations here
     * are POSIX's, not glibc's — the deliberate divergence, like the arithmetic
     * above.  A bare `:` followed by `- = + ?` is still the default/alt operator;
     * a negative offset therefore needs the space (`${S: -2}`). */
    setenv("S", "abcdefgh", 1);
    expect_words("substr off", "${S:2}", 0, (const char *const[]){ "cdefgh" }, 1);
    expect_words("substr off len", "${S:2:3}", 0, (const char *const[]){ "cde" }, 1);
    expect_words("substr head", "${S:0:3}", 0, (const char *const[]){ "abc" }, 1);
    expect_words("substr neg off", "${S: -2}", 0, (const char *const[]){ "gh" }, 1);
    expect_words("substr neg off paren", "${S:(-3)}", 0, (const char *const[]){ "fgh" }, 1);
    expect_words("substr arith off", "${S: 2 + 1}", 0, (const char *const[]){ "defgh" }, 1);
    expect_words("substr quoted", "\"${S:2:3}\"", 0, (const char *const[]){ "cde" }, 1);
    expect_words("substr default dash", "${S:-2}", 0, (const char *const[]){ "abcdefgh" }, 1);
    /* An empty or past-the-end substring is an empty expansion: unquoted it
     * yields no field at all. */
    expect_words("substr empty", "${S:0:0}", 0, 0, 0);
    expect_words("substr past end", "${S:100}", 0, 0, 0);
    expect_words("substr of empty", "${W:2}", 0, 0, 0);
    expect_rc("substr neg len", "${S:2:-1}", 0, WRDE_SYNTAX);
    expect_rc("substr no off", "${S:}", 0, WRDE_SYNTAX);
    expect_rc("substr unclosed", "${S:2", 0, WRDE_SYNTAX);
    expect_rc("substr undef", "${NOPE_XYZ:2}", WRDE_UNDEF, WRDE_BADVAL);

    /* WRDE_DOOFFS */
    {
        wordexp_t we; memset(&we, 0, sizeof we);
        we.we_offs = 2;
        int r = cact_wordexp("a b", &we, WRDE_DOOFFS);
        checks++;
        if (r != 0 || we.we_wordc != 2 ||
            we.we_wordv[0] != 0 || we.we_wordv[1] != 0 ||
            strcmp(we.we_wordv[2], "a") || strcmp(we.we_wordv[3], "b") ||
            we.we_wordv[4] != 0) {
            fails++;
            printf("FAIL dooffs rc=%d wordc=%zu\n", r, we.we_wordc);
        }
        cact_wordfree(&we);
    }

    /* WRDE_APPEND */
    {
        wordexp_t we; memset(&we, 0, sizeof we);
        cact_wordexp("a", &we, 0);
        int r = cact_wordexp("b c", &we, WRDE_APPEND);
        checks++;
        if (r != 0 || we.we_wordc != 3 ||
            strcmp(we.we_wordv[0], "a") || strcmp(we.we_wordv[1], "b") ||
            strcmp(we.we_wordv[2], "c") || we.we_wordv[3] != 0) {
            fails++;
            printf("FAIL append rc=%d wordc=%zu\n", r, we.we_wordc);
        }
        cact_wordfree(&we);
    }

    /* WRDE_REUSE */
    {
        wordexp_t we; memset(&we, 0, sizeof we);
        cact_wordexp("a b c", &we, 0);
        int r = cact_wordexp("z", &we, WRDE_REUSE);
        checks++;
        if (r != 0 || we.we_wordc != 1 || strcmp(we.we_wordv[0], "z")) {
            fails++;
            printf("FAIL reuse rc=%d wordc=%zu\n", r, we.we_wordc);
        }
        cact_wordfree(&we);
    }

    printf("wordexp: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
