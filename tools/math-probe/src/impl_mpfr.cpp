// The oracle: MPFR rounds the exact value once to the target precision (53 or 24 bits, nearest even).
#include "impls.h"
#include <mpfr.h>
namespace
{
template <int (*F)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t)> double D(double x)
{
    thread_local mpfr_t in, out;
    thread_local bool ready = false;
    if (!ready)
    {
        mpfr_init2(in, 53);
        mpfr_init2(out, 53);
        ready = true;
    }
    mpfr_set_d(in, x, MPFR_RNDN);
    F(out, in, MPFR_RNDN);
    return mpfr_get_d(out, MPFR_RNDN);
}
template <int (*F)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t)> float S(float x)
{
    thread_local mpfr_t in, out;
    thread_local bool ready = false;
    if (!ready)
    {
        mpfr_init2(in, 24);
        mpfr_init2(out, 24);
        ready = true;
    }
    mpfr_set_flt(in, x, MPFR_RNDN);
    F(out, in, MPFR_RNDN);
    return mpfr_get_flt(out, MPFR_RNDN);
}
const Impl Table{"mpfr",          D<mpfr_sin>, D<mpfr_cos>, D<mpfr_tan>, D<mpfr_acos>, D<mpfr_log10>,
                 S<mpfr_sin>, S<mpfr_cos>, S<mpfr_tan>, S<mpfr_acos>};
} // namespace
const Impl* ImplMpfr() { return &Table; }

// sin or cos of a double rounded once to 24 bits: what FSIN followed by FSTP DWORD aims at.
float MpfrSinToFloat(double x)
{
    thread_local mpfr_t in, out;
    thread_local bool ready = false;
    if (!ready)
    {
        mpfr_init2(in, 53);
        mpfr_init2(out, 24);
        ready = true;
    }
    mpfr_set_d(in, x, MPFR_RNDN);
    mpfr_sin(out, in, MPFR_RNDN);
    return mpfr_get_flt(out, MPFR_RNDN);
}
// sin of a double rounded once to 64 bits, as a long double: the "retained" value of ADR 0014.
long double MpfrSinTo64(double x)
{
    thread_local mpfr_t in, out;
    thread_local bool ready = false;
    if (!ready)
    {
        mpfr_init2(in, 53);
        mpfr_init2(out, 64);
        ready = true;
    }
    mpfr_set_d(in, x, MPFR_RNDN);
    mpfr_sin(out, in, MPFR_RNDN);
    return mpfr_get_ld(out, MPFR_RNDN);
}
long double MpfrCosTo64(double x)
{
    thread_local mpfr_t in, out;
    thread_local bool ready = false;
    if (!ready)
    {
        mpfr_init2(in, 53);
        mpfr_init2(out, 64);
        ready = true;
    }
    mpfr_set_d(in, x, MPFR_RNDN);
    mpfr_cos(out, in, MPFR_RNDN);
    return mpfr_get_ld(out, MPFR_RNDN);
}
