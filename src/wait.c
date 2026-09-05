#include "sys/wait.h"
#include "string.h"

pid_t waitpid(pid_t pid, int *status, int options);

pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    (void)options;
    if (rusage) memset(rusage, 0, sizeof(*rusage));
    return waitpid(pid, status, 0);
}

pid_t wait3(int *status, int options, struct rusage *rusage) {
    return wait4(-1, status, options, rusage);
}

pid_t wait(int *status) {
    return wait4(-1, status, 0, 0);
}
