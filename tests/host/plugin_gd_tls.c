/*
 * dlfcn host-test fixture: an i686 plugin whose `__thread` uses the DEFAULT
 * (general-dynamic) model.  GNU ld turns its access into a pair of GOT words
 * (R_386_TLS_DTPMOD32 / _DTPOFF32 — the module id and the variable's offset)
 * plus a ___tls_get_addr call, which is the path the loader has to fill from the
 * module it gives the plugin's TLS.
 */

__thread int plugin_gd_var = 1;

int plugin_gd_get(void) { return plugin_gd_var; }
