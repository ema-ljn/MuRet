// The correctly rounded functions behind cr_redirect.h: LLVM-libc's shared math headers.
#include "shared/math/acos.h"
#include "shared/math/acosf.h"
#include "shared/math/cos.h"
#include "shared/math/cosf.h"
#include "shared/math/sin.h"
#include "shared/math/sinf.h"
#include "shared/math/tan.h"
#include "shared/math/tanf.h"

namespace L = LIBC_NAMESPACE::shared;
namespace std
{
float cr_sin(float x) { return L::sinf(x); }
double cr_sin(double x) { return L::sin(x); }
float cr_cos(float x) { return L::cosf(x); }
double cr_cos(double x) { return L::cos(x); }
float cr_tan(float x) { return L::tanf(x); }
double cr_tan(double x) { return L::tan(x); }
float cr_acos(float x) { return L::acosf(x); }
double cr_acos(double x) { return L::acos(x); }
} // namespace std
