/*
 * dlfcn host-test fixture: an i686 plugin image.
 *
 * Nothing here is executed on the host (it is i686 code) — the test maps it with
 * the real loader and then reads the resulting memory: that the .data image was
 * copied, that the relocation filled in clibc.so's addresses, and that the
 * initialiser ran.  The declarations are local because the fixture is built
 * freestanding for i686 and no 32-bit system headers are needed.
 */

extern unsigned int strlen(const char *s);   /* defined by clibc.so */
extern char *tzname[2];                      /* a clibc.so data symbol */

int plugin_data = 0x1234;

/* Filled in by the loader's relocation: the two pointers must come out equal to
 * clibc.so's strlen and tzname. */
void *plugin_strlen_ptr = (void *)strlen;
void *plugin_tzname_ptr = (void *)tzname;

/* Set by the initialiser below, so the test can see that dlopen() ran it. */
int plugin_inited;

int plugin_strlen(const char *s) { return (int)strlen(s); }   /* goes through the PLT */
int plugin_add(int a, int b) { return a + b + plugin_data; }

__attribute__((constructor)) static void plugin_ctor(void) { plugin_inited = 1; }
