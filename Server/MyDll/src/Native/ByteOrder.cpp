#include "ByteOrder.h"

// 0x10004b4f: reverse an unsigned 64-bit value without external state.
mydll_u64 __cdecl mydll_util_bswap64(mydll_u64 qwVal)
{
    mydll_u64 j = 0;
    BYTE* pFrom = 0;
    BYTE* pOut = 0;
    int k;

    pOut = (BYTE*)&j;
    pFrom = (BYTE*)&qwVal;
    pFrom += 7;
    for (k = 0; k < 8; k++)
        *(pOut + k) = *(pFrom - k);
    return j;
}

// 0x10004bba: reverse eight bytes in place.
void __cdecl mydll_util_bswap64_inplace(BYTE* p)
{
    BYTE bt = 0xCC;
    int i;

    for (i = 0; i < 4; i++)
    {
        bt = *(p + i);
        *(p + i) = *(p + 7 - i);
        *(p + 7 - i) = bt;
    }
}

// 0x10004c0e: reverse four bytes in place.
void __cdecl mydll_util_bswap32_inplace(BYTE* p)
{
    BYTE bt = 0xCC;
    int i;

    for (i = 0; i < 2; i++)
    {
        bt = *(p + i);
        *(p + i) = *(p + 3 - i);
        *(p + 3 - i) = bt;
    }
}
