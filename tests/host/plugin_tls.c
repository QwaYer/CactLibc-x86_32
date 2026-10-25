/*
 * dlfcn host-test fixture: an i686 plugin that carries its own `__thread`.
 *
 * Nothing here is executed on the host (it is i686 code) — the test maps it with
 * the real loader and then reads the resulting memory: the module the loader
 * added to the static TLS layout, the image it copied into the thread blocks,
 * and the displacement its R_386_TLS_TPOFF relocation wrote into the GOT slot.
 *
 * The variable is built for the initial-exec model on purpose: that is the model
 * a static TLS block can host, and GNU ld turns it into an R_386_TLS_TPOFF
 * against a GOT slot (the default, general-dynamic, would need __tls_get_addr
 * and a DTV, which CactOS does not have — see plugin_gd_tls.c).
 */

__thread int plugin_tls_var __attribute__((tls_model("initial-exec"))) = 0x5A5A5A5A;

int plugin_tls_get(void) { return plugin_tls_var; }
void plugin_tls_set(int v) { plugin_tls_var = v; }
