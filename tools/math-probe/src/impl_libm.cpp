#include "impls.h"
#include <cmath>
namespace
{
double Sin(double x) { return std::sin(x); }
double Cos(double x) { return std::cos(x); }
double Tan(double x) { return std::tan(x); }
double Acos(double x) { return std::acos(x); }
double Log10(double x) { return std::log10(x); }
float SinF(float x) { return std::sin(x); }
float CosF(float x) { return std::cos(x); }
float TanF(float x) { return std::tan(x); }
float AcosF(float x) { return std::acos(x); }
const Impl Table{"libm", Sin, Cos, Tan, Acos, Log10, SinF, CosF, TanF, AcosF};
} // namespace
const Impl* ImplLibm() { return &Table; }
