#include "WorldObjects/WorldObjectMapCreation.h"

#include "Crt/CrtRandom.h"
#include "Kernel/WorldObject.h"
#include "Maps/MapId.h"
#include "WorldObjects/IWorldObjectCreationServices.h"
#include "WorldObjects/WorldObjectRegistrations.h"
#include "WorldObjects/WorldObjects.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace Client::WorldObjects
{
namespace
{
std::uint8_t CreateChaosCastleObject(Kernel::WorldObject& object, std::int32_t map)
{
    // 0x405950: the accepted map returns AL = 1 even if the type writes nothing.
    constexpr std::int32_t FirstChaosCastle = 18;
    constexpr std::int32_t LastChaosCastle = 23;
    if (map < FirstChaosCastle || map > LastChaosCastle)
    {
        return 0;
    }
    if ((object.type >= 18 && object.type <= 21) || (object.type >= 24 && object.type <= 35))
    {
        object.basePosition.x = object.transform.position.x;
        const float y = object.transform.position.y;
        const float z = object.transform.position.z;
        object.state.hiddenMesh = Kernel::HiddenObject;
        object.basePosition.y = y;
        object.basePosition.z = z;
    }
    return 1;
}

std::uint8_t CreateMapObject438A80(Kernel::WorldObject&)
{
    // The three verified bytes 32 c0 c3 of 0x438a80 return AL = 0 without reading the object or the map.
    return 0;
}

enum class CastleStateGroup : std::uint8_t
{
    None,
    VisibleWhenNonzero,
    HiddenWhenNonzero,
};

CastleStateGroup CastleStateGroupOf(std::int16_t type)
{
    // 0x42b480: the signed16 selection comes before the getter and is not re-evaluated after it.
    constexpr std::array<std::int16_t, 20> VisibleWhenNonzero{4,  11, 37, 41, 43, 44, 45, 46, 47, 48,
                                                              67, 68, 69, 71, 72, 73, 74, 75, 76, 88};
    constexpr std::array<std::int16_t, 16> HiddenWhenNonzero{16, 38, 39, 40, 50, 58, 59, 78,
                                                             80, 81, 82, 83, 84, 85, 86, 87};
    if (std::find(VisibleWhenNonzero.begin(), VisibleWhenNonzero.end(), type) != VisibleWhenNonzero.end())
    {
        return CastleStateGroup::VisibleWhenNonzero;
    }
    if (std::find(HiddenWhenNonzero.begin(), HiddenWhenNonzero.end(), type) != HiddenWhenNonzero.end())
    {
        return CastleStateGroup::HiddenWhenNonzero;
    }
    return CastleStateGroup::None;
}

void ApplyCastlePredicate(Kernel::WorldObject& object, std::uint8_t state, bool hideWhenNonzero)
{
    const bool hidden = hideWhenNonzero ? state != 0 : state == 0;
    constexpr std::int16_t NoHiddenMesh = -1;
    object.state.hiddenMesh = hidden ? Kernel::HiddenObject : NoHiddenMesh;
}

std::optional<WorldObjectCreationError>
ApplyCastlePredicateState(Kernel::WorldObject& object, const WorldObjectCreationContext& context, bool hideWhenNonzero)
{
    if (context.state7239Ec == nullptr)
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    // 0x4294b0: one live BYTE per call, without normalization or an invented initial value.
    ApplyCastlePredicate(object, *context.state7239Ec, hideWhenNonzero);
    return std::nullopt;
}

std::optional<WorldObjectCreationError> ApplyCastleObjectState(Kernel::WorldObject& object,
                                                               const WorldObjectCreationContext& context)
{
    if (CastleStateGroupOf(object.type) == CastleStateGroup::None)
    {
        return std::nullopt;
    }
    if (context.state7239Ec == nullptr)
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    ApplyCastleSiegeObjectState(object, *context.state7239Ec);
    return std::nullopt;
}

bool ApplyCreationRandomPhase(Kernel::WorldObject& object, Crt::CrtRandom* random, const float& factor)
{
    if (random == nullptr)
    {
        return false;
    }
    // 0x42c711..0x42c72b and 0x43ff90..0x43ffa9: one rand, signed remainder of 1000 and a store at +0x84.
    constexpr int RandomSpan = 1000;
    const int remainder = random->Next() % RandomSpan;
    // FMUL m32[0x6b88d4] comes after the RNG: take the borrowed storage's value here.
    const float factorValue = factor;
    object.field84 = static_cast<float>(static_cast<double>(remainder) * static_cast<double>(factorValue));
    return true;
}

std::optional<WorldObjectCreationError> ApplyCastleSpecialCase(Kernel::WorldObject& object,
                                                               WorldObjectCreationContext& context)
{
    // 0x42c6c4 stores a single read of the type; no callback changes the already chosen route.
    const std::int16_t type = object.type;
    if (type == 7 || type == 10 || type == 13 || type == 14 || (type >= 61 && type <= 64))
    {
        object.field08 = 0;
        object.field91 = 1;
    }
    else if (type == 8 || type == 9)
    {
        object.field08 = 0;
    }
    else if (type == 19 || type == 415)
    {
        object.transform.scale = 1.0f;
        if (type == 415)
        {
            return ApplyCastlePredicateState(object, context, true);
        }
    }
    else if (type == 39)
    {
        object.state.hiddenMesh = Kernel::HiddenObject;
    }
    else if (type == 41 && !ApplyCreationRandomPhase(object, context.random, context.randomPhaseFactor.get()))
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    if (type == 77 || type == 84)
    {
        RegisterWorldObject(context.registrations, object.identity);
    }
    if (type == 79 &&
        (context.services == nullptr ||
         !context.services->CreateModelEffect({309, object.transform.position, 0.0f, object.identity}, object)))
    {
        return WorldObjectCreationError::UnsupportedEffect;
    }
    return std::nullopt;
}

std::optional<WorldObjectCreationError> CreateCastleSiegeObject(Kernel::WorldObject& object,
                                                                WorldObjectCreationContext& context)
{
    using Maps::CastleSiegeMap;
    if (context.map != CastleSiegeMap)
    {
        return std::nullopt;
    }
    const auto error = ApplyCastleSpecialCase(object, context);
    if (error.has_value())
    {
        return error;
    }
    // H reads the live type after effect 309; its AL return does not cut 0x606660's tail.
    return ApplyCastleObjectState(object, context);
}
std::optional<WorldObjectCreationError> CreateMapObject43FF70(Kernel::WorldObject& object,
                                                              WorldObjectCreationContext& context)
{
    // 0x43ff10 reads the live DWORD here, after C/H; do not reuse the initial dispatch's map.
    constexpr std::int32_t LandOfTrialMap = Maps::LandOfTrialsMap;
    if (context.map != LandOfTrialMap)
    {
        return std::nullopt;
    }
    const std::int16_t type = object.type;
    if ((type == 27 || type == 54) &&
        !ApplyCreationRandomPhase(object, context.random, context.randomPhaseFactor.get()))
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    return std::nullopt;
}

std::uint8_t CreateMapObject42EE30(const WorldObjectCreationContext& context)
{
    // 0x42ee20: a live DWORD equal to 35; the wrapper normalizes AL without effects or RNG.
    constexpr std::int32_t Map35 = Maps::SecondCrywolfMap;
    return static_cast<std::uint8_t>(context.map == Map35);
}

std::uint8_t CreateMapObject4250E0(const WorldObjectCreationContext& context)
{
    // 0x425070: a single 32-bit read for both equalities; the wrapper keeps AL 0/1.
    constexpr std::int32_t Map33 = Maps::AidaMap;
    using Maps::CharacterSelectMap;
    const std::int32_t map = context.map;
    return static_cast<std::uint8_t>(map == Map33 || map == CharacterSelectMap);
}

std::uint8_t CreateMapObject4303D0(const WorldObjectCreationContext& context)
{
    // 0x4303c0: a live DWORD equal to 34; the wrapper normalizes AL without effects or RNG.
    constexpr std::int32_t Map34 = Maps::CrywolfMap;
    return static_cast<std::uint8_t>(context.map == Map34);
}
} // namespace

void ApplyCastleSiegeObjectState(Kernel::WorldObject& object, std::uint8_t siegeState)
{
    const CastleStateGroup group = CastleStateGroupOf(object.type);
    if (group == CastleStateGroup::None)
    {
        return;
    }
    // 0x42b480's native DWORD store happens AFTER the getter and overwrites the previous value.
    ApplyCastlePredicate(object, siegeState, group == CastleStateGroup::HiddenWhenNonzero);
}

std::optional<WorldObjectCreationError> ApplyDefaultWorldObjectCreation(Kernel::WorldObject& object,
                                                                        WorldObjectCreationContext& context)
{
    if (CreateChaosCastleObject(object, context.map) == 1)
    {
        return std::nullopt;
    }
    if (CreateMapObject438A80(object) == 1)
    {
        return std::nullopt;
    }
    const auto castleError = CreateCastleSiegeObject(object, context);
    if (castleError.has_value())
    {
        return castleError;
    }
    // This tail's AL returns are ignored. Only the port's integration gate is checked.
    const auto fourthError = CreateMapObject43FF70(object, context);
    if (fourthError.has_value())
    {
        return fourthError;
    }
    (void)CreateMapObject42EE30(context);
    (void)CreateMapObject4250E0(context);
    (void)CreateMapObject4303D0(context);
    return std::nullopt;
}
} // namespace Client::WorldObjects
