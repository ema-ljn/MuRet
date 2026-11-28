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
// 0x5674c8: the maximum of MouseUpdateTime (0x6c8dcc) on arrival.
constexpr std::int32_t ArrivalMouseUpdateTimeMax = 12;
// 0x56751b: a +0x376 below 0xcd is not an NPC that talks.
constexpr std::uint16_t FirstTalkingNpc = 0xcd;
// 0x56760d..0x567616: NPC 0xee does not talk to a hero below level 10 ([0x7681eb0] + 0xe).
constexpr std::uint16_t LevelLimitedNpc = 0xee;
constexpr std::int32_t LevelLimit = 10;
// 0x567618: Text.bmd text 663 (0x76edad0), the sprintf format with the 10, of type 1 (0x567631).
constexpr std::size_t LevelLimitText = 663;
// 0x567652..0x56766b: the NPCs that set 0x78abb09 to 1.
constexpr std::uint16_t FirstFlaggedNpc = 0xf3;
constexpr std::uint16_t SecondFlaggedNpc = 0xf6;
constexpr std::uint16_t ThirdFlaggedNpc = 0xfb;
// 0x567aaf..0x567ac9: NPC 0x101 with bit 0x2000000 of the hero's +0x78 gives text 1830 (0x7743264), of type 2.
constexpr std::uint16_t FlagLimitedNpc = 0x101;
constexpr std::uint32_t FlagLimitedHeroFlag = 0x2000000;
constexpr std::size_t FlagLimitText = 1830;
// 0x569586: the NPC that sets 0x6eeda8 to 1.
constexpr std::uint16_t QuestDialogNpc = 0xe5;
// 0x567b36..0x567b46 and 0x5688f0..0x5688fd: the models (+2, signed) of the Crywolf altars.
constexpr std::int16_t FirstCrywolfAltar = 0x204;
constexpr std::int16_t LastCrywolfAltar = 0x208;
// 0x4303c0: the map 34.
using Maps::CrywolfMap;

bool IsACrywolfAltar(const Kernel::ActorState& npc)
{
    const auto model = static_cast<std::int16_t>(npc.type);
    return model >= FirstCrywolfAltar && model <= LastCrywolfAltar;
}

// An arrival C3: the padding rand() (0x567788, 0x56923b) and the serial (0x5767913) through the protection table,
// which is encrypted again with another on closing.
void DrawTheC3Randoms(Crt::CrtRandom& random)
{
    static_cast<void>(random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Models::LogicalAccess serial(serialDepth, random, Models::SerialCloseRandoms);
    }
}

// 0x567c2d.., 0x568288.., 0x568908.. and 0x568f54..: C1 05 30 keyH keyL, the NPC's key (+0x1f4) with MOVSX.
Core::Expected<void, std::string> SendTheTalk(const Movement::HeroMovementContext& context,
                                              const Kernel::ActorState& npc, std::string_view where)
{
    if (!context.sendTalk)
        return Core::Unexpected(std::format("{}: falta el servicio del C3 30", where));
    DrawTheC3Randoms(context.random);
    context.sendTalk(std::bit_cast<std::int16_t>(npc.key));
    return {};
}

// 0x555190("", text, type) with the empty .bss names (0x77f3bf0 and 0x77f3bf4, which nobody writes).
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
    // 0x5674d2..0x5674ed: outside an active Crywolf (0x435a00 && 0x4303c0), 0x4f2250 and +0x378 at 0 (on MoveHero's
    // arrival the hero is already stopped: 0x4f2250 picks the action again and spends its rand() suffix).
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
    // 0x5674f4: with no chosen NPC, nothing more (0x56ab0c).
    if (runtime.chosenNpc.index == -1)
        return {};
    if (runtime.chosenNpc.index < 0 ||
        static_cast<std::size_t>(runtime.chosenNpc.index) >= context.world.Actors().size())
        return Core::Unexpected(
            std::format("0x567502: el NPC elegido {} está fuera de la tabla de personajes", runtime.chosenNpc.index));
    const Kernel::ActorState& npc = context.world.Actors()[static_cast<std::size_t>(runtime.chosenNpc.index)];
    // 0x56751b: a +0x376 below 0xcd goes straight to 0x569599 (0x6c8e54 at −1, without touching 0x6eeda8).
    if (npc.word376 < FirstTalkingNpc)
    {
        runtime.chosenNpc.index = -1;
        return {};
    }
    // 0x567521..0x5675ed: the level (+0xe, unsigned) of the own record, opened and closed through the table.
    std::int32_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    // 0x567618..0x567651: the warning and the exit, without setting 0x6c8e54 to −1.
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
    // 0x567672..0x567697: the quest window (panel 8) closes with 0x65cae0(8, 0), which sends the C1 03 31.
    {
        Hud::GamePanels panels(context.world, context.random);
        panels.LendTheWindowClose(context.sendWindowClose);
        if (panels.Registry().IsOpen(Ui::QuestPanel))
        {
            if (auto closed = panels.Registry().Close(Ui::QuestPanel); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
        }
    }
    // 0x56769c..0x567a85: the C3 A0 if the quests do not have the class yet ([0x710148] + 4 at 0xff).
    if (runtime.quest.characterClass == Quests::QuestClassUnknown)
    {
        if (!context.requestQuestList)
            return Core::Unexpected(std::string("0x5676b0: falta el servicio del C3 A0"));
        DrawTheC3Randoms(context.random);
        context.requestQuestList();
    }
    // 0x567a9b..0x567aff: the 0x101 with the hero's bit 0x2000000 ([0x743a170] + 0x78, the same one that arrives).
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
        // 0x567b00..0x567c23 (CL9 milestone 11): the click on an altar. With an elf, 0x4365a0 and 0x435b00 show the
        // altar's state and go on at 0x569573, without the C1 30; without an elf, the C1 30 of 0x567c2d; another
        // target, 0x568288's.
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
        // 0x5688ca..0x5688fd: on map 34 without the event, an altar does not talk.
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
