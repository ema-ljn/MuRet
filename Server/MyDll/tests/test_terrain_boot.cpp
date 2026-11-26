#include "TerrainBoot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace MyDll::Boot;

namespace
{
const unsigned BridgeSeven = 17;
const unsigned NoFailure = 99;
const char Directory[] = "C:\\MuServer\\Data\\";
unsigned s_failMap = NoFailure;
unsigned s_calls;
void* s_object;
char* s_path;
unsigned s_map;

bool __cdecl TestLoad(unsigned map, char* path)
{
    const unsigned maps[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
                             15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 30, 31, 33, 34};
    const unsigned terrains[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 12, 12, 12,
                                 12, 12, 12, 19, 19, 19, 19, 19, 19, 25, 31, 32, 34, 35};
    assert(s_calls < sizeof(maps) / sizeof(maps[0]));
    assert(map == maps[s_calls]);
    char expected[MAX_PATH];
    sprintf(expected, "%sTerrain%u.att", Directory, terrains[s_calls]);
    assert(strcmp(path, expected) == 0);
    ++s_calls;
    return map != s_failMap;
}

void LoadsTheSeventhBridgeAndPreservesTheFullOrder()
{
    s_calls = 0;
    const TerrainResult result = LoadTerrainPlan(Directory, &TestLoad);
    assert(result.complete && result.loaded == 29 && s_calls == 29);
}

void DoesNotContinueAfterAMapFails()
{
    s_calls = 0;
    s_failMap = BridgeSeven;
    const TerrainResult result = LoadTerrainPlan(Directory, &TestLoad);
    assert(!result.complete && result.loaded == BridgeSeven && result.failedMap == BridgeSeven);
    assert(s_calls == BridgeSeven + 1);
}

void RejectsOverlongPathBeforeCallingTheHost()
{
    s_calls = 0;
    char path[MAX_PATH];
    memset(path, 'x', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    const TerrainResult result = LoadTerrainPlan(path, &TestLoad);
    assert(!result.complete && !result.loaded && !s_calls);
}

__declspec(naked) void FakeThiscall()
{
    __asm {
        mov s_object, ecx
        mov eax, [esp+4]
        mov s_path, eax
        mov edx, [esp+8]
        mov s_map, edx
        ret 8
    }
}

void CallsTheHostWithThisInEcxAndTwoCalleePoppedArguments()
{
    DWORD before, after;
    char path[] = "Terrain12.att";
    int object;
    __asm mov before, esp;
    CallTerrainLoader(reinterpret_cast<DWORD>(&FakeThiscall), &object, path, BridgeSeven);
    __asm mov after, esp;
    assert(before == after && s_object == &object && s_path == path && s_map == BridgeSeven);
}
} // namespace

int main()
{
    LoadsTheSeventhBridgeAndPreservesTheFullOrder();
    DoesNotContinueAfterAMapFails();
    RejectsOverlongPathBeforeCallingTheHost();
    CallsTheHostWithThisInEcxAndTwoCalleePoppedArguments();
    puts("PASS terrain plan and x86 call boundary");
    return 0;
}
