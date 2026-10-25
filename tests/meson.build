# Host-side unit tests.
#
# The shipped src/*.c is compiled for the *host* and linked against the host
# libc, then compared with glibc; this is the automatic counterpart to the
# ad-hoc checks the libc used to get during development.
#
#   meson test -C build-meson
#   meson test -C build-meson --print-errorlogs
#
# Cross-compiling for i686 does not disable it: the tests use the build-machine
# compiler (plain `cc`), never the cross one.
test('libc-host',
  find_program('run-host.sh'),
  timeout: 300,
  protocol: 'exitcode')
