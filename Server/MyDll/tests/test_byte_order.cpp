#include "ByteOrder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace
{
void FixedAndGeneratedValuesMatchIndependentReversal()
{
    static const mydll_u64 fixed[] = {0, ~mydll_u64(0), mydll_u64(1) << 56, mydll_u64(1) << 63,
                                      (mydll_u64(0x01234567) << 32) | mydll_u64(0x89abcdefUL)};
    unsigned state = 5;
    for (unsigned caseIndex = 0; caseIndex < 49; ++caseIndex)
    {
        mydll_u64 input;
        if (caseIndex < sizeof(fixed) / sizeof(fixed[0]))
            input = fixed[caseIndex];
        else
        {
            unsigned low = state = state * 1103515245 + 12345;
            unsigned high = state = state * 1103515245 + 12345;
            input = (mydll_u64(high) << 32) | low;
        }
        BYTE expected[8];
        BYTE actual[8];
        for (unsigned i = 0; i < 8; ++i)
        {
            expected[i] = static_cast<BYTE>(input >> (i * 8));
            actual[i] = expected[7 - i];
        }
        mydll_u64 reversed;
        memcpy(&reversed, actual, 8);
        assert(mydll_util_bswap64(input) == reversed);
    }
}

void InPlaceReversalsLeaveCanariesIntact()
{
    unsigned j;
    for (unsigned i = 0; i < 48; ++i)
    {
        BYTE buffer[10];
        for (j = 0; j < sizeof(buffer); ++j)
            buffer[j] = static_cast<BYTE>(i * 11 + j);
        BYTE expected[10];
        memcpy(expected, buffer, sizeof(buffer));
        buffer[0] = buffer[9] = 0xee;
        expected[0] = expected[9] = 0xee;
        const unsigned count = i < 24 ? 8 : 4;
        for (j = 0; j < count / 2; ++j)
        {
            const BYTE swap = expected[j];
            expected[j] = expected[count - 1 - j];
            expected[count - 1 - j] = swap;
        }
        if (count == 8)
            mydll_util_bswap64_inplace(buffer + 1);
        else
            mydll_util_bswap32_inplace(buffer + 1);
        assert(!memcmp(buffer, expected, sizeof(expected)));
    }
}
} // namespace

int main()
{
    FixedAndGeneratedValuesMatchIndependentReversal();
    InPlaceReversalsLeaveCanariesIntact();
    puts("PASS byte order");
    return 0;
}
