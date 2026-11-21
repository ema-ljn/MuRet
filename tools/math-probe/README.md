# Math probe (temporary)

Probe of the deterministic-math analysis of 2026-11-21 (sin, cos, tan, acos, log10 and their float
versions). It is not part of the client and this branch is not meant to be merged: it only exists so
that GitHub runs the same probe with MSVC and with GCC/Clang (`.github/workflows/math-probe.yml`).

- `src/probe.cpp`: the commands (`hash`, `check`, `table`, `dump`, `diff`, `time`, `exhaustive`, `x87`, ...).
- `src/domains.h`: the input sets, generated without anything implementation-defined.
- `src/impl_*.cpp`: one adaptor per library (system libm, LLVM-libc, CORE-MATH, openlibm, MPFR, SLEEF).
- `expected/cr-2000000.txt`: the hashes every correctly rounded implementation must give.
- `data/x87-intel-xeon.bin.gz`: what FSIN and FCOS return on an Intel Xeon (family 6, model 207), to compare CPUs.
