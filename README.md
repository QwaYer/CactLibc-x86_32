# 🌵 CactLib/x86_32

<p align="center">
  <img src="https://img.shields.io/badge/version-2.0.0-green.svg?style=for-the-badge" alt="Version: 2.0.0">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C%2FASM-orange.svg?style=for-the-badge" alt="Language: C/ASM">
  <img src="https://img.shields.io/badge/output-clibc.so-green.svg?style=for-the-badge" alt="clibc.so">
  <img src="https://img.shields.io/badge/syscall-sysenter-purple.svg?style=for-the-badge" alt="sysenter">
  <img src="https://img.shields.io/badge/status-2.0.0-yellow.svg?style=for-the-badge" alt="2.0.0">
</p>

<p align="center">
  A <strong>freestanding C library</strong> for <a href="https://github.com/QwaYer/CactOS-x86_32"><strong>CactOS</strong></a> user space on <strong>i686</strong>.<br>
  No host libc — every OS entry goes through <code>sysenter</code> using the numbers from <a href="include/syscall.h"><code>include/syscall.h</code></a>, which must match <a href="https://github.com/QwaYer/CactKernel-x86_32"><strong>CactKernel-x86_32</strong></a> <code>syscalls.h</code>; everything beyond the <strong>15</strong> traps is a VFS-node ioctl from <a href="include/ioctl_abi.h"><code>ioctl_abi.h</code></a>.
</p>

---

## 📦 Overview

| | |
|---|---|
| **Sources** | 53 × `src/*.c` + `src/start.S`, `src/setjmp.S`, `src/thread_start.S` |
| **Public headers** | 74 files under `include/` (including `sys/*.h`) |
| **Syscall traps** | **15** (`SYS_SYSCALL_COUNT` mirrors the kernel enum); every other operation is a VFS-node ioctl |
| **Runtime deps** | None (build-time: `clang -m32`, GNU `binutils`, `meson`, `ninja`) |
| **Artifacts** | **`clibc.so`** (shared ET_DYN for PIE), **`build-meson/ld.so`** (PT_INTERP dynamic linker), **`build-meson/start.o`** (PIC `_start`) |

CactLib is the **contract surface** between user ELF binaries and the kernel. If you add or renumber a syscall in the kernel, you **must**:

1. Update **`include/syscall.h`** here to match **`Cact/kernel/core/syscall/syscalls.h`**.
2. Rebuild **`clibc.so`** (**`ninja -C build-meson`**).
3. **Re-link every user program** (init, shell, demos, drivers’ staged ELFs) against the new archive / shared object.

Adding a *feature* (a new ioctl) does not renumber traps: mirror the new command/struct in **`include/ioctl_abi.h`** instead, and wrap it in the matching `src/*.c`.

**Ecosystem:** **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** (integrator) · **CactKernel** · **CactLib** · **Cactsole** · **Cgoct** · **LocalRepoCactOS** (`cctkfs.img` packer).

---

## 🔨 Building

**Recommended — full workspace**

Clone **[CactOS-x86_32](https://github.com/QwaYer/CactOS-x86_32)** next to this tree and run **`ninja -C CactOS-x86_32/build-meson stage`** from their **common parent** — **CactOS** drives `ninja` here as part of the full userland + ISO pipeline.

**Standalone — this repository**

**Requirements:** **`clang`** with **`-m32`**, GNU **`binutils`**, **`meson`** and **`ninja`**.

```sh
git clone https://github.com/QwaYer/CactLibc-x86_32
cd CactLibc-x86_32

meson setup build-meson --cross-file cross/i686-cact-clang.ini
ninja -C build-meson        # build-meson/{clibc.so,ld.so,start.o}
ninja -C build-meson clean
```

**Default compile flags** (see [`meson.build`](meson.build)):

```meson
libc_c_args = ['-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib']
# clibc.so objects take -fPIC from the target's `pic: true`
```

**Static link example** (the archive is built on demand):

```sh
ninja -C build-meson src/libc.a
ld -m elf_i386 -nostdlib -o myprogram myprogram.o build-meson/src/libc.a
```

**Shared / PIE note:** `clibc.so` is built as **`ET_DYN`** with a fixed link script (`libc.ld`). PIE executables link against **`build-meson/start.o`** + **`build-meson/clibc.so`**.

> ⚠️ **i686 only.** Building `-m32` will fail on a pure 64-bit toolchain without multilib.

---

## 📂 Layout

```
CactLib-x86_32/
├── src/
│   ├── stdio.c      printf family (integers, %f/%e/%g, width/precision/#) — one formatter engine, puts, putchar, kprint
│   ├── stdlib.c     malloc/brk heap, exit, atoi, itoa, …
│   ├── string.c     memset, memcpy, strlen, strcmp, …
│   ├── unistd.c     POSIX-like file + process + mount + module wrappers
│   ├── socket.c     BSD sockets (stream/dgram) + getsockname/getpeername
│   ├── dns.c        dns_resolve() → /dev/net CACT_NETCTL_DNS_RESOLVE
│   ├── crypto.c     /dev/crypto primitives (SHA, HMAC, HKDF, AES-GCM, X25519, P-256, sig/X.509)
│   ├── tls.c        TLS 1.3 client — handshake, record layer, key schedule
│   ├── tls_roots.c  PEM→DER CA-bundle loader for the chain verifier
│   ├── nodeio.c     /dev, /proc and ioctl relay helpers (nio_*)
│   ├── signal.c     signals, masks, alarm, interval timers
│   ├── stat.c       stat, fstat
│   ├── dirent.c     getdents
│   ├── fcntl.c      open variants, symlink, readlink, link, unlink, …
│   ├── mman.c       mmap, munmap, mprotect
│   ├── locale.c     setlocale()/localeconv() — the C locale, and only it
│   ├── math.c       x87 math — sin/cos/tan/sqrt/exp/log/pow/…, no libm
│   ├── futex.c      futex + /proc/self/ctl helpers (pthread, semaphore)
│   ├── semaphore.c  POSIX semaphores — real blocking on the futex
│   ├── thread_local.c  per-thread errno: a registry keyed by stack range
│   ├── ldso.c       ld.so: loads DT_NEEDED, relocates, publishes the tables
│   ├── dirent.c     getdents + opendir/readdir/closedir/scandir
│   ├── fnmatch.c    fnmatch() — glob patterns
│   ├── glob.c       glob()/globfree()
│   ├── regex.c      POSIX BRE/ERE: regcomp/regexec/regerror/regfree
│   ├── wordexp.c    wordexp()/wordfree() — the shell's expansions, in-process
│   ├── sched.c      POSIX scheduling — SCHED_OTHER only, plus sched_yield
│   ├── spawn.c      posix_spawn/posix_spawnp + attrs and file actions
│   ├── strftime.c   strftime() / strptime() (C locale, UTC)
│   ├── sysconf.c    getpagesize(), sysconf()
│   ├── shm.c        SysV shared memory wrappers
│   ├── shm_open.c   POSIX shared memory — shm_open/shm_unlink in /dev/shm
│   ├── mqueue.c     POSIX message queues — mq_open & friends on /dev/mqueue
│   ├── termios.c    tcgetattr / tcsetattr
│   ├── time.c       clocks + nanosleep
│   ├── tz.c         timezone engine — POSIX TZ strings (localtime/mktime/%Z/%z)
│   ├── tls_static.c static TLS (__thread) — the per-thread block and its plan
│   ├── dlfcn.c      dlopen/dlsym/dlclose/dlerror — the runtime loader
│   ├── syscall.c    variadic syscall() helper (sysenter)
│   ├── start.S      user _start (non-PIC + PIC flavours)
│   └── …            atfork, env, epoll, eventfd, getopt, pthread, scanf, wait, …
├── include/
│   ├── syscall.h    authoritative SYS_* list — 15 traps, keep identical to the kernel!
│   ├── ioctl_abi.h  every relay command/struct (FDCTL_*/SOCKCTL_*/NETCTL_*/CRYPTCTL_*)
│   ├── nodeio.h     nio_open/read/write/ioctl/dev_cmd helpers
│   ├── socket.h     sockaddr_in helpers + dns_resolve + getsockname/getpeername
│   ├── crypto.h     cact_sha256/…, cact_aes*gcm_*, cact_x25519_*, cact_sig_verify, cact_x509_verify
│   ├── tls.h        cact_tls_connect/read/write/close/set_roots
│   ├── stdio.h string.h stdlib.h unistd.h locale.h wordexp.h …
│   └── sys/*.h
├── tests/           host-side unit tests (tests/run-host.sh, meson test)
├── meson.build
└── LICENSE          GPLv3
```

---

## 📖 API reference

### `stdio.h`

| Function | Role |
|----------|------|
| `printf` | Tiny `printf` — see format limits below |
| `puts` / `putchar` | Line / character output to fd 1 |
| `kprint` | Writes to **`/dev/console`** (kernel debug channel) through `nio_write` |
| `rename` | `rename(2)` wrapper |

> ⚠️ **`printf`** handles the integer, `%c`, `%s`, `%p`, `%n` and the GNU **`%m`** conversions plus the floating-point **`%f`/`%e`/`%g`** families (either case), the **`0`/`-`/`#`/`+`/space/`'`** flags, a width/precision (`.N`, `.*`, or the positional `*m$`) and the `hh`/`h`/`l`/`ll`/`L`/`z`/`t`/`j` length modifiers. Arguments may be addressed by position (`%2$s %1$s`). There is no locale **data** — `setlocale()`/`localeconv()` exist and only the C locale is selectable, so `'` grouping is inert exactly as in glibc's C locale and the decimal point is `.`.
>
> The whole family (**`printf`/`fprintf`/`vfprintf`/`snprintf`/`sprintf`/`asprintf`/`vasprintf`/`dprintf`/`vdprintf`**) is produced by **one format engine** over an output sink, so the FILE path, the buffer path and the descriptor path can no longer drift apart (the FILE and buffer paths used to be two near-identical copies — that is how `%i` and `%*` were missing from both). All of them return the number of bytes written, except the buffer path, which returns the length it *would* have written, and `asprintf`, which sizes one malloc'd buffer from that count. Output is staged through a 128-byte buffer, so a long line is a handful of `write`s instead of one per character.

### `stdlib.h`

| Function | Role |
|----------|------|
| `malloc` / `free` / `calloc` / `realloc` | See **malloc internals** below |
| `exit` | `_exit` via syscall |
| `atoi` | naive decimal parser |
| `itoa` / `hex_to_ascii` | small integer → ASCII helpers |

### `string.h`

`memset` · `memcpy` · `memcmp` · `strlen` · `strcmp` · `strncmp` · `strcpy` · `strncpy` · `strcat` · `buf_append` · `buf_append_int` …

### `unistd.h`

Core POSIX-like wrappers: `read`, `write`, `open`, `close`, `fork`, `execve`, `getpid`, `getppid`, `waitpid` (the `options` word is forwarded, so `WUNTRACED` reaches the kernel), `lseek`, `pipe`, `dup`, `dup2`, `select`, `poll`, `getcwd`, `chdir`, `mkdir`, `rmdir`, `ioctl`, `sleep`, `brk`, **`mount`/`umount`**, **`module_load` / `module_unload`**, and many more — always cross-check the `.c` file against [`syscall.h`](include/syscall.h).

### `socket.h`

Full BSD-style set: `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `setsockopt`, `getsockopt`, `getsockname`, `getpeername`.

Additionally **`dns_resolve(const char *name, uint32_t *out_ip_host)`** wraps **`CACT_NETCTL_DNS_RESOLVE` (0x3403)** on `/dev/net` — resolves a **dotted IPv4 literal** or performs a **blocking DNS A query** (requires the kernel to have a DNS server address from DHCP or `ip`). Return **`0`** on success, **`-1`** on error.

### `signal.h`

`signal`, `kill`, `killpg`, `raise`, `sigaction`, `sigprocmask`, `sigpending`, `sigsuspend`, `alarm`, `setitimer`, `sigaltstack` (+ `stack_t`, `SS_ONSTACK`/`SS_DISABLE`, `MINSIGSTKSZ`, `SA_ONSTACK`/`SA_SIGINFO`/`SA_NODEFER`), `sigqueue`, and the synchronous half `sigwait`/`sigwaitinfo`/`sigtimedwait`.

> `sigwait`/`sigwaitinfo`/`sigtimedwait` **consume** a signal from the set instead of having it delivered — no handler runs — which is what makes them different from `sigsuspend`. The kernel does the waiting (`CACT_PROCCTL_SIGWAIT`) and hands back the signal, its `si_code` and the `sigqueue` value; the number that comes out is the same value this header's `SIG*` macros hold, so `case SIGINT:` works on it. `sigqueue` carries a value (`union sigval` → `si_value`), and because the kernel keeps one pending value per signal, a second `sigqueue` for an already-pending signal does not queue another instance — POSIX's own rule for non-realtime signals. An unbounded wait is woken by the signal itself; the timed form polls the kernel's timer in one-tick slices (a timed kernel park is not signal-interruptible), so its timeout granularity is the 10 ms tick.

> The alternate signal stack is carried **per task by the kernel**: a handler installed with `SA_ONSTACK` runs on it, the frame stores the interrupted `esp` so `sigreturn` returns to the original stack, a second signal during that handler stays on the current stack (POSIX: one handler at a time), an unmapped or too-small alt stack falls back to the normal stack rather than stranding the signal, and `exec` disables it. **`SA_SIGINFO` is delivered**: the kernel builds a `siginfo_t` and a `ucontext_t` (interrupted registers in `REG_*` order) into the signal frame and enters the handler as `void handler(int, siginfo_t *, void *)`. `si_code` and `si_addr` are truthful — `SI_USER` for a sent signal, `SI_KERNEL` plus the faulting address for one the kernel generated — and `si_value` carries what `sigqueue()` sent (`SI_QUEUE`). The handler's first argument is the signal's **mask value** (the same as this header's `SIG*` macros), so `case SIGINT:` works. The delivered signal is blocked while its handler runs and the interrupted mask is restored by `sigreturn` (POSIX), unless the disposition sets `SA_NODEFER`.

### Other headers

| Header | Highlights |
|--------|------------|
| **`sys/mman.h`** | `mmap`, `munmap`, `mprotect`; `shm_open`/`shm_unlink` — POSIX shared memory as names in the `/dev/shm` tmpfs (a `MAP_SHARED` mapping of the descriptor shares real frames; see the limits table) |
| **`mqueue.h`** | `mq_open` `mq_close` `mq_unlink` `mq_send` `mq_receive` `mq_timedsend` `mq_timedreceive` `mq_getattr` `mq_setattr` `mq_notify` (+ `struct mq_attr`, and `struct sigevent` in `signal.h`). A queue is a file in the queue filesystem the kernel mounts at `/dev/mqueue`, so `mq_open` is open-or-create; a message carries a priority, so send/receive are ioctls rather than `write`/`read`. Blocking, `O_NONBLOCK`, and the timed forms all work (see the limits table) |
| **`math.h`** | `sin` `cos` `tan` `sqrt` `atan` `atan2` `asin` `acos` `exp` `log` `log10` `pow` `fmod` `modf` `frexp` `ldexp` `floor` `ceil` `round` `trunc` `fabs` (+ `HUGE_VAL`/`INFINITY`/`NAN`, `isnan`/`isinf`/`isfinite`) — **x87, no libm** |
| **`semaphore.h`** | `sem_init` `sem_destroy` `sem_wait` `sem_trywait` `sem_timedwait` `sem_post` `sem_getvalue` — blocking; `pshared != 0` uses the kernel's shared futex (keyed by the futex word's **physical page**, so two processes mapping the same object rendezvous on it) |
| **`dirent.h`** | `getdents`, `DIR`, `opendir` `readdir` `readdir_r` `closedir` `rewinddir` `telldir` `seekdir` `dirfd` `scandir` `alphasort` |
| **`fnmatch.h`** / **`glob.h`** | Glob matching and expansion (`FNM_PATHNAME`/`FNM_PERIOD`/`FNM_CASEFOLD`, `GLOB_NOCHECK`/`GLOB_MARK`/`GLOB_APPEND`/`GLOB_DOOFFS`/`GLOB_TILDE`) |
| **`getopt.h`** | `getopt_long`, `getopt_long_only`, `struct option` (short `getopt` is in `unistd.h`) |
| **`regex.h`** | `regcomp` `regexec` `regerror` `regfree` — POSIX BRE/ERE, leftmost-longest, subexpression capture, `REG_ICASE`/`REG_NEWLINE`/`REG_NOSUB`/`REG_NOTBOL`/`REG_NOTEOL`/`REG_STARTEND` |
| **`spawn.h`** | `posix_spawn`/`posix_spawnp`, `posix_spawnattr_*` (`RESETIDS`/`SETPGROUP`/`SETSIGDEF`/`SETSIGMASK`), `posix_spawn_file_actions_*` (`open`/`close`/`dup2`). The child's exec error is reported back through a close-on-exec pipe, as in glibc |
| **`wordexp.h`** | `wordexp` `wordfree` — quoting, escapes, `$VAR`, `${name:-x}`/`:+`/`:=`/`#`/`%`/`${#name}`, arithmetic `$((…))`, `~`, `$(…)`/backticks and pathname expansion |
| **`locale.h`** | `setlocale` `localeconv` — no locale data ships, so only `"C"` is selectable (and `printf`'s `'` flag is therefore inert) |
| **`errno.h`** | `errno` is a **macro over `__errno_location()`** — per thread (see below) |
| **`sys/ldt.h`** | `set_thread_area` (+ `struct user_desc`) — installs the task's `%gs` base directly; CactOS has no LDT, so the rest of the descriptor is ignored |
| **`tls_layout.h`** | the static TLS layout ld.so computes and the libc fills per thread (`cact_tls_layout_t`, `cact_tls_plan`, plus the `cact_tls_reserve`/`cact_tls_take` spare room an object loaded later draws from) — internal contract between the two, not for programs |
| **`dlfcn.h`** | `dlopen` `dlsym` `dlvsym` `dlclose` `dlerror` `dladdr` (+ `Dl_info`) and `RTLD_LAZY`/`NOW`/`GLOBAL`/`LOCAL`/`DEFAULT`/`NEXT` — a real runtime loader (see below) |
| **`link.h`** | `dl_iterate_phdr` (+ `struct dl_phdr_info`). `dlpi_addr` is always 0 (no load bias) and the program headers are the loader's own copies, because a CactOS image's ELF header is not mapped. The program is named `<main>` where glibc uses `""` |
| **`dl_elf.h`** | the ELF32 shapes and PT_/DT_/R_386_ constants the two loaders share (a program reaches them through `link.h`, which needs `Elf32_Phdr`) |
| **`dl_objects.h`** | the already-mapped-object table ld.so publishes and the libc extends — internal to the two loaders |
| **`shm.h`** | `shmget`, `shmat`, `shmdt`, `shmctl` |
| **`time.h`** | `gettimeofday`, `clock_gettime` (**`CLOCK_REALTIME`** from the RTC via `/proc/wallclock`; **`CLOCK_MONOTONIC`** from `/proc/time`), `clock_settime` (**`CLOCK_REALTIME`** only, root), `nanosleep`, `tzset` + `tzname`/`timezone`/`daylight`, `strftime`/`strptime` (C locale, `TZ`-aware `%Z`/`%z`) |
| **`crypto.h`** | `/dev/crypto` primitives: SHA-256/384, HMAC (+verify), HKDF, AES-128/256-GCM seal/open, X25519/P-256, `cact_sig_verify`, `cact_x509_verify` |
| **`tls.h`** | `cact_tls_connect/read/write/close`, `cact_tls_set_roots`, `cact_tls_error` — TLS 1.3 client, keys stay in the process |
| **`termios.h`** | `tcgetattr`, `tcsetattr` |
| **`fcntl.h`** / **`poll.h`** / **`select.h`** | open flags, non-blocking pollable fds |

> 💡 Network configuration and ICMP do **not** go through `syscall()`: use the `/dev/net` ioctls (`CACT_NETCTL_*` in [`ioctl_abi.h`](include/ioctl_abi.h)) through the **`nio_dev_cmd("net", …)`** helper in [`nodeio.h`](include/nodeio.h) — that is what `ip`, `ping` and `dhcpd` do.

---

## 🧠 `malloc` internals

The heap is a **singly linked list** of blocks carved out of the **`brk`** region returned by the kernel.

```
┌──────────────────┬───────────────────────┬──────────────┐
│  block_header    │  user payload        │  slack       │
│  16 bytes        │  8-byte aligned      │  optional    │
└──────────────────┴───────────────────────┴──────────────┘
```

```c
struct block_header {
    uint32_t magic;              /* 0xA110CA7E — canary */
    uint32_t size;             /* usable bytes excluding header */
    uint32_t is_free;          /* 1 = on free list */
    struct block_header *next;
};
```

- **First-fit** scan across `free_list`.
- **Coalescing** of adjacent free blocks on `free()`.
- **`sbrk`** requests are rounded up to at least **`max(header + size, 4096)`** bytes to reduce syscall chatter.
- **Thread-safe**: allocation is serialized by a short spinlock, so the heap survives `pthread_create` (the critical sections are short and the holder is preempted, so a spinner always makes progress).

---

## 🧪 Tests

`tests/` holds an in-tree host suite: the shipped `src/*.c` compiled for the build machine with its entry points renamed and its `errno` redirected (`tests/host/rename_*.h`, `tests/host/shim.c`), then compared with glibc/libm — the `printf`, `math`, `wordexp`, `time`, `thread_local`, `signal`, `tz`, `tls`, `semaphore`, `dlfcn`, `strftime`, `glob`, `sched`, `spawn`, `atfork`, `pthread`, `shm_open` and `mqueue` units today. The `dlfcn` one builds i686 plugins with `ld` and drives the real loader against them, reading the mapped memory back (two of them carry a `__thread` of their own — one initial-exec, one general-dynamic — and one exists only to be loaded into a namespace of its own); the `glob` one builds a real directory tree in a temp dir and checks every flag against glibc's `glob()` on the same tree; the `spawn` one really forks and execs `/bin/true`, so a refused scheduling policy is seen as the parent's `posix_spawn()` returning the error. Run it with `tests/run-host.sh`, or `meson test -C build-meson`. The kernel keeps its own host unit tests and headless `check`.

## ⚠️ Known limitations

Honest list as of 2.0.0 — the interface is there; what follows is where the platform cannot answer, and the choices that are deliberate:

| Area | What is true |
|------|--------------|
| **No zoneinfo tree in the image** | `TZ` is understood as a POSIX TZ string and a named zone is read as a TZif file from `/usr/share/zoneinfo`, but a CactOS image ships **no** such tree, so a named zone is UTC until one is staged (yes, that file's POSIX footer is what makes the "slim" tzdata files usable). For a `struct tm` from `gmtime()` — where POSIX leaves `%z`/`%Z` undefined — the configured standard zone is reported rather than glibc's "GMT"/"+0000", because this `struct tm` has no `tm_gmtoff`/`tm_zone`; and for two historic DST periods glibc prints a different abbreviation than the zone file's own type label (this libc prints the file's). |
| **No settable monotonic clock** | `CLOCK_REALTIME` can be set (root only, `CACT_SYSCTL_SETWALLCLOCK`); the wall clock is an anchor plus elapsed ticks, so setting it re-anchors and time keeps running forward. Nothing is written to the CMOS chip (a reboot re-reads the hardware date), and `clock_getres` reports the kernel's 1 ms timer. |
| **`tan` near a pole** | `sin`/`cos`/`tan` reduce modulo π/2 with a Payne–Hanek step above \|x\| = 1e6 (1280 bits of 2/π, so the reduction error does not grow with \|x\|), and below that the fast `fprem`-against-80-bit-2π path is effectively exact. The limit left is the pole: `tan` is `-cot(r)` in the odd quadrants, so where `r` is tiny its relative error grows like the argument's absolute error (≈2⁻⁶⁴) over `r`. |
| **No fixed-priority class** | The MLFQ is a **dynamic** feedback scheduler — a task's band is earned from its own run/sleep behaviour and decays when it hogs the CPU — and it deliberately has no fixed-priority dimension. `SCHED_FIFO`/`SCHED_RR` are therefore defined (a portable `<sched.h>` must define them) but refused with `EINVAL`, `sched_priority` is 0, and only the calling process can be named (`EPERM` otherwise). That is POSIX's own answer for a policy an implementation does not support; a 1…99 range would have been a fiction. `sched_yield()` is real (`CACT_PROCCTL_YIELD`). |
| **Static TLS is finite** | A shared object's own `__thread` goes into the `CACT_TLS_RESERVE` (1 KiB) of spare room that every thread block keeps below its modules, and `CACT_TLS_MAX_MODULES` (8) modules in all can exist. One that does not fit is refused with an error rather than mis-resolved, and `errno` itself still finds its slot by the running thread's stack range rather than through a compiler TLS anchor. |
| **`dlmopen` namespaces share the pinned objects** | A namespace is a **symbol scope**, not a second address space: every image is mapped once, at its link-time address, so a library can live in only one namespace, and the pinned objects (the program and clibc.so) are shared by all of them — a second copy of one cannot be mapped. Inside a namespace, a lookup sees that namespace's objects and the shared ones, and nothing else. |
| **`shm_open` objects are tmpfs files** | The POSIX shared-memory name space is a tmpfs mounted at `/dev/shm` (no new kernel object: a tmpfs file's contents already live in a memfd page array, so a `MAP_SHARED` mapping shares real frames and the shared futex works across the processes that map it). Borrowing tmpfs has two consequences: the `mode` argument is accepted and **ignored** (tmpfs nodes are `0777`; there is no per-file mode), and an object that holds contents takes one of the **128** slots in the kernel's generic inode page cache (the same table `/tmp` and the disk filesystems use), so at most 128 files system-wide can hold data simultaneously. A slot is reused once its object is gone, so a create/unlink loop does not drain the table. |
| **`mq_open` queues are files in `/dev/mqueue`** | A queue is a file in the small filesystem the kernel mounts at `/dev/mqueue`, so `mq_open()` is open-or-create and `mq_unlink()` is an `unlink()`; a queue unlinked while still open survives until the last close. A name is `/` plus one component of at most **127** characters, a queue holds at most **256** messages of at most **8192** bytes (**10**×**8192** by default when `attr` is NULL), priorities are **0…31** (`MQ_PRIO_MAX`, POSIX's minimum), and all queues' messages share a **4 MiB** kernel byte budget. The `mode` argument is accepted and **ignored**, and `mq_notify` honours `SIGEV_SIGNAL`/`SIGEV_NONE` only (`SIGEV_THREAD` is `EINVAL`). |
| **The loader relocates eagerly** | `RTLD_LAZY` and `RTLD_NOW` both resolve everything at load time — there is no lazy-binding thunk — and `dlvsym` accepts the version and ignores it, since nothing in the image defines any. |
| **`regex` backtracking cap** | Backtracking stops after 4M steps, so a pathological pattern reports `REG_NOMATCH` rather than running forever. |
| **`wordexp` diverges on purpose** | Arithmetic `$((…))` and the substring `${name:off:len}` follow POSIX, where glibc's own `wordexp` reports `WRDE_SYNTAX`; and `~user` stays literal, there being no passwd database. Deliberate, not an oversight. |

## ⚙️ ABI — `sysenter` (15 traps)

**Register convention (`sysenter`, 3-scalar traps):**

```
EAX = syscall number (SYS_*)
EBX = 1st argument
ESI = 2nd argument
EDI = 3rd argument
EAX ← return value (signed int semantics)
```

`sysenter`/`sysexit` steal **ECX** (return ESP) and **EDX** (return EIP), which is why the second and third arguments travel in **ESI**/**EDI** (`__syscallN` in [`syscall.h`](include/syscall.h)); the kernel's legacy `int 0x80` gate has been removed.

```c
static inline intptr_t __syscall3(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    intptr_t ret;
    __asm__ volatile (
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(a1), "S"(a2), "D"(a3)
        : "ecx", "edx", "memory"
    );
    return ret;
}
```

Many traps take a **pointer/struct** (e.g. `SYS_MMAP`); the kernel dispatches those through its full frame path — `_needs_frame()` in CactKernel **`mod.c`**.

**The 15 traps** (authoritative list in [`include/syscall.h`](include/syscall.h)):

| # | Constant | Typical use |
|---|----------|-------------|
| 0 | `SYS_OPEN` | open a VFS node / file |
| 1 | `SYS_CLOSE` | |
| 2 | `SYS_READ` | |
| 3 | `SYS_WRITE` | |
| 4 | `SYS_IOCTL` | every relay command — see [`ioctl_abi.h`](include/ioctl_abi.h) |
| 5 | `SYS_POLL` | |
| 6 | `SYS_FORK` | |
| 7 | `SYS_EXEC` | |
| 8 | `SYS_EXIT` | |
| 9 | `SYS_WAITPID` | |
| 10 | `SYS_BRK` | heap |
| 11 | `SYS_MMAP` | uses the full register frame in the kernel |
| 12 | `SYS_MUNMAP` | |
| 13 | `SYS_MPROTECT` | |
| 14 | `SYS_SIGRETURN` | signal-return trampoline |

---

## ⚖️ License

**GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

---

<p align="center">
  <strong>Developer:</strong> <a href="https://github.com/QwaYer">QwaYer</a>
  &nbsp;·&nbsp; <strong>Kernel:</strong> <a href="https://github.com/QwaYer/CactKernel-x86_32">CactKernel-x86_32</a>
  &nbsp;·&nbsp; <strong>OS:</strong> <a href="https://github.com/QwaYer/CactOS-x86_32">CactOS-x86_32</a>
</p>
