#include "Scenes/WorldSpecialModels.h"

#include "Data/Models/ModelFiles.h"
#include "Scenes/WorldModels.h"

#include <array>
#include <cstdint>

namespace Client::Scenes
{
namespace
{
enum class SpecialMap : std::uint8_t
{
    Lorencia = 0,
    Dungeon = 1,
    Devias = 2,
    Noria = 3,
    LostTower = 4,
    Exile = 5,
    Stadium = 6,
    Atlans = 7,
    Tarkan = 8,
    Icarus = 10,
    FirstBloodCastle = 11,
    LastBloodCastle = 17,
    FirstChaosCastle = 18,
    LastChaosCastle = 23,
    FirstKalima = 24,
    LastKalima = 29,
    LandOfTrials = 31,
    Aida = 33,
    Crywolf = 34,
    SeventhKalima = 36,
    Login = 55,
};

// Cada fila es 0x60da30(modelo, carpeta, nombre, número), en el orden del ASM. Una carpeta de
// especiales no se deriva de Object<N>: Lost Tower comparte Dungeon; Angel usa Player y Npc.
constexpr std::array<Data::Models::ModelFile, 2> Lorencia{
    {{0xad, "Object1\\", "Bird", 1}, {0xb2, "Object1\\", "Fish", 1}}}; // 617345..6173a2
constexpr std::array<Data::Models::ModelFile, 3> Dungeon{{{0xda, "Object2\\", "DungeonStone", 1},
                                                          {0xaf, "Object2\\", "Bat", 1},
                                                          {0xb0, "Object2\\", "Rat", 1}}}; // 6173af..617440
constexpr std::array<Data::Models::ModelFile, 2> Devias{
    {{0x201, "Npc\\", "obj_donkey", -1}, {0x202, "Npc\\", "obj_flag", -1}}}; // 61744d..6174aa
constexpr std::array<Data::Models::ModelFile, 4> Noria{{{0xae, "Object1\\", "Butterfly", 1},
                                                        {0x142, "Npc\\", "warp01", -1},
                                                        {0x143, "Npc\\", "warp02", -1},
                                                        {0x144, "Npc\\", "warp03", -1}}}; // 617520..6175e2
constexpr std::array<Data::Models::ModelFile, 7> Exile{{{0xe7, "Object6\\", "Meteo", 1},
                                                        {0xe8, "Object6\\", "Meteo", 2},
                                                        {0xe9, "Object6\\", "Meteo", 3},
                                                        {0xea, "Object6\\", "Meteo", 4},
                                                        {0xeb, "Object6\\", "Meteo", 5},
                                                        {0xef, "Object6\\", "BossHead", 1},
                                                        {0xf0, "Object6\\", "Princess", 1}}}; // 617633..617685
constexpr std::array<Data::Models::ModelFile, 1> Stadium{{{0xb1, "Object7\\", "Bug", 1}}};    // 6176cb
constexpr std::array<Data::Models::ModelFile, 8> Atlans{{{0xb3, "Object8\\", "Fish", 2},
                                                         {0xb4, "Object8\\", "Fish", 3},
                                                         {0xb5, "Object8\\", "Fish", 4},
                                                         {0xb6, "Object8\\", "Fish", 5},
                                                         {0xb7, "Object8\\", "Fish", 6},
                                                         {0xb8, "Object8\\", "Fish", 7},
                                                         {0xb9, "Object8\\", "Fish", 8},
                                                         {0xba, "Object8\\", "Fish", 9}}};        // 6176f3..617734
constexpr std::array<Data::Models::ModelFile, 1> Tarkan{{{0xb2, "Object9\\", "Bug", 2}}};         // 617864
constexpr std::array<Data::Models::ModelFile, 1> Icarus{{{0xbb, "Object11\\", "cloud", -1}}};     // 6178ba
constexpr std::array<Data::Models::ModelFile, 1> BloodCastle{{{0xbc, "Object12\\", "Crow", 1}}};  // 6179b3
constexpr std::array<Data::Models::ModelFile, 1> ChaosCastle{{{0x1f7, "Player\\", "Angel", -1}}}; // 618970
constexpr std::array<Data::Models::ModelFile, 11> Kalima{{{0x11d, "Monster\\", "cd71a", -1},
                                                          {0x11e, "Monster\\", "cd71b", -1},
                                                          {0x11f, "Monster\\", "cd71c", -1},
                                                          {0x120, "Monster\\", "cd71d", -1},
                                                          {0x121, "Monster\\", "cd71e", -1},
                                                          {0x122, "Monster\\", "cd71f", -1},
                                                          {0x123, "Monster\\", "cd71g", -1},
                                                          {0x124, "Monster\\", "cd71h", -1},
                                                          {0x125, "Skill\\", "dragonhead", -1},
                                                          {0x126, "Skill\\", "phoenix", -1},
                                                          {0x127, "Monster\\", "cundun_gone", -1}}}; // 618a1d..618c45
constexpr std::array<Data::Models::ModelFile, 1> Trials{{{0xae, "Object1\\", "Butterfly", 1}}};      // 61823e
constexpr std::array<Data::Models::ModelFile, 3> Aida{{{0xae, "Object1\\", "Butterfly", 1},
                                                       {0x141, "Object34\\", "tree_eff", -1},
                                                       {0xb2, "Object9\\", "Bug", 2}}}; // 618566..6185e6
constexpr std::array<Data::Models::ModelFile, 3> Crywolf{{{0x148, "Object35\\", "scorpion", -1},
                                                          {0xed, "Monster\\", "arrowstusk", -1},
                                                          {0xee, "Monster\\", "arrowstusk", -1}}};   // 617a4e..617b28
constexpr std::array<Data::Models::ModelFile, 1> Login{{{DragonModel, "Object56\\", "Dragon", -1}}}; // 6188bf
} // namespace

WorldSpecialModelSet SpecialWorldModelsOf(std::uint8_t map)
{
    const auto world = static_cast<SpecialMap>(map);
    if (world >= SpecialMap::FirstBloodCastle && world <= SpecialMap::LastBloodCastle)
        return {BloodCastle, WorldSpecialKind::BloodCastle, {}};
    if (world >= SpecialMap::FirstChaosCastle && world <= SpecialMap::LastChaosCastle)
        return {ChaosCastle, WorldSpecialKind::ChaosCastle, "Npc\\"};
    if ((world >= SpecialMap::FirstKalima && world <= SpecialMap::LastKalima) || world == SpecialMap::SeventhKalima)
        return {Kalima, WorldSpecialKind::Kalima, {}};
    switch (world)
    {
    case SpecialMap::Lorencia:
        return {Lorencia, {}, {}};
    case SpecialMap::Dungeon:
    case SpecialMap::LostTower:
        return {Dungeon, {}, {}};
    case SpecialMap::Devias:
        return {Devias, {}, {}};
    case SpecialMap::Noria:
        return {Noria, WorldSpecialKind::Noria, {}};
    case SpecialMap::Exile:
        return {Exile, WorldSpecialKind::Exile, {}};
    case SpecialMap::Stadium:
        return {Stadium, {}, {}};
    case SpecialMap::Atlans:
        return {Atlans, {}, {}};
    case SpecialMap::Tarkan:
        return {Tarkan, {}, {}};
    case SpecialMap::Icarus:
        return {Icarus, {}, {}};
    case SpecialMap::LandOfTrials:
        return {Trials, {}, {}};
    case SpecialMap::Aida:
        return {Aida, {}, {}};
    case SpecialMap::Crywolf:
        return {Crywolf, {}, {}};
    case SpecialMap::Login:
        return {Login, {}, {}};
    default:
        return {};
    }
}
} // namespace Client::Scenes
