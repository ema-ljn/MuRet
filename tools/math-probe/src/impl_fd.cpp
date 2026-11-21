#include "impls.h"
#define FD2(a, b) a##b
#define FD1(a, b) FD2(a, b)
#define FD(name) FD1(PROBE_PREFIX, name)
extern "C"
{
double FD(sin)(double);
double FD(cos)(double);
double FD(tan)(double);
double FD(acos)(double);
double FD(log10)(double);
float FD(sinf)(float);
float FD(cosf)(float);
float FD(tanf)(float);
float FD(acosf)(float);
}
namespace
{
#define PROBE_STRING2(x) #x
#define PROBE_STRING(x) PROBE_STRING2(x)
const Impl Table{PROBE_STRING(PROBE_NAME), FD(sin), FD(cos), FD(tan), FD(acos), FD(log10), FD(sinf), FD(cosf), FD(tanf), FD(acosf)};
}
const Impl* PROBE_ENTRY() { return &Table; }
