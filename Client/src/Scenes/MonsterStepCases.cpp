#include "Scenes/MonsterStepCases.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Animation.h"
#include "Scenes/ActorState.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldTexts.h"
#include "UI/SprintfFormat.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t CastleMap = 30;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// El dec de 32 bits, que da la vuelta en el mínimo.
std::int32_t Decremented(std::int32_t value)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 1u);
}

// 0x42db4b..0x42dc28: las puertas 286 y 287.
Core::Expected<void, std::string> StepTheGate(ActorState& actor, const CastleObjectStep& step)
{
    WorldRuntime& runtime = step.runtime;
    constexpr float Reach = 500.0f; // 0x43fa0000
    constexpr std::uint8_t Front = 1;
    constexpr std::int32_t OpenTime = 300; // 0x12c
    constexpr std::uint8_t Opened = 2;
    const auto setAction = [&actor, &step](std::uint8_t action,
                                           std::string_view address) -> Core::Expected<void, std::string>
    {
        if (step.skeleton == nullptr)
            return Core::Unexpected(std::format("{}: falta el modelo {:#x} para 0x4ea030", address, actor.type));
        Render::Models::SetAction(actor.animation, *step.skeleton, action);
        return {};
    };
    if (actor.byte110 == 0)
    {
        actor.dword100 = 0;
        if (PlayerNear(actor, step.pool, Front, Reach) || runtime.castleByte7239d8 == Opened)
        {
            constexpr std::uint8_t OpenedAction = 4;
            if (auto set = setAction(OpenedAction, "0x42db87"); !set.HasValue())
                return set;
            runtime.castleByte7239d8 = Opened;
            actor.byte110 = 1;
            actor.dword100 = OpenTime;
        }
    }
    constexpr std::uint8_t OpeningAction = 5;
    if (actor.animation.action != OpeningAction && actor.byte110 != 0 &&
        (actor.dword100 < 0 || runtime.castleByte7239d8 == 0))
    {
        if (PlayerNear(actor, step.pool, Front, Reach))
        {
            actor.dword100 = OpenTime;
            runtime.castleByte7239d8 = Opened;
        }
        else
        {
            if (auto set = setAction(0, "0x42dc00"); !set.HasValue())
                return set;
            actor.byte110 = 0;
            runtime.castleByte7239d8 = 0;
        }
    }
    actor.dword100 = Decremented(actor.dword100);
    return {};
}
} // namespace

Core::Expected<void, std::string> QueueTheGateName(const ActorState& actor, const Data::Text::TextTable* texts,
                                                   WorldTextQueue& queue)
{
    constexpr std::size_t LevelText = 1176; // 0x77133fc
    if (texts == nullptr)
        return Core::Unexpected<std::string>("0x4fd63b: falta Text.bmd para el nombre del 0xc5 (Text[1176])");
    const std::optional<std::string_view> format = texts->Text(LevelText);
    if (!format)
        return Core::Unexpected<std::string>("0x4fd63b: Text.bmd no tiene el texto 1176");
    const std::string name = ActorText(actor);
    std::string text = UI::Sprintf("%s .", std::string_view(name));
    text += UI::Sprintf(*format, static_cast<std::uint32_t>(actor.level));
    // 0x4fd5f3: los dos buffers de la pila son de 100 bytes (0xf8 − 0x94).
    constexpr std::size_t Buffer = 100;
    if (text.size() >= Buffer)
        return Core::Unexpected(std::format("0x4fd67c: el nombre del 0xc5 con su nivel ocupa {} bytes y se pasa del "
                                            "buffer de 100",
                                            text.size() + 1));
    QueueWorldText(queue, text, actor.position);
    return {};
}

void StepTheFallingBall(ActorState& actor, const std::function<double(float, float)>& height)
{
    actor.position.z = F32(D(actor.position.z) + D(actor.floatE4));
    constexpr double Gravity = 6.0; // 0x6b87f4
    constexpr double Lift = 30.0;   // 0x6b8858
    constexpr float Bounce = -0.4f; // 0x6ba18c
    constexpr float Damping = 0.8f; // 0x6b8a38
    actor.floatE4 = F32(D(actor.floatE4) - Gravity);
    const double ground = height(actor.position.x, actor.position.y) + Lift;
    // 0x4fb950: fcom con la z guardada; con la altura por encima (ordenada), la z pasa a ella.
    if (ground > D(actor.position.z))
    {
        actor.position.z = F32(ground);
        actor.floatE4 = F32(D(actor.floatE4) * D(Bounce));
    }
    actor.angles.x = F32(D(actor.angles.x) + D(actor.floatCC));
    actor.angles.y = F32(D(actor.angles.y) + D(actor.floatD0));
    actor.angles.z = F32(D(actor.floatD4) + D(actor.angles.z));
    actor.floatCC = F32(D(actor.floatCC) * D(Damping));
    actor.floatD0 = F32(D(actor.floatD0) * D(Damping));
    actor.floatD4 = F32(D(actor.floatD4) * D(Damping));
}

bool PlayerNear(const ActorState& object, std::span<const ActorState> pool, std::uint8_t mode, float range)
{
    constexpr std::uint8_t PlayerKind = 1;
    constexpr std::size_t Pool = 400; // 0x190
    for (std::size_t slot = 0; slot < std::min(pool.size(), Pool); ++slot)
    {
        const ActorState& player = pool[slot];
        if (player.byte88 != PlayerKind || player.type != Data::Models::PlayerSkeletonModel || !player.seen)
            continue;
        const double dx = D(object.position.x) - D(player.position.x);
        const double dy = D(object.position.y) - D(player.position.y);
        switch (mode)
        {
        case 0:
            // 0x42a05c..0x42a07c: C0 con NaN también.
            if (!(dx >= D(range)) && !(dy >= 100.0))
                return true;
            break;
        case 1:
            // 0x42a085..0x42a0b2: |dx| debajo de 1 (o NaN), dy mayor que 0 y debajo del rango.
            if (!(std::abs(dx) >= 1.0) && dy > 0.0 && !(dy >= D(range)))
                return true;
            break;
        case 2:
            if (!(std::sqrt(dy * dy + dx * dx) >= D(range)))
                return true;
            break;
        default:
            break;
        }
    }
    return false;
}

Core::Expected<void, std::string> StepTheCastleObject(ActorState& actor, const CastleObjectStep& step)
{
    WorldRuntime& runtime = step.runtime;
    if (runtime.map != CastleMap)
        return {};
    switch (actor.word376)
    {
    case 216: // 0x42dacb
    {
        if (!step.height)
            return Core::Unexpected<std::string>("0x42dad6: falta la altura del terreno (0x5f93c0) para el 216");
        constexpr double Raise = 200.0; // 0x6b8928
        actor.position.z = F32(step.height(actor.position.x, actor.position.y) + Raise);
        runtime.castleFlags7239e4 = actor.flags78;
        actor.byte42c = actor.flags78 != 0 ? 1 : 0;
        return {};
    }
    case 277: // 0x42db0f
        runtime.castleGatesClosed = 1;
        return {};
    case 278: // 0x42dc29
    {
        constexpr std::uint8_t Marked = 0x80;
        if (actor.byte42d == Marked)
        {
            runtime.castleMarkX = actor.position.x;
            runtime.castleMarkY = actor.position.y;
        }
        return {};
    }
    case 283: // 0x42db1c: 0x4294b0.
        actor.dword58 = runtime.siegeActive ? -1 : -2;
        actor.byte42c = runtime.siegeActive ? 1 : 0;
        return {};
    case 286:
    case 287:
        return StepTheGate(actor, step);
    default:
        return {};
    }
}
} // namespace Client::Scenes
