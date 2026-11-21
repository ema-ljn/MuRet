# LLVM-libc math headers

A copy of the header-only math of [LLVM-libc](https://libc.llvm.org/), from llvm-project at the tag
`llvmorg-23.1.6`, unmodified. License: Apache-2.0 with LLVM exceptions (`LICENSE.TXT`).

The client uses it for `sin`, `cos`, `tan`, `acos` and `log10`, because its functions are correctly rounded:
each returns the float or double nearest to the exact value, so the result does not depend on the compiler,
the system, the C library or the CPU. Only `Client/src/Render/Models/RoundedMath/RoundedMath.cpp` includes
these headers; the rest of the client calls `Render/Models/RoundedMath.h`.

One case is not correctly rounded: without FMA instructions, the sine and the tangent of a double below 2^-968 can
be one unit in the last place off (`src/__support/math/sin.h` and `tan.h`, where `x * 2^-54` underflows; still so in
llvm-project `main` on 2026-11-21). `RoundedMath.cpp` returns `x` there, which is the nearest double, and
`RoundedMathTest` compares every function with MPFR.

It is a copy and not a vcpkg port because the `llvm[libc]` port builds all of LLVM, and the client needs
208 headers.

## Updating

```sh
git clone --depth 1 --branch <tag> --filter=blob:none --sparse https://github.com/llvm/llvm-project.git
git -C llvm-project sparse-checkout set libc
python3 -I third_party/llvm-libc/vendor.py llvm-project
```

Change `PINNED_TAG` in `vendor.py` and the tag above. `RoundedMathTest` has to pass unchanged on every
platform: a correctly rounded result does not depend on the version.
