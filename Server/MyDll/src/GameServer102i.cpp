#include "HostBoot.h"

#include <stddef.h>

namespace MyDll
{
namespace Boot
{
namespace
{
const DWORD ItemBytes = 0x0376a100UL;
const DWORD BloodCastleBytes = 0x00001300UL;
const DWORD ObjectBytes = 0x011d6680UL;
const DWORD MonsterBytes = 0x0001400cUL;

struct Backing
{
    unsigned char items[ItemBytes];
    unsigned char bloodCastle[BloodCastleBytes];
    unsigned char objects[ObjectBytes];
    unsigned char monsters[MonsterBytes];
};

typedef char BackingLayoutCheck[offsetof(Backing, items) == 0 && offsetof(Backing, bloodCastle) == 0x0376a100UL &&
                                        offsetof(Backing, objects) == 0x0376b400UL &&
                                        offsetof(Backing, monsters) == 0x04941a80UL && sizeof(Backing) == 0x04955a8cUL
                                    ? 1
                                    : -1];
Backing s_backing;

const Relocation Relocations[] = {
#include "GameServer102iRelocations.inc"
};
const unsigned RelocationCount = sizeof(Relocations) / sizeof(Relocations[0]);
typedef char RelocationCountCheck[RelocationCount == 2814 ? 1 : -1];

const unsigned char InitReturn[] = {0xeb, 0x08};
const unsigned char ReceiveSelector[] = {0xa1, 0x28, 0x2f, 0x31, 0x07, 0x85, 0xc0, 0x74, 0x07, 0xff, 0xe0};
const unsigned char SendSelector[] = {0xa1, 0x34, 0x2f, 0x31, 0x07, 0x85, 0xc0, 0x74, 0x07, 0xff, 0xe0};
const unsigned char NativeDispatch[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c, 0x53, 0x56, 0x57};
const unsigned char AreaShortTail[] = {0x85, 0xc0, 0x74, 0x01, 0xc3, 0xeb, 0x0b};
const unsigned char AreaLongTail[] = {0x90, 0x90, 0x83, 0xc4, 0x10, 0x5f, 0x5e, 0x5b, 0x8b, 0xe5, 0x5d, 0xc3};
const unsigned char NativeMapSelector[] = {0xa1, 0x48, 0x2f, 0x31, 0x07, 0x85, 0xc0, 0x75, 0x0a, 0xa1,
                                           0xac, 0xe1, 0x50, 0x00, 0xe9, 0x53, 0x03, 0x00, 0x00};
#ifdef MYDLL_SELECTION
const unsigned char CharacterListCall[] = {0x50, 0xff, 0x15, 0x0c, 0x2f, 0x31, 0x07};
const unsigned char NativeListDispatch[] = {0x55, 0x8b, 0xec, 0x81, 0xec, 0xb0, 0x01, 0x00, 0x00};
const unsigned char CreateClassGuard[] = {0x83, 0xf9, 0x40, 0x7f, 0x1c, 0xeb, 0x32};
const unsigned char TerrainReturn[] = {0xff, 0xd0, 0xe9, 0xf4, 0x06, 0x00, 0x00};
const unsigned char TerrainEntry[] = {0xe9, 0x38, 0x1d, 0x04, 0x00};
const unsigned char TerrainDispatch[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x44, 0x53, 0x56, 0x57, 0x89, 0x4d, 0xfc};
const unsigned char TerrainCleanup[] = {0xc2, 0x08, 0x00};
#endif

const Signature Signatures[] = {
    {0x0048f736, InitReturn, sizeof(InitReturn)},
    {0x00414170, ReceiveSelector, sizeof(ReceiveSelector)},
    {0x00414180, NativeDispatch, sizeof(NativeDispatch)},
    {0x00440230, SendSelector, sizeof(SendSelector)},
    {0x00440240, NativeDispatch, sizeof(NativeDispatch)},
    {0x00415b7e, AreaShortTail, sizeof(AreaShortTail)},
    {0x00427975, AreaLongTail, sizeof(AreaLongTail)},
    {0x00489fa8, NativeMapSelector, sizeof(NativeMapSelector)},
#ifdef MYDLL_SELECTION
    {0x0040e814, CharacterListCall, sizeof(CharacterListCall)},
    {0x0040f6c0, NativeListDispatch, sizeof(NativeListDispatch)},
    {0x004174f5, CreateClassGuard, sizeof(CreateClassGuard)},
    {0x00489fbb, TerrainReturn, sizeof(TerrainReturn)},
    {0x00401fc3, TerrainEntry, sizeof(TerrainEntry)},
    {0x00443d00, TerrainDispatch, sizeof(TerrainDispatch)},
    {0x00443d47, TerrainCleanup, sizeof(TerrainCleanup)},
#endif
};

const HostLayout Layout = {
    0x00400000,
    0x07ddc000,
    0x0048f730,
    0x073112ac,
    {0x07312f08, 0x07312f0c, 0x07312f24, 0x07312f28, 0x07312f2c, 0x07312f34, 0x07312f60, 0x07312f48},
    Signatures,
    sizeof(Signatures) / sizeof(Signatures[0]),
    Relocations,
    RelocationCount};
} // namespace

const HostLayout& GameServer102iLayout()
{
    return Layout;
}

unsigned char* GameServer102iBacking(DWORD& size)
{
    size = sizeof(s_backing);
    return reinterpret_cast<unsigned char*>(&s_backing);
}

const unsigned char* GameServer102iObjects()
{
    return s_backing.objects;
}
} // namespace Boot
} // namespace MyDll
