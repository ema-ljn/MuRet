#include "TerrainBoot.h"
#include "BootOutcome.h"
#include "ExecutableDirectory.h"

#include <stdio.h>
#include <string.h>

namespace MyDll
{
namespace Boot
{
namespace
{
struct TerrainRange
{
    unsigned first;
    unsigned last;
    unsigned terrain;
    bool incrementTerrain;
};

// 04-character-selection § Terrain correction
const TerrainRange Ranges[] = {{0, 10, 1, true},    {11, 17, 12, false}, {18, 23, 19, false}, {24, 24, 25, false},
                               {30, 30, 31, false}, {31, 31, 32, false}, {33, 33, 34, false}, {34, 34, 35, false}};
const char DataPath[] = "..\\Data\\";
const unsigned TerrainNameBytes = sizeof("Terrain35.att");
const DWORD NativeLoader = 0x00401fc3;
const DWORD MapTable = 0x07342078;
const DWORD MapStride = 0x0004db68;
const DWORD MapAttributes = 0x10;
const DWORD MapNumber = 0x0004db64;
char s_data[MAX_PATH];

bool __cdecl TerrainPresent(unsigned, char* path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != MAXDWORD && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool __cdecl LoadNativeTerrain(unsigned map, char* path)
{
    unsigned char* const object = reinterpret_cast<unsigned char*>(MapTable + MapStride * map);
    CallTerrainLoader(NativeLoader, object, path, map);
    DWORD attributes;
    DWORD number;
    memcpy(&attributes, object + MapAttributes, sizeof(attributes));
    memcpy(&number, object + MapNumber, sizeof(number));
    return attributes != 0 && number == map;
}
} // namespace

TerrainResult LoadTerrainPlan(const char* directory, TerrainLoader load)
{
    TerrainResult result = {0, 0, false};
    char path[MAX_PATH];
    const size_t length = strlen(directory);
    if (length > sizeof(path) - TerrainNameBytes)
        return result;
    memcpy(path, directory, length);
    for (unsigned i = 0; i < sizeof(Ranges) / sizeof(Ranges[0]); ++i)
    {
        const TerrainRange& range = Ranges[i];
        for (unsigned map = range.first; map <= range.last; ++map)
        {
            const unsigned terrain = range.terrain + (range.incrementTerrain ? map - range.first : 0);
            sprintf(path + length, "Terrain%u.att", terrain);
            if (!load(map, path))
            {
                result.failedMap = map;
                return result;
            }
            ++result.loaded;
        }
    }
    result.complete = true;
    return result;
}

void CallTerrainLoader(DWORD entry, void* object, char* path, unsigned map)
{
    // 0x00443d00: thiscall, ret 8.
    __asm {
        push map
        push path
        mov ecx, object
        call entry
    }
}

bool PrepareTerrain()
{
    DWORD length;
    if (!ExecutableDirectory(s_data, length) || length > sizeof(s_data) - sizeof(DataPath))
        return false;
    memcpy(s_data + length, DataPath, sizeof(DataPath));
    return length + sizeof(DataPath) - 1 <= sizeof(s_data) - TerrainNameBytes;
}

void __cdecl LoadGameServerTerrain()
{
    const TerrainResult preflight = LoadTerrainPlan(s_data, &TerrainPresent);
    if (!preflight.complete)
    {
        ReportTerrainLoad(false, 0, preflight.failedMap);
        return;
    }
    const TerrainResult result = LoadTerrainPlan(s_data, &LoadNativeTerrain);
    ReportTerrainLoad(result.complete, result.loaded, result.failedMap);
}
} // namespace Boot
} // namespace MyDll
