#include "locale.h"
#include "string.h"

/* The C locale and nothing else — there is no locale data on a CactOS image.
 * The names accepted are the ones every program must be able to select; any
 * other name fails, as the standard allows when the locale is not available. */
static const char *const c_names[] = { "C", "POSIX", "C.UTF-8", 0 };

static struct lconv c_lconv = {
    .decimal_point  = ".",
    .thousands_sep  = "",
    .grouping       = "",
    .int_curr_symbol   = "",
    .currency_symbol   = "",
    .mon_decimal_point = "",
    .mon_thousands_sep = "",
    .mon_grouping      = "",
    .positive_sign     = "",
    .negative_sign     = "",
    .int_frac_digits = 127,
    .frac_digits     = 127,
    .p_cs_precedes   = 127,
    .p_sep_by_space  = 127,
    .n_cs_precedes   = 127,
    .n_sep_by_space  = 127,
    .p_sign_posn     = 127,
    .n_sign_posn     = 127,
    .int_p_cs_precedes  = 127,
    .int_p_sep_by_space = 127,
    .int_n_cs_precedes  = 127,
    .int_n_sep_by_space = 127,
    .int_p_sign_posn    = 127,
    .int_n_sign_posn    = 127,
};

char *setlocale(int category, const char *locale) {
    (void)category;
    if (!locale) return (char *)"C";            /* query */

    if (locale[0] == '\0') return (char *)"C";  /* "" selects the native locale, i.e. C here */
    for (int i = 0; c_names[i]; i++)
        if (strcmp(locale, c_names[i]) == 0) return (char *)"C";
    return 0;                                   /* not available */
}

struct lconv *localeconv(void) {
    return &c_lconv;
}
