#include "Npc/NpcTalk.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Maps/MapId.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Chat/GameMessages.h"
#include "Movement/GameMovement.h"
#include "Hud/GamePanels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Quests/QuestState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Terrain/WorldTerrain.h"
#include "Assets/FaunaModels.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Npc
{
namespace
{
// 0x5674c8: el máximo de MouseUpdateTime (0x6c8dcc) al llegar.
constexpr std::int32_t ArrivalMouseUpdateTimeMax = 12;
// 0x56751b: los +0x376 de menos de 0xcd no son NPC que hablen.
constexpr std::uint16_t FirstTalkingNpc = 0xcd;
// 0x56760d..0x567616: el NPC 0xee no le habla a un héroe de menos de nivel 10 ([0x7681eb0] + 0xe).
constexpr std::uint16_t LevelLimitedNpc = 0xee;
constexpr std::int32_t LevelLimit = 10;
// 0x567618: el texto 663 de Text.bmd (0x76edad0), formato del sprintf con el 10, de tipo 1 (0x567631).
constexpr std::size_t LevelLimitText = 663;
// 0x567652..0x56766b: los NPC que ponen 0x78abb09 en 1.
constexpr std::uint16_t FirstFlaggedNpc = 0xf3;
constexpr std::uint16_t SecondFlaggedNpc = 0xf6;
constexpr std::uint16_t ThirdFlaggedNpc = 0xfb;
// 0x567aaf..0x567ac9: el NPC 0x101 con el bit 0x2000000 de +0x78 del héroe da el texto 1830 (0x7743264), de tipo 2.
constexpr std::uint16_t FlagLimitedNpc = 0x101;
constexpr std::uint32_t FlagLimitedHeroFlag = 0x2000000;
constexpr std::size_t FlagLimitText = 1830;
// 0x569586: el NPC que pone 0x6eeda8 en 1.
constexpr std::uint16_t QuestDialogNpc = 0xe5;
// 0x567b36..0x567b46 y 0x5688f0..0x5688fd: los modelos (+2, con signo) de los altares de Crywolf.
constexpr std::int16_t FirstCrywolfAltar = 0x204;
constexpr std::int16_t LastCrywolfAltar = 0x208;
// 0x4303c0: the map 34.
using Maps::CrywolfMap;

bool IsACrywolfAltar(const Kernel::ActorState& npc)
{
    const auto model = static_cast<std::int16_t>(npc.type);
    return model >= FirstCrywolfAltar && model <= LastCrywolfAltar;
}

// Un C3 de la llegada: el rand() del relleno (0x567788, 0x56923b) y el serial (0x5767913) por la tabla de
// protección, que se vuelve a cifrar con otro al cerrarse.
void DrawTheC3Randoms(Crt::CrtRandom& random)
{
    static_cast<void>(random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Models::LogicalAccess serial(serialDepth, random, Models::SerialCloseRandoms);
    }
}

// 0x567c2d.., 0x568288.., 0x568908.. y 0x568f54..: C1 05 30 claveH claveL, la clave (+0x1f4) del NPC con MOVSX.
Core::Expected<void, std::string> SendTheTalk(const Movement::HeroMovementContext& context,
                                              const Kernel::ActorState& npc, std::string_view where)
{
    if (!context.sendTalk)
        return Core::Unexpected(std::format("{}: falta el servicio del C3 30", where));
    DrawTheC3Randoms(context.random);
    context.sendTalk(std::bit_cast<std::int16_t>(npc.key));
    return {};
}

// 0x555190("", texto, tipo) con los nombres vacíos de la .bss (0x77f3bf0 y 0x77f3bf4, que nadie escribe).
Core::Expected<void, std::string> SayIt(const Movement::HeroMovementContext& context, std::string_view text,
                                        std::int32_t type, std::string_view where)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el aviso sin medidor", where));
    return Chat::AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, text, type);
}

Core::Expected<std::string_view, std::string> TextOf(const Movement::HeroMovementContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<Actors::ActorStopContext, std::string> StopContextForHero(const Kernel::ActorState& hero,
                                                                         const Movement::HeroMovementContext& context)
{
    static const Models::Skeleton EmptySkeleton;
    const bool player = hero.type == Tables::PlayerSkeletonModel;
    if (player && context.playerSkeleton == nullptr)
        return Core::Unexpected(std::string("0x5674e5: falta Player.bmd"));

    const Models::Skeleton* playerSkeleton =
        context.playerSkeleton != nullptr ? context.playerSkeleton : &EmptySkeleton;
    const Models::Skeleton* actorSkeleton = nullptr;
    const Assets::FaunaModels& faunaModels = Assets::FaunaModelsOrEmpty(context.faunaModels);
    if (!player)
    {
        actorSkeleton = faunaModels.SkeletonOf(Models::ModelId{hero.type});
        if (actorSkeleton == nullptr && context.models != nullptr)
            actorSkeleton = context.models->SkeletonOf(Models::ModelId{hero.type});
    }
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    return Actors::ActorStopContext{.runtime = context.world.Runtime(),
                                    .scene = Kernel::SceneId::Game,
                                    .playerSkeleton = *playerSkeleton,
                                    .items = context.items,
                                    .random = context.random,
                                    .terrainWalls = terrain.attributes ? &terrain.attributes->cells : nullptr,
                                    .sendAction = context.sendAction,
                                    .actorSkeleton = actorSkeleton,
                                    .modelSounds = faunaModels.SoundActions(hero.type)};
}
} // namespace

Core::Expected<void, std::string> TalkToTheNpc(Kernel::ActorState& hero, const Movement::HeroMovementContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    mouse.updateTimeMax = ArrivalMouseUpdateTimeMax;
    // 0x5674d2..0x5674ed: fuera de Crywolf activo (0x435a00 && 0x4303c0), 0x4f2250 y +0x378 en 0 (en la llegada de
    // MoveHero el héroe ya está parado: 0x4f2250 vuelve a elegir la acción y saca su sufijo de rand()).
    const bool crywolf = Events::Crywolf::InTheCrywolfDefense(runtime);
    if (!crywolf)
    {
        const auto stop = StopContextForHero(hero, context);
        if (!stop.HasValue())
            return Core::Unexpected(stop.Error());
        const auto stopped = Actors::SetActorStop(hero, stop.Value());
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        hero.movement = 0;
    }
    // 0x5674f4: sin NPC elegido, nada más (0x56ab0c).
    if (runtime.chosenNpc.index == -1)
        return {};
    if (runtime.chosenNpc.index < 0 ||
        static_cast<std::size_t>(runtime.chosenNpc.index) >= context.world.Actors().size())
        return Core::Unexpected(
            std::format("0x567502: el NPC elegido {} está fuera de la tabla de personajes", runtime.chosenNpc.index));
    const Kernel::ActorState& npc = context.world.Actors()[static_cast<std::size_t>(runtime.chosenNpc.index)];
    // 0x56751b: un +0x376 de menos de 0xcd va derecho a 0x569599 (0x6c8e54 en −1, sin tocar 0x6eeda8).
    if (npc.word376 < FirstTalkingNpc)
    {
        runtime.chosenNpc.index = -1;
        return {};
    }
    // 0x567521..0x5675ed: el nivel (+0xe, sin signo) del registro propio, abierto y cerrado por la tabla.
    std::int32_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    // 0x567618..0x567651: el aviso y la salida, sin poner 0x6c8e54 en −1.
    if (npc.word376 == LevelLimitedNpc && level < LevelLimit)
    {
        auto format = TextOf(context, LevelLimitText, "0x567618");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        return SayIt(context, Crt::Sprintf(format.Value(), LevelLimit), Ui::SystemMessage, "0x567639");
    }
    const std::uint16_t word = npc.word376;
    runtime.panels.field78abb09 =
        (word == FirstFlaggedNpc || word == SecondFlaggedNpc || word == ThirdFlaggedNpc) ? 1 : 0;
    runtime.panels.repairMode = 0;
    // 0x567672..0x567697: la ventana de las misiones (el panel 8) se cierra con 0x65cae0(8, 0), que manda el C1 03 31.
    {
        Hud::GamePanels panels(context.world, context.random);
        panels.LendTheWindowClose(context.sendWindowClose);
        if (panels.Registry().IsOpen(Ui::QuestPanel))
        {
            if (auto closed = panels.Registry().Close(Ui::QuestPanel); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
        }
    }
    // 0x56769c..0x567a85: el C3 A0 si las misiones todavía no tienen la clase ([0x710148] + 4 en 0xff).
    if (runtime.quest.characterClass == Quests::QuestClassUnknown)
    {
        if (!context.requestQuestList)
            return Core::Unexpected(std::string("0x5676b0: falta el servicio del C3 A0"));
        DrawTheC3Randoms(context.random);
        context.requestQuestList();
    }
    // 0x567a9b..0x567aff: el 0x101 con el bit 0x2000000 del héroe ([0x743a170] + 0x78, el mismo que llega).
    if (word == FlagLimitedNpc && (hero.flags78 & FlagLimitedHeroFlag) == FlagLimitedHeroFlag)
    {
        auto text = TextOf(context, FlagLimitText, "0x567ad3");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto said = SayIt(context, text.Value(), Ui::ErrorMessage, "0x567add"); !said.HasValue())
            return said;
        runtime.chosenNpc.index = -1;
        return {};
    }
    if (crywolf)
    {
        // 0x567b00..0x567c23 (CL9 hito 11): el clic en un altar. Con una elfa, 0x4365a0 y 0x435b00 muestran el estado
        // del altar y siguen en 0x569573, sin el C1 30; sin elfa, el C1 30 de 0x567c2d; otro blanco, el de 0x568288.
        const auto click = Events::Crywolf::ClickTheCrywolfAltar(runtime, context.texts, hero, npc);
        if (!click.HasValue())
            return Core::Unexpected(click.Error());
        if (click.Value() != Events::Crywolf::AltarClick::Contract)
        {
            const bool altar = click.Value() == Events::Crywolf::AltarClick::Talk;
            if (auto sent = SendTheTalk(context, npc, altar ? "0x567c2d" : "0x568288"); !sent.HasValue())
                return sent;
        }
    }
    else if (runtime.map == CrywolfMap)
    {
        // 0x5688ca..0x5688fd: en el 34 sin el evento, un altar no habla.
        if (!IsACrywolfAltar(npc))
        {
            if (auto sent = SendTheTalk(context, npc, "0x568908"); !sent.HasValue())
                return sent;
        }
    }
    else if (auto sent = SendTheTalk(context, npc, "0x568f54"); !sent.HasValue())
        return sent;
    // 0x569573..0x56959b.
    runtime.byte6eeda8 = npc.word376 == QuestDialogNpc ? 1 : 0;
    runtime.chosenNpc.index = -1;
    return {};
}
} // namespace Client::Npc
