#pragma once

#include "Types.h"

#ifdef _MSC_VER
typedef unsigned __int64 mydll_u64;
#else
typedef unsigned long long mydll_u64;
#endif

mydll_u64 __cdecl mydll_util_bswap64(mydll_u64 qwVal);
void __cdecl mydll_util_bswap64_inplace(BYTE* p);
void __cdecl mydll_util_bswap32_inplace(BYTE* p);
