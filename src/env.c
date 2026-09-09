#include "stdlib.h"
#include "string.h"
#include "errno.h"

char **environ = 0;

/* TRUE once the environment array has been replaced by a malloc'd copy.
 * The initial array is borrowed from the exec stack (set by _start via
 * __cact_init_env) and must never be free()d. */
static int _env_owned = 0;

void __cact_init_env(char **envp) {
    if (envp && !environ) {
        environ = envp;
        _env_owned = 0;
    }
}

static int _env_count(void) {
    int n = 0;
    if (environ) while (environ[n]) n++;
    return n;
}

static int _env_find(const char *name, size_t nlen) {
    if (!environ) return -1;
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=')
            return i;
    }
    return -1;
}

int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    size_t nlen = strlen(name);
    int i = _env_find(name, nlen);
    if (i < 0) return 0;
    for (int j = i; environ[j]; j++) environ[j] = environ[j + 1];
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!value) value = "";
    size_t nlen = strlen(name);
    int i = _env_find(name, nlen);
    if (i >= 0 && !overwrite) return 0;

    size_t vlen = strlen(value);
    char *e = malloc(nlen + vlen + 2);
    if (!e) { errno = ENOMEM; return -1; }
    memcpy(e, name, nlen);
    e[nlen] = '=';
    memcpy(e + nlen + 1, value, vlen + 1);

    if (i >= 0) { environ[i] = e; return 0; }

    int n = _env_count();
    char **ne = malloc(sizeof(char *) * (n + 2));
    if (!ne) { free(e); errno = ENOMEM; return -1; }
    for (int k = 0; k < n; k++) ne[k] = environ[k];
    ne[n] = e;
    ne[n + 1] = 0;
    if (_env_owned && environ) free(environ);
    environ = ne;
    _env_owned = 1;
    return 0;
}

int putenv(char *string) {
    if (!string) { errno = EINVAL; return -1; }
    char *eq = strchr(string, '=');
    if (!eq) {
        size_t nlen = strlen(string);
        int i = _env_find(string, nlen);
        if (i < 0) return 0;
        for (int j = i; environ[j]; j++) environ[j] = environ[j + 1];
        return 0;
    }
    size_t nlen = (size_t)(eq - string);
    int i = _env_find(string, nlen);
    if (i >= 0) { environ[i] = string; return 0; }

    int n = _env_count();
    char **ne = malloc(sizeof(char *) * (n + 2));
    if (!ne) { errno = ENOMEM; return -1; }
    for (int k = 0; k < n; k++) ne[k] = environ[k];
    ne[n] = string;
    ne[n + 1] = 0;
    if (_env_owned && environ) free(environ);
    environ = ne;
    _env_owned = 1;
    return 0;
}
