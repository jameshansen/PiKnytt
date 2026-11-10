#!/bin/sh
# Builds Scale.sft from tools/scale.c with zig (https://ziglang.org), on any machine.
# The filter is 32-bit Windows code with no C runtime, so zig only lends its compiler
# and its Windows headers.
set -e
cd "$(dirname "$0")/.."
headers=$(dirname "$(readlink -f "$(command -v zig)")")/lib/libc/include/any-windows-any
zig cc -target x86-windows-gnu -shared -nostdlib -isystem "$headers" -Os -s -fno-builtin -fno-sanitize=undefined \
  -o Scale.sft tools/scale.c -lkernel32 -luser32 -lgdi32
rm -f scale.lib   # the import library the linker writes alongside
ls -l Scale.sft
