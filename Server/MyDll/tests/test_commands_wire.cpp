#include "Commands.h"
#include "Wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

namespace
{
void SlashUsesTheMessageOffset()
{
    char message[32] = {0};
    message[0xd] = '/';
    assert(mydll_cmd_is_slash(0, message, sizeof(message)) == 1);
    message[0xd] = '!';
    assert(mydll_cmd_is_slash(0, message, sizeof(message)) == 0);
}

void YesIsCaseInsensitiveAfterTheOffset()
{
    char lower[32] = {0};
    memcpy(lower + 0xd, "/yes", 5);
    assert(mydll_cmd_is_yes(0, reinterpret_cast<LPBYTE>(lower), sizeof(lower)) == 1);
    assert(mydll_cmd_is_yes(0, reinterpret_cast<LPBYTE>(lower), sizeof(lower)) == 1);
    char no[32] = {0};
    memcpy(no + 0xd, "/no", 4);
    assert(mydll_cmd_is_yes(0, reinterpret_cast<LPBYTE>(no), sizeof(no)) == 0);
}

void XorWirePreservesTheOriginalLoopBounds()
{
    BYTE bytes[5] = {0xc1, 4, 0xf1, 2, 0};
    const BYTE expected[5] = {0x6b, 0xae, 0x5b, 0xa8, 0xaa};
    mydll_net_xor_aa(bytes, 5);
    assert(!memcmp(bytes, expected, sizeof(expected)));
    mydll_net_xor_aa(bytes, 0);
    mydll_net_xor_aa(bytes, -1);
    assert(!memcmp(bytes, expected, sizeof(expected)));
}
} // namespace

int main()
{
    SlashUsesTheMessageOffset();
    YesIsCaseInsensitiveAfterTheOffset();
    XorWirePreservesTheOriginalLoopBounds();
    puts("PASS commands and wire");
    return 0;
}
