#include "Wire.h"

// 0x1002c651: output XOR for f60.
void MYDLL_STDCALL mydll_net_xor_aa(BYTE* buf, int len)
{
    int i;

    for (i = 0; i < len; i++)
    {
        buf[i] ^= 0xaa;
    }
}
