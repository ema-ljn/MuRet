#include "impls.h"
#define CM2(a, b) a##b
#define CM1(a, b) CM2(a, b)
#define CM(name) CM1(name, PROBE_SUFFIX)
extern "C"
{
double CM(cr_sin)(double);
double CM(cr_cos)(double);
double CM(cr_tan)(double);
double CM(cr_acos)(double);
double CM(cr_log10)(double);
float CM(cr_sinf)(float);
float CM(cr_cosf)(float);
float CM(cr_tanf)(float);
float CM(cr_acosf)(float);
}
namespace
{
#define PROBE_STRING2(x) #x
#define PROBE_STRING(x) PROBE_STRING2(x)
const Impl Table{PROBE_STRING(PROBE_NAME), CM(cr_sin), CM(cr_cos), CM(cr_tan), CM(cr_acos), CM(cr_log10), CM(cr_sinf), CM(cr_cosf), CM(cr_tanf), CM(cr_acosf)};
}
const Impl* PROBE_ENTRY() { return &Table; }
