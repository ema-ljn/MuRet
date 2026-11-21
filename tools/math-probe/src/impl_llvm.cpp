// Compiled several times with different flags; LIBC_NAMESPACE and PROBE_ENTRY keep the copies apart.
#include "impls.h"
#include "shared/math/acos.h"
#include "shared/math/acosf.h"
#include "shared/math/cos.h"
#include "shared/math/cosf.h"
#include "shared/math/log10.h"
#include "shared/math/sin.h"
#include "shared/math/sinf.h"
#include "shared/math/tan.h"
#include "shared/math/tanf.h"
namespace
{
namespace L = LIBC_NAMESPACE::shared;
double Sin(double x) { return L::sin(x); }
double Cos(double x) { return L::cos(x); }
double Tan(double x) { return L::tan(x); }
double Acos(double x) { return L::acos(x); }
double Log10(double x) { return L::log10(x); }
float SinF(float x) { return L::sinf(x); }
float CosF(float x) { return L::cosf(x); }
float TanF(float x) { return L::tanf(x); }
float AcosF(float x) { return L::acosf(x); }
#define PROBE_STRING2(x) #x
#define PROBE_STRING(x) PROBE_STRING2(x)
const Impl Table{PROBE_STRING(PROBE_NAME), Sin, Cos, Tan, Acos, Log10, SinF, CosF, TanF, AcosF};
} // namespace
const Impl* PROBE_ENTRY() { return &Table; }
