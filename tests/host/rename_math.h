/* Host-test renames for src/math.c (see rename_stdio.h for the rationale). */
#ifndef CACT_HOST_RENAME_MATH_H
#define CACT_HOST_RENAME_MATH_H

#undef errno
#define __errno_location cact_host_errno_location

#define sin    cact_sin
#define cos    cact_cos
#define tan    cact_tan
#define sqrt   cact_sqrt
#define floor  cact_floor
#define ceil   cact_ceil
#define round  cact_round
#define trunc  cact_trunc
#define fabs   cact_fabs
#define log    cact_log
#define log10  cact_log10
#define exp    cact_exp
#define pow    cact_pow
#define atan2  cact_atan2
#define atan   cact_atan
#define asin   cact_asin
#define acos   cact_acos
#define fmod   cact_fmod
#define modf   cact_modf
#define frexp  cact_frexp
#define ldexp  cact_ldexp

#endif
