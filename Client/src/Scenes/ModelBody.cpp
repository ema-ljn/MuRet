#include "Scenes/ModelBody.h"

#include "Core/Expected.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x4f96e2..0x4f9737.
constexpr std::uint16_t BlinkingModel = 0x192;
constexpr std::uint16_t SlowDyingModel = 0x1a9;
constexpr std::uint8_t DyingAction = 6;
constexpr float LateFrame = 6.0f;         // 0x6b87f4
constexpr float FasterLateFactor = 4.0f;  // 0x6b8a44
constexpr float SlowerDyingFactor = 0.5f; // 0x6b882c

// 0x4fcbb8 y 0x4fce68: el tipo que no pasa por 0x4ea030.
constexpr std::uint16_t NamedModel = 0xc5;
// 0x4fcbef..0x4fcbfc: en el mapa 1 (Dungeon), el 0x28.
constexpr std::int32_t DungeonMap = 1;
constexpr std::uint16_t DungeonModel = 0x28;
// 0x4fcc11..0x4fcc1b: los monstruos, de Monster01 (0x152) a antes del 0x1ca.
constexpr std::uint16_t FirstMonsterModel = 0x152;
constexpr std::uint16_t EndOfMonsterModels = 0x1ca;
// 0x4fcc48..0x4fcc62: las acciones que vuelven a la 0.
constexpr std::array<std::uint8_t, 7> RestingActions{1, 5, 3, 4, 8, 9, 0xb};
// 0x4fcc7a..0x4fcc86: el 0x1ce y el 0x1cf.
constexpr std::uint16_t FirstTalkingNpc = 0x1ce;
constexpr std::uint16_t LastTalkingNpc = 0x1cf;
constexpr int TalkingNpcSpan = 16;    // 0x4fcc9f
constexpr int TalkingNpcResting = 12; // 0x4fccab

// 0x42d893..0x42d8c5: el mapa 30 y la tabla de bytes 0x42d9f8 sobre la de saltos 0x42d9e8, con +0x376 − 221.
constexpr std::int32_t CastleMap = 30;
constexpr std::uint32_t CarriedFlag = 0x20000; // 0x42d99d

bool IsOneOf(std::span<const std::uint8_t> values, std::uint8_t value)
{
    for (const std::uint8_t candidate : values)
        if (candidate == value)
            return true;
    return false;
}

// 0x4ea030 con el modelo del personaje.
void SetModelAction(ActorState& actor, const Render::Models::Skeleton& skeleton, std::uint8_t action)
{
    Render::Models::SetAction(actor.animation, skeleton, action);
}

// Lo que 0x42d890 deja de la acción: la anterior (+0x112), la key (+0x118), la acción (+0x111) y el frame (+0x114), sin
// 0x4ea030.
void PutTheAction(ActorState& actor, std::uint8_t action)
{
    actor.animation.previousAction = action;
    actor.animation.previousKey = 0;
    actor.animation.action = action;
    actor.animation.frame = 0.0f;
}

// 0x42d890(c, c): fuera del mapa 30, 0. En el 30, por +0x376.
bool FinishTheCastleObject(ActorState& actor, const Render::Models::Skeleton& skeleton, WorldRuntime& runtime)
{
    if (runtime.map != CastleMap)
        return false;
    switch (actor.word376)
    {
    case 221: // 0x42d981
    case 222:
        SetModelAction(actor, skeleton, 0);
        return true;
    case 277: // 0x42d995
        if ((actor.flags78 & CarriedFlag) == CarriedFlag)
        {
            actor.byte42c = 0;
            SetModelAction(actor, skeleton, 2);
        }
        else if (actor.flags78 == 0)
        {
            actor.byte42c = 1;
            SetModelAction(actor, skeleton, 0);
        }
        return true;
    case 286: // 0x42d8cc
    case 287:
    {
        constexpr std::uint8_t OpenedAction = 4;
        constexpr std::uint8_t OpeningAction = 5;
        constexpr std::uint8_t ClosingAction = 2;
        const std::uint8_t action = actor.animation.action;
        if (action == OpeningAction)
        {
            PutTheAction(actor, OpenedAction);
            constexpr std::uint8_t Opened = 2;
            constexpr std::int32_t OpenedTime = 0x12c;
            runtime.castleByte7239d8 = Opened;
            actor.byte110 = 1;
            actor.dword100 = OpenedTime;
            return true;
        }
        if (action == OpenedAction || action == ClosingAction)
        {
            PutTheAction(actor, OpenedAction);
            return true;
        }
        if (action == 0)
        {
            PutTheAction(actor, 0);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

} // namespace

float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed)
{
    if (IsNewFace(type))
        return speed + speed;
    if (type == BlinkingModel && action == DyingAction && frame > LateFrame)
        return speed * FasterLateFactor;
    if (type == SlowDyingModel && action == DyingAction)
        return speed * SlowerDyingFactor;
    return speed;
}

Core::Expected<ModelActionEnd, std::string> FinishTheModelAction(ActorState& actor, std::size_t slot,
                                                                 const Render::Models::Skeleton& skeleton,
                                                                 WorldRuntime& runtime,
                                                                 Render::Models::CrtRandom& random)
{
    const std::uint16_t type = actor.type;
    if (type == NamedModel)
    {
        actor.animation.action = 1;
        return ModelActionEnd::Done;
    }
    if (FinishTheCastleObject(actor, skeleton, runtime))
        return ModelActionEnd::AdvanceAgain;
    if (runtime.map == DungeonMap && type == DungeonModel)
    {
        SetModelAction(actor, skeleton, 0);
        return ModelActionEnd::Done;
    }
    if (type >= FirstMonsterModel && type < EndOfMonsterModels)
    {
        const std::uint8_t action = actor.animation.action;
        if (action == DyingAction)
        {
            if (actor.byte38a != 0)
                return ModelActionEnd::Done;
            return Core::Unexpected(std::format("0x4fcc3c: el monstruo {:#x} del lugar {} terminó de morir: +0x38a y "
                                                "0x52e810 (los restos, 0x518cf0) son de la CL11",
                                                type, slot));
        }
        if (IsOneOf(RestingActions, action))
            SetModelAction(actor, skeleton, 0);
        return ModelActionEnd::Done;
    }
    if (type >= FirstTalkingNpc && type <= LastTalkingNpc)
    {
        if (random.Next() % TalkingNpcSpan < TalkingNpcResting)
        {
            SetModelAction(actor, skeleton, 0);
            return ModelActionEnd::Done;
        }
        SetModelAction(actor, skeleton, static_cast<std::uint8_t>(1 + random.Next() % 2));
        return ModelActionEnd::Done;
    }
    if (IsNewFace(type))
        return ModelActionEnd::Done;
    SetModelAction(actor, skeleton, static_cast<std::uint8_t>(random.Next() % 2));
    return ModelActionEnd::Done;
}

void LookAtTheTarget(ActorState& actor, const ActorState& target)
{
    const auto heading = static_cast<float>(
        Render::Models::CreateAngle(actor.position.x, actor.position.y, target.position.x, target.position.y));
    // 0x4ea11a..0x4ea128: C0 en 1 (menor o sin orden, NaN) mira.
    constexpr double FacingLimit = 90.0; // 0x6b8948
    const double distance = Render::Models::AngleDistance(actor.angles.z, heading, 1);
    if (!(distance < FacingLimit) && !std::isnan(distance))
    {
        actor.headTurn.target = {};
        return;
    }
    constexpr double HeadHeight = 50.0;               // 0x6b86b0
    constexpr auto Share = static_cast<double>(0.2f); // 0x6b8854
    constexpr float FullTurn = 360.0f;                // 0x6b869c
    actor.headTurn.target.x = static_cast<float>(static_cast<double>(actor.angles.z) - static_cast<double>(heading));
    actor.headTurn.target.y = static_cast<float>(
        (static_cast<double>(target.position.z) - (static_cast<double>(actor.position.z) + HeadHeight)) * Share);
    for (int axis = 0; axis < 2; ++axis)
        if (actor.headTurn.target[axis] < 0.0f)
            actor.headTurn.target[axis] += FullTurn;
}

Core::Expected<void, std::string> SetTheModelHeadTarget(ActorState& actor, std::span<const ActorState> pool,
                                                        Render::Models::CrtRandom& random)
{
    const std::uint8_t action = actor.animation.action;
    if (action == DyingAction)
        return {};
    if (action == 0)
    {
        constexpr int OneIn = 32;
        if (random.Next() % OneIn != 0)
            return {};
        constexpr int TurnSpan = 128;
        constexpr int TurnCentre = 64;
        constexpr int NodSpan = 48;
        constexpr int NodCentre = 16;
        constexpr float FullTurn = 360.0f;
        actor.headTurn.target.x = static_cast<float>((random.Next() % TurnSpan) - TurnCentre);
        actor.headTurn.target.y = static_cast<float>((random.Next() % NodSpan) - NodCentre);
        for (int axis = 0; axis < 2; ++axis)
            if (actor.headTurn.target[axis] < 0.0f)
                actor.headTurn.target[axis] += FullTurn;
        return {};
    }
    constexpr std::uint8_t TrackingAction = 2;
    constexpr std::uint16_t NoTarget = 0xffff;
    if (action == TrackingAction && actor.word3a4 != NoTarget)
    {
        // 0x4ea236: MOVSX, el lugar con signo sobre 0x743a16c.
        const auto target = static_cast<std::int16_t>(actor.word3a4);
        if (target < 0 || static_cast<std::size_t>(target) >= pool.size())
            return Core::Unexpected(
                std::format("0x4ea24c: el blanco +0x3a4 ({}) cae fuera de los {} personajes", target, pool.size()));
        LookAtTheTarget(actor, pool[static_cast<std::size_t>(target)]);
        return {};
    }
    actor.headTurn.target = {};
    return {};
}

std::optional<ModelTrailCut> ModelTrailCutOf(const ActorState& actor)
{
    const std::uint16_t type = actor.type;
    const std::uint8_t action = actor.animation.action;
    const bool swing = action == 3 || action == 4;
    // 0x4fd10b..0x4fd20e.
    bool trail = false;
    constexpr std::array<std::uint16_t, 3> TrailedObjects{0x47, 0x4a, 0x12d};
    constexpr int FirstSword = 0x212;
    constexpr int EndOfSwords = 0x412;
    const int hand = static_cast<std::int16_t>(actor.hands[0].model);
    if (actor.word376 == TrailedObjects[0] || actor.word376 == TrailedObjects[1] || actor.word376 == TrailedObjects[2])
        trail = swing;
    else if (type == 0x194)
        trail = action == 3;
    else if (type == 0x195 || type == 0x197)
        trail = swing;
    else
        trail = hand >= FirstSword && hand < EndOfSwords && swing;
    if (trail)
        return ModelTrailCut{.address = 0x4fd233, .beforeTheRandom = true};
    // 0x4fd5c1..0x4fd5df: los cuatro, en orden. Cada uno solo pose y la estela (0x52e470, CL11) en sus acciones; en las
    // otras no hace nada ni saca rand().
    // 0x441260: el 0x1a7 en la 3 o la 4 (0x44127c).
    if (type == 0x1a7 && swing)
        return ModelTrailCut{.address = 0x441260, .beforeTheRandom = true};
    // 0x42fa40: el 0x1b3 en la 3 (0x42fa64) y el 0x1b1 en la 4 (0x42fb61).
    if ((type == 0x1b3 && action == 3) || (type == 0x1b1 && action == 4))
        return ModelTrailCut{.address = 0x42fa40, .beforeTheRandom = true};
    // 0x432bd0 está portado (Scenes::StepTheSwingsOf1abTo1b4).
    // 0x426d90: el 0x1ba en la 3 (0x426db9), el 0x1b9 en la 3 o la 4 (0x426eb6) y el 0x1b8 en la 3 o la 4 con el frame
    // (+0x114) en 5,06f (0x6b8a30) o menos, o NaN (0x427065: TEST AH, 0x41).
    constexpr float LastTrailFrame = std::bit_cast<float>(0x40a1eb85u);
    const bool late = actor.animation.frame > LastTrailFrame;
    if ((type == 0x1ba && action == 3) || (type == 0x1b9 && swing) || (type == 0x1b8 && swing && !late))
        return ModelTrailCut{.address = 0x426d90, .beforeTheRandom = false};
    return std::nullopt;
}
} // namespace Client::Scenes
