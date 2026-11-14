#include "Scenes/CombatActionSpeeds.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <initializer_list>

namespace Client::Scenes
{
namespace
{
// Los offsets nativos de velocidad son 4 + 16·acción (0x4f444a..0x4f48c1).
void SetOffsets(PlayerActionSpeeds& speeds, std::initializer_list<std::size_t> offsets, double value)
{
    for (const auto offset : offsets)
        speeds.Set((offset - 4) / 16, static_cast<float>(value));
}
void SetRange(PlayerActionSpeeds& speeds, std::size_t first, std::size_t last, double value)
{
    for (auto offset = first; offset <= last; offset += 16)
        speeds.Set((offset - 4) / 16, static_cast<float>(value));
}
void SetAttackSpeeds(PlayerActionSpeeds& speeds, float attack)
{
    const double base = static_cast<double>(attack);
    const double quarter = base + 0.25;
    const double third = base + static_cast<double>(0.3f);
    SetOffsets(speeds, {0x224}, base + static_cast<double>(0.6f));
    SetRange(speeds, 0x234, 0x374, quarter);
    SetOffsets(speeds, {0x384, 0x394, 0x3b4}, third);
    SetOffsets(speeds, {0x3a4}, base + static_cast<double>(0.27f));
    SetOffsets(speeds, {0x3c4, 0x3d4}, base + static_cast<double>(0.24f));
    SetOffsets(speeds, {0x434, 0x8d4}, quarter);
    SetOffsets(speeds, {0x3e4}, static_cast<double>(0.38f));
    SetOffsets(speeds, {0x3f4}, static_cast<double>(0.34f));
    SetOffsets(speeds, {0x424, 0x404, 0x414}, third);
    SetRange(speeds, 0x2e4, 0x314, third);
    SetRange(speeds, 0x364, 0x374, third);
    SetOffsets(speeds, {0x454, 0x4f4, 0x534, 0x5a4}, static_cast<double>(0.3f));
    SetOffsets(speeds, {0x4c4, 0x504, 0x564, 0x5c4, 0x5f4, 0x604, 0x594}, quarter);
    SetOffsets(speeds, {0x4d4, 0x874}, static_cast<double>(0.28f));
    SetOffsets(speeds, {0x4e4, 0x584, 0x864}, base + static_cast<double>(0.2f));
    SetOffsets(speeds, {0x5e4, 0x614, 0x854}, third);
    SetRange(speeds, 0x7e4, 0x834, third);
    SetOffsets(speeds, {0x844}, base + static_cast<double>(0.4f));
}
void SetMagicSpeeds(PlayerActionSpeeds& speeds, float magic, float halfMagic)
{
    const double half = static_cast<double>(halfMagic);
    SetOffsets(speeds, {0x924}, static_cast<double>(magic) + 0.25);
    SetRange(speeds, 0x8e4, 0x914, half + static_cast<double>(0.29f));
    SetOffsets(speeds, {0x934, 0x974, 0x524}, half + static_cast<double>(0.3f));
    SetOffsets(speeds, {0x944, 0x514, 0x5b4}, half + static_cast<double>(0.4f));
    SetOffsets(speeds, {0x954}, half + static_cast<double>(0.6f));
    SetOffsets(speeds, {0x964, 0x444}, half + 0.5);
    SetOffsets(speeds, {0x5d4}, half + static_cast<double>(0.37f));
}
} // namespace
void RefreshCombatActionSpeeds(const WorldEntryResources& resources)
{
    RefreshCombatActionSpeeds(resources.world, resources.cloth.random);
}
void RefreshCombatActionSpeeds(WorldState& world, Render::Models::CrtRandom& random)
{
    float attack = 0.0f;
    float magic = 0.0f;
    float halfMagic = 0.0f;
    {
        const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                                   Render::Models::OwnRecordCloseRandoms);
        const auto& own = world.OwnCharacter();
        constexpr double FullFactor = static_cast<double>(0.004f);
        constexpr double HalfFactor = static_cast<double>(0.002f);
        attack = static_cast<float>(static_cast<double>(own.attackSpeed) * FullFactor);
        magic = static_cast<float>(static_cast<double>(own.magicSpeed) * FullFactor);
        halfMagic = static_cast<float>(static_cast<double>(own.magicSpeed) * HalfFactor);
    }
    SetAttackSpeeds(world.ActionSpeeds(), attack);
    SetMagicSpeeds(world.ActionSpeeds(), magic, halfMagic);
}
} // namespace Client::Scenes
