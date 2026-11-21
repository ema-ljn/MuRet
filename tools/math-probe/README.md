# Math probe (temporary)

Probe of the deterministic-math analysis of 2026-11-21 (sin, cos, tan, acos, log10 and their float
versions). It is not part of the client and this branch is not meant to be merged: it only exists so
that GitHub runs the same probe with MSVC and with GCC/Clang (`.github/workflows/math-probe.yml`).

- `src/probe.cpp`: the commands (`hash`, `check`, `table`, `dump`, `diff`, `time`, `exhaustive`, `x87`, ...).
- `src/domains.h`: the input sets, generated without anything implementation-defined.
- `src/impl_*.cpp`: one adaptor per library (system libm, LLVM-libc, CORE-MATH, openlibm, MPFR, SLEEF).
- `expected/cr-2000000.txt`: the hashes every correctly rounded implementation must give.
- `data/x87-intel-xeon.bin.gz`: what FSIN and FCOS return on an Intel Xeon (family 6, model 207), to compare CPUs.
- `pose/pose.cpp`: the client's `HalfLifeMath.cpp` and `OriginalRotation.cpp` on fixed inputs, one hash per output set.
- `pose/cr_redirect.h`, `pose/cr_impl.cpp`: a forced include that sends `sin`, `cos`, `tan` and `acos` (the ones
  glm calls too) to LLVM-libc. A lab trick to leave the client code untouched, not a design.

## Running it

Linux:

```sh
./fetch_sources.sh all
LLVM_LIBC=deps/llvm-23/libc LLVM_LIBC_MAIN=deps/llvm-main/libc OPENLIBM=deps/openlibm COREMATH=deps/core-math-2024 ./build.sh
./build/probe check 2000000 llvm expected/cr-2000000.txt
LLVM_LIBC=deps/llvm-23/libc GLM=deps/glm ./build_pose.sh
./build/pose-system && ./build/pose-cr
```

Windows, from a Visual Studio developer prompt:

```bat
build_msvc.cmd release deps\llvm-23\libc "/O2 /MD" ""
build_pose_msvc.cmd deps\llvm-23\libc deps\glm
```

`build_msvc.cmd <name> <llvm libc dir> "<flags>" "<extra flags, LLVM-libc file only>" [compiler] [std switch]`:
the extra flags reach only the file that includes LLVM-libc, so the input generator is the same in every variant.

## Pose hashes

| Output | glibc 2.39 | UCRT | Correctly rounded |
|---|---|---|---|
| quaternions | `e64a4b63deb3b313` | `e207c86968da4d85` | `7134dff05e511aad` |
| matrices | `7d9818bae68470f5` | `7d9818bae68470f5` | `7d9818bae68470f5` |
| round-matrices | `f91ae0a6125869bd` | `f91ae0a6125869bd` | `f91ae0a6125869bd` |
| slerps | `006acabf22c05588` | `02ba84ce6d2dff10` | `c9d6a374a4fddbef` |
| chains | `849d6eaee3f3cab8` | `58ad422616cd252d` | `0de99a6d7b82ee7f` |

The correctly rounded column is the same with GCC 13 (`-O2`, `-O0`), Clang 18, MSVC 19.44 and 19.52 (Release,
Debug, `/arch:AVX2`) and MSVC 19.52 with `/Zc:cmath`.

## Commit message switches

`[quick]` skips the long steps (differences, timings, every float). `[pose-only]` also skips the Windows variants.
