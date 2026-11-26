#pragma once

#include <stddef.h>

#ifdef _MSC_VER
#define MYDLL_STDCALL __stdcall
#else
#define MYDLL_STDCALL __attribute__((stdcall))
#endif

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef BYTE* LPBYTE;
