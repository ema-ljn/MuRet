// Force-included ahead of the client sources and glm: every sin, cos, tan and acos, float or double,
// lands on the correctly rounded functions of cr_impl.cpp. A probe trick, not how the client would do it.
#pragma once
#include <cmath>
#include <complex>
#include <valarray>

namespace std
{
float cr_sin(float x);
double cr_sin(double x);
float cr_cos(float x);
double cr_cos(double x);
float cr_tan(float x);
double cr_tan(double x);
float cr_acos(float x);
double cr_acos(double x);
} // namespace std

#define sin cr_sin
#define cos cr_cos
#define tan cr_tan
#define acos cr_acos
