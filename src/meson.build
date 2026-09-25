# Every libc translation unit.  start.S is compiled twice on purpose: non-PIC
# into libc.a here, and PIC by the root target that publishes start.o, exactly
# as the Makefile did with build/ and build/pic/.
libc_c_sources = files(
  'crypto.c',
  'ctype.c',
  'dirent.c',
  'dlfcn.c',
  'dns.c',
  'env.c',
  'epoll.c',
  'errno_str.c',
  'errno_var.c',
  'eventfd.c',
  'fcntl.c',
  'flock.c',
  'getopt.c',
  'libgen.c',
  'math_stubs.c',
  'mman.c',
  'nodeio.c',
  'pthread.c',
  'random.c',
  'scanf.c',
  'shm.c',
  'signal.c',
  'signalfd.c',
  'socket.c',
  'stat.c',
  'tls.c',
  'tls_roots.c',
  'stdio.c',
  'stdlib.c',
  'string.c',
  'syscall.c',
  'termios.c',
  'time.c',
  'timerfd.c',
  'udivdi3.c',
  'unistd.c',
  'wait.c',
)

libc_asm_sources = files('setjmp.S')

# Named 'c' so the archive keeps its historical name: libc.a.  Not part of the
# default build, just like the Makefile's TARGET_A.
static_library('c',
  libc_c_sources + libc_asm_sources + files('start.S'),
  include_directories: libc_includes,
  c_args: libc_c_args,
  build_by_default: false)

# PIC objects for clibc.so, without start.o (see the Makefile's ALL_PIC_OBJS).
cactlib_pic = static_library('cactlib_pic',
  libc_c_sources + libc_asm_sources,
  include_directories: libc_includes,
  c_args: pic_c_args,
  pic: true)
