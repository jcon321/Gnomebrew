#!/usr/bin/env bash
# Compile and link the Windows build on Linux with llvm-mingw, to catch errors
# before a Visual Studio build. Output: build-win/eqclient_gui.exe, build-win/eqclient.exe
#   LLVM_MINGW_ROOT=/path/to/llvm-mingw tools/cross-check-windows.sh
set -euo pipefail
cd "$(dirname "$0")/.."

root="${LLVM_MINGW_ROOT:-}"
if [[ -z "$root" ]]; then
	root="$(ls -d ../toolchains/llvm-mingw-*-ucrt-* 2>/dev/null | sort | tail -n1 || true)"
fi
if [[ -z "$root" || ! -x "$root/bin/x86_64-w64-mingw32-clang++" ]]; then
	echo "llvm-mingw not found; set LLVM_MINGW_ROOT" >&2
	exit 1
fi
root="$(cd "$root" && pwd)"

cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DLLVM_MINGW_ROOT="$root" >/dev/null
cmake --build build-win
