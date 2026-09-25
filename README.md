# 🌵 CactLib/x86_32

<p align="center">
  <img src="https://img.shields.io/badge/version-1.0.0-green.svg?style=for-the-badge" alt="Version: 1.0.0">
  <img src="https://img.shields.io/badge/license-GPLv3-blue.svg?style=for-the-badge" alt="License: GPLv3">
  <img src="https://img.shields.io/badge/arch-i686-red.svg?style=for-the-badge" alt="Arch: i686">
  <img src="https://img.shields.io/badge/language-C%2FASM-orange.svg?style=for-the-badge" alt="Language: C/ASM">
  <img src="https://img.shields.io/badge/output-clibc.so-green.svg?style=for-the-badge" alt="clibc.so">
  <img src="https://img.shields.io/badge/syscall-sysenter%20%2B%20int%200x80-purple.svg?style=for-the-badge" alt="sysenter + int 0x80">
  <img src="https://img.shields.io/badge/status-1.0.0-yellow.svg?style=for-the-badge" alt="1.0.0">
</p>

<p align="center">
  A <strong>freestanding C library</strong> for <a href="https://github.com/QwaYer/CactOS-x86_32"><strong>CactOS</strong></a> user space on <strong>i686</strong>.<br>
  No host libc — every OS entry goes through <code>sysenter</code> (with an <code>int 0x80</code> fallback) using the numbers from <a href="include/syscall.h"><code>include/syscall.h</code></a>, which must match <a href="https://github.com/QwaYer/CactKernel-x86_32"><strong>CactKernel-x86_32</strong></a> <code>syscalls.h</code>; everything beyond the <strong>15</strong> traps is a VFS-node ioctl from <a href="include/ioctl_abi.h"><code>ioctl_abi.h</code></a>.
</p>

---

## 📦 Overview

| | |
|---|---|
| **Sources** | 37 × `src/*.c` + `src/start.S`, `src/setjmp.S` |
| **Public headers** | 60 files under `include/` (including `sys/*.h`) |
| **Syscall traps** | **15** (`SYS_SYSCALL_COUNT` mirrors the kernel enum); every other operation is a VFS-node ioctl |
| **Runtime deps** | None (build-time: `clang -m32`, GNU `binutils`, `meson`, `ninja`) |
| **Artifacts** | **`clibc.so`** (shared ET_DYN for PIE), **`build-meson/start.o`** (PIC `_start`) |

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
│   ├── stdio.c      printf family (integers, %f/%e/%g, width/precision/#), puts, putchar, kprint
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
│   ├── shm.c        SysV shared memory wrappers
│   ├── termios.c    tcgetattr / tcsetattr
│   ├── time.c       clocks + nanosleep
│   ├── syscall.c    variadic syscall() helper (sysenter)
│   ├── start.S      user _start (non-PIC + PIC flavours)
│   └── …            env, epoll, eventfd, getopt, pthread, scanf, wait, …
├── include/
│   ├── syscall.h    authoritative SYS_* list — 15 traps, keep identical to the kernel!
│   ├── ioctl_abi.h  every relay command/struct (FDCTL_*/SOCKCTL_*/NETCTL_*/CRYPTCTL_*)
│   ├── nodeio.h     nio_open/read/write/ioctl/dev_cmd helpers
│   ├── socket.h     sockaddr_in helpers + dns_resolve + getsockname/getpeername
│   ├── crypto.h     cact_sha256/…, cact_aes*gcm_*, cact_x25519_*, cact_sig_verify, cact_x509_verify
│   ├── tls.h        cact_tls_connect/read/write/close/set_roots
│   ├── stdio.h string.h stdlib.h unistd.h …
│   └── sys/*.h
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

> ⚠️ **`printf`** handles the integer, `%c`, `%s` and `%p` conversions plus the floating-point **`%f`/`%e`/`%g`** families (either case), the **`0`/`-`/`#`** flags and a width/precision (`.N` or `.*`); `%l`/`%ll` length modifiers are accepted. No locale, no `%n`, no positional arguments.

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

`signal`, `kill`, `sigaction`, `sigprocmask`, `sigpending`, `sigsuspend`, `alarm`, `setitimer`.

### Other headers

| Header | Highlights |
|--------|------------|
| **`sys/mman.h`** | `mmap`, `munmap`, `mprotect` |
| **`shm.h`** | `shmget`, `shmat`, `shmdt`, `shmctl` |
| **`time.h`** | `gettimeofday`, `clock_gettime` (**`CLOCK_REALTIME`** from the RTC via `/proc/wallclock`; **`CLOCK_MONOTONIC`** from `/proc/time`), `nanosleep` |
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

---

## ⚙️ ABI — `sysenter` (15 traps)

**Register convention (`sysenter`, 3-scalar traps):**

```
EAX = syscall number (SYS_*)
EBX = 1st argument
ESI = 2nd argument
EDI = 3rd argument
EAX ← return value (signed int semantics)
```

`sysenter`/`sysexit` steal **ECX** (return ESP) and **EDX** (return EIP), which is why the second and third arguments travel in **ESI**/**EDI**; an `int 0x80` gate is kept as a fallback (`__syscallN` in [`syscall.h`](include/syscall.h)).

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
