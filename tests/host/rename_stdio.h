/* Host-test renames.
 *
 * The libc units are compiled for the host with this header pre-included so
 * that their entry points do not collide with glibc's, and so their `errno`
 * (a macro over __errno_location(), see include/errno.h) lands in the test's
 * own slot rather than interposing glibc's.  See tests/run-host.sh.
 */
#ifndef CACT_HOST_RENAME_H
#define CACT_HOST_RENAME_H

#undef errno
#define __errno_location cact_host_errno_location

#define kprint         cact_kprint
#define putchar        cact_putchar
#define puts           cact_puts
#define rename         cact_rename
#define fopen          cact_fopen
#define fdopen         cact_fdopen
#define fclose         cact_fclose
#define fread          cact_fread
#define fwrite         cact_fwrite
#define fseek          cact_fseek
#define ftell          cact_ftell
#define fflush         cact_fflush
#define fgets          cact_fgets
#define feof           cact_feof
#define ferror         cact_ferror
#define clearerr       cact_clearerr
#define fgetc          cact_fgetc
#define ungetc         cact_ungetc
#define fputc          cact_fputc
#define fputs          cact_fputs
#define remove         cact_remove
#define fileno         cact_fileno
#define getline        cact_getline
#define tmpfile        cact_tmpfile
#define perror         cact_perror
#define vfprintf       cact_vfprintf
#define fprintf        cact_fprintf
#define printf         cact_printf
#define vsnprintf      cact_vsnprintf
#define snprintf       cact_snprintf
#define vsprintf       cact_vsprintf
#define sprintf        cact_sprintf
#define vasprintf      cact_vasprintf
#define asprintf       cact_asprintf
#define vdprintf       cact_vdprintf
#define dprintf        cact_dprintf
#define strtof         cact_strtof
#define popen          cact_popen
#define pclose         cact_pclose
#define open_memstream cact_open_memstream

#endif
