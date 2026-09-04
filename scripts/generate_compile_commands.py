#!/usr/bin/env python3
"""
Generate compile_commands.json for clangd from build_odin.sh's flags.

Reads main.cpp's #include order and produces one entry per src/*.cpp file.
Each entry's "arguments" array force-includes the right slice of the unity
chain so clangd can parse that file standalone.

Run via scripts/generate_compile_commands.sh (which discovers LLVM and
exports CXX / LLVM_CXXFLAGS / GIT_SHA / GIT_DATE into the environment).
"""

import json
import os
import pathlib
import re
import sys


def main() -> int:
    repo_root  = pathlib.Path(sys.argv[1]).resolve()
    src_dir    = repo_root / 'src'
    stubs_path = repo_root / 'scripts' / 'clangd_stubs.cpp'
    output     = repo_root / 'compile_commands.json'

    cxx      = os.environ['CXX']
    cxxflags = os.environ['LLVM_CXXFLAGS']
    git_sha  = os.environ['GIT_SHA']
    git_date = os.environ['GIT_DATE']

    # 1. Parse main.cpp's TOP-LEVEL #include order. We deliberately do NOT
    #    expand transitive deps — the top-level files will pull in their own
    #    deps when clangd processes them, and forcing them twice causes
    #    "redefinition" / duplicate-macro errors.
    #
    #    Use the include path AS WRITTEN in main.cpp (e.g. "llvm-c/Types.h")
    #    not just the basename — basenames break for files like Types.h that
    #    live in subdirectories.
    main_cpp = (src_dir / 'main.cpp').read_text()
    include_re = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
    unity_chain = []  # paths relative to src/, in main.cpp's order, deduplicated
    seen = set()
    for m in include_re.finditer(main_cpp):
        rel = m.group(1)
        if rel.startswith('src/'):
            rel = rel[4:]
        base = pathlib.Path(rel).name
        if base not in seen:
            seen.add(base)
            unity_chain.append(rel)

    # 2. For each src/*.cpp file, compute its prelude = main.cpp's includes
    #    before it (or the full chain if F isn't directly included by main.cpp).
    all_src_cpps = sorted(p.name for p in src_dir.glob('*.cpp'))
    chain_basenames = [pathlib.Path(p).name for p in unity_chain]
    index_in_chain = {name: i for i, name in enumerate(chain_basenames)}

    # Common flags (kept in sync with build_odin.sh, minus link-time flags)
    common_flags = [
        '-Wno-switch', '-Wno-macro-redefined', '-Wno-unused-value',
        '-std=c++17',
        '-D_GNU_SOURCE',
        '-D_GLIBCXX_USE_CXX11_ABI=1',
        '-D__STDC_CONSTANT_MACROS',
        '-D__STDC_FORMAT_MACROS',
        '-D__STDC_LIMIT_MACROS',
        f'-DGIT_SHA="{git_sha}"',
        f'-DODIN_VERSION_RAW="dev-{git_date}"',
        '-fno-exceptions',
        '-Isrc',
    ] + cxxflags.split()

    entries = []
    for src in all_src_cpps:
        if src in index_in_chain:
            prelude = unity_chain[:index_in_chain[src]]
        else:
            # Not in main.cpp's chain — give it the full chain so it has
            # complete context (rare; e.g., helper .cpp not pulled by main.cpp).
            prelude = list(unity_chain)

        args = [cxx, 'src/main.cpp', 'src/libtommath.cpp']
        args += common_flags
        # clangd_stubs.cpp mirrors the "loose" code at the top of main.cpp —
        # globals/functions defined directly there (not via #include). Must
        # come before the unity chain so types like Timings are visible.
        args += ['-include', 'scripts/clangd_stubs.cpp']
        for f in prelude:
            args += ['-include', f]
        args += ['-c', '-o', '/dev/null']

        entries.append({
            'directory': str(repo_root),
            'file': f'src/{src}',
            'arguments': args,
        })

    with open(output, 'w') as f:
        json.dump(entries, f, indent=2)

    print(f'Wrote {output.relative_to(repo_root)} ({len(entries)} entries)')
    print(f'Unity chain ({len(unity_chain)} files): {", ".join(unity_chain)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
