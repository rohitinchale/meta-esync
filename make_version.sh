#!/bin/sh

set -e
out="$(mktemp)"

cat >"$out" << _EOF_

#ifndef BUILD_VERSION
#define BUILD_VERSION "$(cd "$1" && git describe --dirty --exact-match --all --long)"
#endif

_EOF_

diff -q "$out" tcu_ua_version.h || {
    cp -f "$out" tcu_ua_version.h || true
}

rm -f "$out"

