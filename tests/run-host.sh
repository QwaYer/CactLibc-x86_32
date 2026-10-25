#!/bin/sh
# Host-side unit tests for CactLibc.
#
# Each unit under test is the real src/*.c, compiled for the host with
# tests/host/rename_<unit>.h pre-included so its entry points (and its errno)
# do not collide with glibc's, then linked against tests/host/shim.c and a test
# main (tests/host/<main>.c) that compares against glibc.  Nothing here is
# cross-compiled: the point is to run the shipped C on a real libc.
#
#   tests/run-host.sh              # all units listed in tests/units.txt
#   HOSTCC=clang tests/run-host.sh
#
# A unit may list several sources (e.g. time.c + tz.c) when one depends on
# another; tokens starting with '-' are extra link flags.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/.." && pwd)
CC=${HOSTCC:-cc}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The units get the CactOS headers (their own); the shims and the test main get
# the system ones — mixing them would hand glibc's printf 32-bit ssize_t.
UNITFLAGS="-O1 -g -w -I$root/include"
HOSTFLAGS="-O1 -g -w"

rc=0

run_unit() {
    name=$1; test=$2; shift 2

    srcs=""; flags=""
    for tok in "$@"; do
        case "$tok" in
            -*) flags="$flags $tok" ;;
            *)  srcs="$srcs $tok" ;;
        esac
    done

    objs=""
    for src in $srcs; do
        "$CC" $UNITFLAGS -include "$here/host/rename_$name.h" \
            -c "$root/src/$src" -o "$tmp/${name}__${src}.o"
        objs="$objs $tmp/${name}__${src}.o"
    done

    "$CC" $HOSTFLAGS -c "$here/host/$test" -o "$tmp/${name}__$test.o"
    # shellcheck disable=SC2086
    "$CC" $objs "$tmp/shim.o" "$tmp/${name}__$test.o" $flags -o "$tmp/$name.test"
    if ! "$tmp/$name.test"; then rc=1; fi
}

# The platform shims (errno slot, nodeio stubs) are shared by every unit.
"$CC" $HOSTFLAGS -c "$here/host/shim.c" -o "$tmp/shim.o"

# ── fixtures for the dlfcn unit ─────────────────────────────────────────────
#
# A plugin is an i686 shared object the loader maps at a fixed address.  It is
# built here rather than committed (no binaries in the repositories) and only
# when that unit is asked for; the test main learns the paths from the
# environment.  The same source is linked three times: normally, with an
# unresolvable symbol, and deliberately on top of clibc.so so the loader's
# collision check has something to refuse.  Two more carry a __thread of their
# own — one static (initial-exec), one general-dynamic, which must be refused.
build_dl_fixtures() {
    libc_so="$root/build-meson/clibc.so"
    if [ ! -f "$libc_so" ]; then
        echo "dlfcn fixtures: $libc_so is missing — build the libc first" >&2
        return 1
    fi
    xcc=${CROSSCC:-clang}
    ldflags="-m elf_i386 -shared --hash-style=both -nostdlib -T $here/host/plugin.ld"
    xcflags="-m32 -fPIE -ffreestanding -fno-stack-protector -nostdlib"
    # The two __thread fixtures are built -fPIC instead: only then does the
    # linker keep the TLS model the source asked for (initial-exec's GOT slot,
    # and general-dynamic's module-id pair), which is what is under test.  Built
    # -fPIE it relaxes both to local-exec, and the general-dynamic one would load
    # as if it were static.
    xcflags_tls="-m32 -fPIC -ffreestanding -fno-stack-protector -nostdlib"

    "$xcc" $xcflags -c "$here/host/plugin.c" -o "$tmp/plugin.o" || return 1
    "$xcc" $xcflags -c "$here/host/plugin_undef.c" -o "$tmp/plugin_undef.o" || return 1
    "$xcc" $xcflags -c "$here/host/plugin_iso.c" -o "$tmp/plugin_iso.o" || return 1
    "$xcc" $xcflags_tls -c "$here/host/plugin_tls.c" -o "$tmp/plugin_tls.o" || return 1
    "$xcc" $xcflags_tls -c "$here/host/plugin_gd_tls.c" -o "$tmp/plugin_gd_tls.o" || return 1

    ld $ldflags --defsym=PLUGIN_BASE=0x0C000000 -o "$tmp/plugin.so" \
       "$tmp/plugin.o" "$libc_so" || return 1
    ld $ldflags --defsym=PLUGIN_BASE=0x0C100000 -o "$tmp/plugin_undef.so" \
       "$tmp/plugin_undef.o" "$libc_so" || return 1
    ld $ldflags --defsym=PLUGIN_BASE=0x0C400000 -o "$tmp/plugin_iso.so" \
       "$tmp/plugin_iso.o" "$libc_so" || return 1
    ld $ldflags --defsym=PLUGIN_BASE=0x10000000 -o "$tmp/plugin_collide.so" \
       "$tmp/plugin.o" "$libc_so" || return 1
    ld $ldflags --defsym=PLUGIN_BASE=0x0C200000 -o "$tmp/plugin_tls.so" \
       "$tmp/plugin_tls.o" "$libc_so" || return 1
    ld $ldflags --defsym=PLUGIN_BASE=0x0C300000 -o "$tmp/plugin_gd_tls.so" \
       "$tmp/plugin_gd_tls.o" "$libc_so" || return 1

    CACT_TEST_LIBC_SO="$libc_so"
    CACT_TEST_PLUGIN_SO="$tmp/plugin.so"
    CACT_TEST_PLUGIN_UNDEF_SO="$tmp/plugin_undef.so"
    CACT_TEST_PLUGIN_ISO_SO="$tmp/plugin_iso.so"
    CACT_TEST_PLUGIN_COLLIDE_SO="$tmp/plugin_collide.so"
    CACT_TEST_PLUGIN_TLS_SO="$tmp/plugin_tls.so"
    CACT_TEST_PLUGIN_GD_SO="$tmp/plugin_gd_tls.so"
    export CACT_TEST_LIBC_SO CACT_TEST_PLUGIN_SO \
           CACT_TEST_PLUGIN_UNDEF_SO CACT_TEST_PLUGIN_ISO_SO \
           CACT_TEST_PLUGIN_COLLIDE_SO \
           CACT_TEST_PLUGIN_TLS_SO CACT_TEST_PLUGIN_GD_SO
    return 0
}

if grep -q '^dlfcn' "$here/units.txt"; then
    build_dl_fixtures || rc=1
fi

while read -r name test rest; do
    [ -z "${name:-}" ] && continue
    case "$name" in \#*) continue ;; esac
    # shellcheck disable=SC2086
    run_unit "$name" "$test" ${rest:-}
done < "$here/units.txt"

if [ "$rc" -eq 0 ]; then
    echo "host tests: OK"
fi
exit $rc
