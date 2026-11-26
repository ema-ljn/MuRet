#pragma once

#include <windows.h>

namespace MyDll
{
namespace Boot
{
typedef bool(__cdecl* TerrainLoader)(unsigned map, char* path);

struct TerrainResult
{
    unsigned loaded;
    unsigned failedMap;
    bool complete;
};

TerrainResult LoadTerrainPlan(const char* directory, TerrainLoader load);
void CallTerrainLoader(DWORD entry, void* object, char* path, unsigned map);
bool PrepareTerrain();
void __cdecl LoadGameServerTerrain();
} // namespace Boot
} // namespace MyDll
