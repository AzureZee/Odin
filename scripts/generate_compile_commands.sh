#!/usr/bin/env sh
# generate_compile_commands.sh
#
# Synthesizes compile_commands.json for clangd from build_odin.sh's flags.
#
# Odin is built as a unity translation unit: build_odin.sh compiles
# src/main.cpp (which #include's every other src/*.cpp in dependency
# order). clangd parses each file individually, so each compile_commands.json
# entry must include the right subset of the unity chain — specifically,
# everything main.cpp pulls in BEFORE the target file.
#
# Usage:
#   scripts/generate_compile_commands.sh            # writes compile_commands.json
#   LLVM_CONFIG=llvm-config-22 scripts/generate_compile_commands.sh
#
# Re-run whenever build_odin.sh's flags or main.cpp's #include order change.

set -eu

# Resolve script dir and repo root regardless of cwd / invocation style
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# --- Discover LLVM (same lookup logic as build_odin.sh) ---
SUPPORTED_LLVM_VERSIONS="22 21 20 19 18 17"
LLVM_CONFIG="${LLVM_CONFIG:-}"

if [ -z "$LLVM_CONFIG" ] && [ -n "$(command -v llvm-config)" ]; then
	LLVM_CONFIG="llvm-config"
fi

if [ -z "$LLVM_CONFIG" ]; then
	for V in $SUPPORTED_LLVM_VERSIONS; do
		for cmd in "llvm-config-$V" "llvm-config$V"; do
			if [ -n "$(command -v "$cmd")" ]; then
				LLVM_CONFIG="$cmd"
				break 2
			fi
		done
	done
fi

if [ -z "$LLVM_CONFIG" ]; then
	echo "ERROR: no llvm-config found on PATH. Set LLVM_CONFIG to proceed." >&2
	exit 1
fi

LLVM_CXXFLAGS="$($LLVM_CONFIG --cxxflags)"

# --- Compiler (same fallback chain as build_odin.sh) ---
if [ -n "$(command -v clang++)" ]; then
	CXX="clang++"
elif [ -x "$($LLVM_CONFIG --bindir)/clang++" ]; then
	CXX="$($LLVM_CONFIG --bindir)/clang++"
else
	echo "ERROR: no clang++ found. Set CXX to proceed." >&2
	exit 1
fi

# --- Mirrors build_odin.sh CPPFLAGS (line 21-29) ---
if [ -d .git ] && [ -n "$(command -v git)" ]; then
	gitnosig="-c log.showSignature=false"
	GIT_SHA="$(git $gitnosig show --pretty='%h' --no-patch --no-notes HEAD)"
	GIT_DATE="$(git $gitnosig show --pretty='%cd' --date=format:%Y-%m --no-patch --no-notes HEAD)"
else
	GIT_SHA="0000000"
	GIT_DATE="$(date +%Y-%m)"
fi

export CXX LLVM_CXXFLAGS GIT_SHA GIT_DATE

# --- Hand off to Python: parse main.cpp's include order, build per-file entries ---
exec python3 "$SCRIPT_DIR/generate_compile_commands.py" "$REPO_ROOT"
