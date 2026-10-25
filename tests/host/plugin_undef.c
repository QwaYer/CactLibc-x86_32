/*
 * dlfcn host-test fixture: a plugin that names a symbol nobody defines, so
 * dlopen() has to fail with "undefined symbol" instead of leaving the PLT entry
 * unresolved.
 */

extern int no_such_symbol_anywhere(void);

int plugin_bad(void) { return no_such_symbol_anywhere(); }
