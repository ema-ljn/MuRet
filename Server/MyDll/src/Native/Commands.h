#pragma once

#include "Types.h"

extern "C" int __stdcall mydll_cmd_is_slash(void* lpThis, char* lpMsg, int aLen);
extern "C" int __stdcall mydll_cmd_is_yes(int aIndex, LPBYTE lpMsg, int aLen);
