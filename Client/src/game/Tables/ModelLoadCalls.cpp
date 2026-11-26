#include "Tables/ModelLoadCalls.h"

#include "Data/Models/ModelFiles.h"
#include "Tables/ModelTextureCalls.h"

#include <array>
#include <span>

namespace Client::Data::Models
{
namespace
{
constexpr std::array<ModelFile, 15> Files{{{0xe5, "Skill\\", "BigStone", 1},
                                           {0xe6, "Skill\\", "BigStone", 2},
                                           {0x10a, "Object12\\", "StoneCoffin", 1},
                                           {0x10b, "Object12\\", "StoneCoffin", 2},
                                           {0x114, "Skill\\", "m_Waves", -1},
                                           {0x115, "Skill\\", "m_Piercing", -1},
                                           {0x12b, "Skill\\", "FlyBigStone1", -1},
                                           {0x12c, "Skill\\", "FlyBigStone2", -1},
                                           {0x12d, "Skill\\", "FlySmallStone1", -1},
                                           {0x12e, "Skill\\", "FlySmallStone2", -1},
                                           {0x12f, "Skill\\", "WallStone1", -1},
                                           {0x130, "Skill\\", "WallStone2", -1},
                                           {0x131, "Skill\\", "GatePart1", -1},
                                           {0x132, "Skill\\", "GatePart2", -1},
                                           {0x133, "Skill\\", "GatePart3", -1}}};
constexpr std::array<ModelTextureCall, 4> BeforeSkill{
    {{0x10d, "Monster\\"}, {0x10a, "Monster\\"}, {0x10d, "Monster\\"}, {0x10b, "Monster\\"}}};
// Folders read from the ASM's PUSHes and the PE's strings: 6cc2cc=Npc, 6cde84=Object31, 6cc2d4=Monster.
constexpr std::array<ModelTextureCall, 26> Textures{
    {{0x114, "Skill\\"},   {0x115, "Skill\\"},    {0x12b, "Npc\\"},      {0x12c, "Skill\\"},   {0x12d, "Skill\\"},
     {0x12e, "Skill\\"},   {0x12f, "Object31\\"}, {0x130, "Object31\\"}, {0x131, "Monster\\"}, {0x132, "Monster\\"},
     {0x133, "Monster\\"}, {0xe5, "Skill\\"},     {0xe6, "Skill\\"},     {0x10a, "Skill\\"},   {0x10b, "Skill\\"},
     {0x114, "Skill\\"},   {0x115, "Skill\\"},    {0x12b, "Skill\\"},    {0x12c, "Skill\\"},   {0x12d, "Skill\\"},
     {0x12e, "Skill\\"},   {0x12f, "Skill\\"},    {0x130, "Skill\\"},    {0x131, "Skill\\"},   {0x132, "Skill\\"},
     {0x133, "Skill\\"}}};
} // namespace
std::span<const ModelFile> SiegeModelFiles()
{
    return Files;
}
std::span<const ModelTextureCall> SiegeBeforeSkillTextureCalls()
{
    return BeforeSkill;
}
std::span<const ModelTextureCall> SiegeTextureCalls()
{
    return Textures;
}
} // namespace Client::Data::Models
