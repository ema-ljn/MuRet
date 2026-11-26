#include "Commands.h"

#include <string.h>

extern "C" int __stdcall mydll_cmd_is_slash(void* lpThis, char* lpMsg, int aLen)
{
    int result = 0;
    if (lpMsg[0xd] == '/')
        result = 1;
    return result;
}

// 0x1000f24a: /YES, after the dispatcher's text offset.
extern "C" int __stdcall mydll_cmd_is_yes(int aIndex, LPBYTE lpMsg, int aLen)
{
    int result = 0;
    char szCmd[5] = "/YES";

    if (memcmp(szCmd, _strupr((char*)lpMsg + 0xd), strlen(szCmd)) == 0)
        result = 1;
    return result;
}
