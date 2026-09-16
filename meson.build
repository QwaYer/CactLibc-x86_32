# ==============================================================================
# CactLibc — Meson build system
# ==============================================================================
#
#   meson setup build-meson --cross-file cross/i686-cact-clang.ini
#   ninja -C build-meson
#
# Published artifacts, consumed by the sibling repos:
#
#   build-meson/src/clibc.so    shared libc (PIC), linked into every userspace PIE
#   build-meson/ld.so           PT_INTERP dynamic linker (/lib/ld.so)
#   build-meson/start.o         PIC _start object, linked into every userspace PIE
#   build-meson/src/libc.a      static non-PIC archive (built on demand)
#
# Unlike the kernel, userspace code may use SSE: the kernel sets
# CR4.OSFXSR|OSXMMEXCPT during FPU init (idt/fpu/fpu_hd.c) before ring 3, so the
# C flags here deliberately do not carry -mno-sse.
# ==============================================================================

project('cactlibc', 'c',
  meson_version: '>=1.0.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=2',   # -Wall -Wextra, as in the Makefile
    'buildtype=plain',
  ])

fs = import('fs')
cc = meson.get_compiler('c')
ld = find_program('ld')

src_root = meson.project_source_root()
cactlib_version = fs.exists('VERSION') ? fs.read('VERSION').strip() : 'unknown'

# Non-PIC objects (libc.a, ld.so) and PIC objects (clibc.so).  The PIC variant
# takes -fPIC from the target's `pic: true`; the version macro lives in
# include/version.h, so no -D is needed here.
libc_c_args = ['-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib']
pic_c_args = ['-ffreestanding', '-fno-stack-protector', '-nostdlib']

libc_includes = include_directories('include')

subdir('src')
subdir('ldso')

# ------------------------------------------------------------------------------
# Published artifacts
# ------------------------------------------------------------------------------

# _start for every userspace PIE.  The old Makefile exposed this as
# build/pic/start.o and it is not part of clibc.so, so it is compiled on its own.
custom_target('start-pic',
  input: 'src/start.S',
  output: 'start.o',
  command: cc.cmd_array() + ['-m32'] + pic_c_args + ['-fPIC', '-c', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true)

custom_target('clibc-so',
  input: cactlib_pic,
  output: 'clibc.so',
  command: [ld, '-m', 'elf_i386', '-shared', '-nostdlib',
            '--hash-style=both', '-soname=clibc.so',
            '-T', src_root / 'libc.ld', '-o', '@OUTPUT@',
            '--whole-archive', cactlib_pic, '--no-whole-archive'],
  depend_files: files('libc.ld'),
  build_by_default: true)

custom_target('ld-so',
  input: ldso_lib,
  output: 'ld.so',
  command: [ld, '-m', 'elf_i386', '-nostdlib',
            '-T', src_root / 'ldso' / 'ldso.ld', '-o', '@OUTPUT@',
            '--whole-archive', ldso_lib, '--no-whole-archive'],
  depend_files: files('ldso/ldso.ld'),
  build_by_default: true)

message('')
message('  CactLibc ' + cactlib_version)
message('  compiler: ' + cc.get_id() + ' ' + cc.version())
message('')
message('  ninja -C build-meson          -> src/clibc.so, ld.so, start.o')
message('  ninja -C build-meson src/libc.a')
message('')
