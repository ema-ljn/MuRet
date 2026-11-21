#include "impls.h"
#include <sleef.h>
namespace
{
double Sin(double x) { return Sleef_sin_u10(x); }
double Cos(double x) { return Sleef_cos_u10(x); }
double Tan(double x) { return Sleef_tan_u10(x); }
double Acos(double x) { return Sleef_acos_u10(x); }
double Log10(double x) { return Sleef_log10_u10(x); }
float SinF(float x) { return Sleef_sinf_u10(x); }
float CosF(float x) { return Sleef_cosf_u10(x); }
float TanF(float x) { return Sleef_tanf_u10(x); }
float AcosF(float x) { return Sleef_acosf_u10(x); }
const Impl Table{"sleef", Sin, Cos, Tan, Acos, Log10, SinF, CosF, TanF, AcosF};

double DSin(double x) { return Sleef_cinz_sind1_u10purec(x); }
double DCos(double x) { return Sleef_cinz_cosd1_u10purec(x); }
double DTan(double x) { return Sleef_cinz_tand1_u10purec(x); }
double DAcos(double x) { return Sleef_cinz_acosd1_u10purec(x); }
double DLog10(double x) { return Sleef_cinz_log10d1_u10purec(x); }
float DSinF(float x) { return Sleef_cinz_sinf1_u10purec(x); }
float DCosF(float x) { return Sleef_cinz_cosf1_u10purec(x); }
float DTanF(float x) { return Sleef_cinz_tanf1_u10purec(x); }
float DAcosF(float x) { return Sleef_cinz_acosf1_u10purec(x); }
const Impl DetTable{"sleef-cinz", DSin, DCos, DTan, DAcos, DLog10, DSinF, DCosF, DTanF, DAcosF};
} // namespace
const Impl* ImplSleef() { return &Table; }
const Impl* ImplSleefDet() { return &DetTable; }
