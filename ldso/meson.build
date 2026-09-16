# /lib/ld.so — the PT_INTERP dynamic linker.  Self-contained, no libc.  The
# Makefile compiled ldso.c with -O2 and start.S with the plain CFLAGS; -O2 is
# applied to both here, which is a no-op for the assembly file.
ldso_lib = static_library('ldso',
  files(
    'start.S',
    'ldso.c',
  ),
  include_directories: libc_includes,
  c_args: libc_c_args + ['-O2'])
