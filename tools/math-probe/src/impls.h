// Common interface of the probe: one row per implementation under test.
#pragma once
#include <cstdint>

struct Impl
{
    const char* name;
    double (*sin)(double);
    double (*cos)(double);
    double (*tan)(double);
    double (*acos)(double);
    double (*log10)(double);
    float (*sinf)(float);
    float (*cosf)(float);
    float (*tanf)(float);
    float (*acosf)(float);
};

// Each impl_*.cpp defines one of these (or none when it is not built on this platform).
const Impl* ImplLibm();
const Impl* ImplLlvm();        // LLVM-libc shared headers, default flags of the build
const Impl* ImplLlvmMain();    // same, from the main branch instead of the release tag
const Impl* ImplLlvmNative();  // same, compiled with -march=native (FMA paths) and contraction on
const Impl* ImplLlvmO0();      // same, compiled at -O0
const Impl* ImplCoreMath();    // CORE-MATH C sources
const Impl* ImplCoreMathNative();
const Impl* ImplFdlibm();      // openlibm (FreeBSD msun, fdlibm family), strict flags
const Impl* ImplFdlibmFma();   // same sources with -march=native -ffp-contract=fast
const Impl* ImplMpfr();        // oracle: MPFR, correctly rounded
const Impl* ImplSleef();       // SLEEF scalar u10
const Impl* ImplSleefDet();    // SLEEF "cinz" pure C deterministic variants
