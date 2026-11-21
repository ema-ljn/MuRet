// The real x87 instructions of the CPU this runs on (GCC/Clang, x86-64 only). FSIN, FCOS, FPTAN and
// FPATAN return a 64-bit significand whatever the precision control says.
#pragma once
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define PROBE_HAS_X87 1
inline long double X87Sin(long double x)
{
    long double r;
    __asm__ __volatile__("fsin" : "=t"(r) : "0"(x));
    return r;
}
inline long double X87Cos(long double x)
{
    long double r;
    __asm__ __volatile__("fcos" : "=t"(r) : "0"(x));
    return r;
}
inline long double X87Tan(long double x)
{
    long double r;
    __asm__ __volatile__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x));
    return r;
}
// acos(x) as atan2(sqrt((1 - x)(1 + x)), x), the usual x87 recipe; an approximation of what the CRT did.
inline long double X87Acos(long double x)
{
    long double y = __builtin_sqrtl((1.0L - x) * (1.0L + x));
    long double r;
    __asm__ __volatile__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}
#else
#define PROBE_HAS_X87 0
#endif
