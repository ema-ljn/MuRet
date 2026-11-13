#include "Scenes/NpcChatter.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x4fcd0c y 0x4fc797: la clase de objeto de los NPC.
constexpr std::uint8_t NpcKind = 4;
// 0x4fcd15..0x4fcd24: los mapas del giro, Lorencia y Devias.
constexpr std::int32_t LorenciaMap = 0;
constexpr std::int32_t DeviasMap = 2;
// 0x4fcd2e..0x4fcd3d: los subtipos del giro, con signo.
constexpr std::int32_t FirstTurnedSubtype = 0xd1;
constexpr std::int32_t LastTurnedSubtype = 0xd3;
// 0x4fcd49: 90 (0x42b40000) en +0x24 en Lorencia.
constexpr float LorenciaTurn = 90.0f;
// 0x4fc7dc: el 0x1cd no habla en el mapa 30.
constexpr std::int32_t CastleMap = 30;
// 0x4fc846: +0x376 del 0x20a que habla.
constexpr std::uint16_t TalkingNpcClass = 0x101;
// 0x6c88b0: los 11 bytes EUC-KR que 0x4fcda2 compara con el nombre (+0x1d9), con strcmp.
constexpr std::array<std::uint8_t, 11> MessageName{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7, 0x20, 0xb1, 0xc3, 0xbc, 0xf6};
// 0x4fcd84 y 0x4fcd9b: 5.000 y 4.000 ms.
constexpr std::uint32_t NamedLineHead = 5000;
constexpr std::uint32_t NamedLineGap = 4000;
// 0x4fcde8 y 0x4fcdef: los dos mensajes del nombre (0x7712970 y 0x7712a9c).
constexpr std::size_t FirstNamedText = 1167;
constexpr std::size_t SecondNamedText = 1168;
// 0x4fc8cb: rand() % 100.
constexpr int ChoiceRange = 100;
// 0x4fc8d5 y 0x4fc8e1: la acción del que está quieto.
constexpr std::uint8_t RestingAction = 1;

// 0x4fc7c4..0x4fc869: los casos del switch sobre el tipo − 0x1cd (las tablas 0x4fc86c y 0x4fc880).
struct NpcLine
{
    std::uint16_t type;
    std::size_t text;
    std::uintptr_t address;
};
constexpr std::array<NpcLine, 4> NpcLines{{{0x1cd, 1974, 0x4fc7fc},
                                           {0x1d3, 1975, 0x4fc81a},
                                           {0x1d5, 1976, 0x4fc838},
                                           {Data::Models::PlayerSkeletonModel, 1827, 0x4fc861}}};

// 0x4fc8f0..0x4fcb05: cada tramo del sorteo, su mensaje en Devias (0x388 o 0x389, con el SETNZ, DEC y AND), de a
// cuánto es el segundo rand() y la acción.
struct TurnedChoice
{
    int below;
    std::size_t text;
    int oneIn;
    std::uint8_t action;
    std::uintptr_t address;
};
constexpr std::array<TurnedChoice, 5> TurnedChoices{{{80, 904, 2, RestingAction, 0x4fc943},
                                                     {85, 904, 2, 0x9d, 0x4fc9ac},
                                                     {90, 904, 2, 0x9f, 0x4fca19},
                                                     {95, 905, 3, 0xab, 0x4fca84},
                                                     {100, 905, 3, 0xa5, 0x4fcaef}}};

// 0x557170(+0x1d9, Text[index], c, 0, −1).
Core::Expected<void, std::string> Say(const ActorState& actor, std::size_t slot, std::size_t index,
                                      const CharacterStepContext& context, std::uintptr_t address)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(
            std::format("{:#x}: el Text {} del globo del lugar {} no está en Text.bmd", address, index, slot));
    if (context.textMeasure == nullptr)
        return Core::Unexpected(std::format("{:#x}: el globo del lugar {} no tiene medidor", address, slot));
    return AddChatBubble(context.world, ChatBubbleMeasure{.measure = *context.textMeasure, .screen = context.screen},
                         ChatNameOf(actor), *text, slot, ChatBubbleMode::Chat);
}
} // namespace

bool HasTheMessageName(const ActorState& actor)
{
    for (std::size_t i = 0; i < actor.name.size(); ++i)
    {
        if (std::to_integer<std::uint8_t>(actor.name[i]) != MessageName[i])
            return false;
    }
    return actor.byte1e3 == MessageName[actor.name.size()];
}

bool TurnTheNpc(ActorState& actor, std::int32_t map)
{
    if (actor.byte88 != NpcKind || (map != LorenciaMap && map != DeviasMap) ||
        actor.type != Data::Models::PlayerSkeletonModel || actor.subtype < FirstTurnedSubtype ||
        actor.subtype > LastTurnedSubtype)
        return false;
    actor.angles = {0.0f, 0.0f, map == LorenciaMap ? LorenciaTurn : 0.0f};
    return true;
}

Core::Expected<void, std::string> SayTheNpcLine(const ActorState& actor, std::size_t slot,
                                                const CharacterStepContext& context)
{
    if (actor.byte88 != NpcKind)
        return {};
    // 0x4fc7a4..0x4fc7b5: un rand() impar no dice nada.
    if (context.random.Next() % 2 != 0)
        return {};
    const std::int32_t map = context.world.Runtime().map;
    for (const NpcLine& line : NpcLines)
    {
        if (actor.type != line.type)
            continue;
        if (line.type == NpcLines[0].type && map == CastleMap)
            return {};
        if (line.type == Data::Models::PlayerSkeletonModel && actor.word376 != TalkingNpcClass)
            return {};
        return Say(actor, slot, line.text, context, line.address);
    }
    return {};
}

Core::Expected<void, std::string> SayTheNamedLine(const ActorState& actor, std::size_t slot,
                                                  const CharacterStepContext& context)
{
    if (!context.systemTime)
    {
        if (HasTheMessageName(actor))
            return Core::Unexpected(std::format("0x4fcd82: el nombre del lugar {} es el de 0x6c88b0 y no hay "
                                                "timeGetTime() para el reloj de 0x7437488",
                                                slot));
        return {};
    }
    WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.namedLineTime)
        runtime.namedLineTime = context.systemTime() - NamedLineHead;
    const std::uint32_t now = context.systemTime();
    if (now - *runtime.namedLineTime <= NamedLineGap || !HasTheMessageName(actor))
        return {};
    const bool first = runtime.namedLineFirst;
    if (auto said = Say(actor, slot, first ? FirstNamedText : SecondNamedText, context, 0x4fcdfb); !said.HasValue())
        return said;
    runtime.namedLineTime = now;
    runtime.namedLineFirst = !first;
    return {};
}

Core::Expected<void, std::string> FinishTheTurnedNpc(ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    if (context.playerSkeleton == nullptr)
        return Core::Unexpected(
            std::format("0x4fc8e4: el 0x20a del lugar {} no tiene las acciones de Player.bmd", slot));
    const Render::Models::Skeleton& skeleton = *context.playerSkeleton;
    const int choice = context.random.Next() % ChoiceRange;
    if (actor.animation.action != RestingAction)
    {
        Render::Models::SetAction(actor.animation, skeleton, RestingAction);
        return {};
    }
    const bool devias = context.world.Runtime().map == DeviasMap;
    for (const TurnedChoice& turned : TurnedChoices)
    {
        if (choice >= turned.below)
            continue;
        if (context.random.Next() % turned.oneIn == 0 && devias)
        {
            if (auto said = Say(actor, slot, turned.text, context, turned.address); !said.HasValue())
                return said;
        }
        Render::Models::SetAction(actor.animation, skeleton, turned.action);
        return {};
    }
    return {};
}
} // namespace Client::Scenes
