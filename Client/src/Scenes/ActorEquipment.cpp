#include "Scenes/ActorEquipment.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/WorldRuntime.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// 0x512a30, cotejado con el PE c32bbd5f (493 instrucciones, 1.733 bytes), con 0x4053f0 (44, 139),
// 0x4053a0 (77 bytes) y 0x512930 (56, 205): 06, "El equipo propio en el personaje".
namespace Client::Scenes
{
namespace
{
// Los lugares del registro propio que lee, de a 0x48 desde el tipo en +0xb6c. Del 9 al 11 no lee nada.
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t FirstBodySlot = 2;
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;

// El nivel de una parte: (B1 >> 3) & 0xf del ítem (0x512b4c..0x512b52).
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// Con estos, 0x512a30 no llama a 0x4f2250 (0x512ba1..0x512bcd): los mapas 18 a 23, las acciones 0xc5 a
// 0xcc y las de 0x22 a 0x97.
constexpr std::int32_t FirstNoStopMap = 18;
constexpr std::int32_t LastNoStopMap = 23;
constexpr std::uint8_t FirstNoStopAction = 0xc5;
constexpr std::uint8_t LastNoStopAction = 0xcc;
constexpr std::uint8_t FirstBusyAction = 0x22;
constexpr std::uint8_t LastBusyAction = 0x97;

// La mascota que 0x511290 mira en el word318 anterior.
constexpr std::uint16_t HeightOffsetPet = 0x1c15;

// Las armas que 0x4053f0 pone en los mapas 18 a 23 según +0x1d4 & 7 (0x40540c..0x405472), con su
// nombre en Item.bmd: el 0 16 (Sword of Destruction) en las dos manos de las clases 1, 3 y 4, el 4 19
// (Great Reign Crossbow) en la derecha de la 2 y el 5 5 (Legendary Staff) en la derecha de la 0.
constexpr std::uint8_t ClassBits = 7;
constexpr std::uint8_t DarkWizardClass = 0;
constexpr std::uint8_t DarkKnightClass = 1;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t MagicGladiatorClass = 3;
constexpr std::uint8_t DarkLordClass = 4;
constexpr std::uint16_t EventSwordModel = Data::Models::ItemModel(0, 16);
constexpr std::uint16_t EventCrossbowModel = Data::Models::ItemModel(4, 19);
constexpr std::uint16_t EventStaffModel = Data::Models::ItemModel(5, 5);
static_assert(EventSwordModel == 0x222 && EventCrossbowModel == 0xa25 && EventStaffModel == 0xc17);

bool InNoStopMaps(std::int32_t map)
{
    return map >= FirstNoStopMap && map <= LastNoStopMap;
}

std::uint16_t ModelOf(const World::ItemState& item, std::uint16_t empty)
{
    if (item.id == NoItemModel)
    {
        return empty;
    }
    return static_cast<std::uint16_t>(item.id + Data::Models::FirstItemModel);
}

std::uint8_t LevelOf(const World::ItemState& item)
{
    return static_cast<std::uint8_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

// 0x512abf..0x512b9b: las manos con su byte +0x1b; las alas y la mascota, sin bytes.
void ProjectCarried(ActorState& actor, const OwnCharacterRecord& own)
{
    static_assert(LeftHandSlot == RightHandSlot + 1);
    for (std::size_t side = 0; side < actor.hands.size(); ++side)
    {
        const World::ItemState& item = own.equipment[RightHandSlot + side];
        ActorItemPart& hand = actor.hands[side];
        hand.model = ModelOf(item, NoItemModel);
        hand.level = LevelOf(item);
        hand.flags1b = item.flags1b;
    }
    actor.wings.model = ModelOf(own.equipment[WingsSlot], NoItemModel);
    actor.wings.level = LevelOf(own.equipment[WingsSlot]);
    actor.pet.model = ModelOf(own.equipment[PetSlot], NoItemModel);
    actor.pet.level = LevelOf(own.equipment[PetSlot]);
    actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
}

// 0x512bb3..0x512bcd.
bool CallsStop(const ActorState& actor, std::int32_t map)
{
    const std::uint8_t action = actor.animation.action;
    const bool skipped = action >= FirstNoStopAction && action <= LastNoStopAction;
    const bool busy = action >= FirstBusyAction && action <= LastBusyAction;
    return !InNoStopMaps(map) && !skipped && !busy;
}

// 0x512cc7..0x512ec9: las cinco partes; un lugar vacío deja la de la clase.
void ProjectBody(ActorState& actor, const OwnCharacterRecord& own)
{
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const World::ItemState& item = own.equipment[FirstBodySlot + part];
        const auto ofClass = GameLogic::ClassPartModel(static_cast<GameLogic::BodyPart>(part), actor.characterClass);
        ActorItemPart& worn = actor.body[part];
        worn.model = ModelOf(item, ofClass);
        worn.level = LevelOf(item);
        worn.flags1b = item.flags1b;
        worn.flags1c = item.flags1c;
    }
}

// 0x4053a0: las manos, las alas y la mascota sin ítem y con el nivel en 0, y el byte +0x1b de las manos
// en 0. Los de las alas y la mascota quedan.
void ClearCarried(ActorState& actor)
{
    for (ActorItemPart& hand : actor.hands)
    {
        hand.model = NoItemModel;
        hand.level = 0;
        hand.flags1b = 0;
    }
    actor.wings.model = NoItemModel;
    actor.wings.level = 0;
    actor.pet.model = NoItemModel;
    actor.pet.level = 0;
    actor.previousHad1c15 = false;
}

// 0x512cc7..0x512ef5: la ropa y lo que sigue.
Core::Expected<void, std::string> Dress(ActorState& actor, const ActorEquipmentContext& context)
{
    ProjectBody(actor, context.own);
    ArmForTheEventMaps(actor, context.stop.runtime.map);
    ApplyHeadAndScale(actor);
    // 0x512eda..0x512eec: lo que devuelve no lo lee nadie. Con otro personaje, 0x550990 lee lo que dejó la
    // última llamada.
    if (&actor == &context.publishedHero)
    {
        context.fullSetScan =
            ScanFullSet(ScannedCharacterOf(actor), context.own, context.ownRecordAccesses, context.stop.random).globals;
    }
    return RecalculateOwnStats(context.own, {.itemSetTables = context.itemSetTables,
                                             .itemSets = context.itemSets,
                                             .items = context.stop.items,
                                             .publishedHero = context.publishedHero,
                                             .ownRecordAccesses = context.ownRecordAccesses,
                                             .random = context.stop.random,
                                             .held = context.held,
                                             .scan505820 = context.fullSetScan});
}
} // namespace

void ArmForTheEventMaps(ActorState& actor, std::int32_t map)
{
    if (!InNoStopMaps(map))
    {
        return;
    }
    ClearCarried(actor);
    ActorItemPart& right = actor.hands[0];
    switch (actor.characterClass & ClassBits)
    {
    case DarkWizardClass:
        right.model = EventStaffModel;
        break;
    case ElfClass:
        right.model = EventCrossbowModel;
        break;
    case DarkKnightClass:
    case MagicGladiatorClass:
    case DarkLordClass:
        right.model = EventSwordModel;
        actor.hands[1].model = EventSwordModel;
        break;
    default:
        break;
    }
}

void ApplyHeadAndScale(ActorState& actor)
{
    if (actor.byte3e3 != 0)
    {
        return;
    }
    const auto helm = actor.body[static_cast<std::size_t>(GameLogic::BodyPart::Helm)].model;
    actor.head = GameLogic::HeadUnder(helm, actor.characterClass).value_or(NoItemModel);
    if (const std::optional<float> scale = GameLogic::ClassScale(actor.characterClass, actor.skin))
    {
        actor.scale = *scale;
    }
}

Core::Expected<ActorStopResult, std::string> ApplyActorEquipment(ActorState& actor,
                                                                 const ActorEquipmentContext& context)
{
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        return ActorStopResult{};
    }
    // 0x512a48..0x512aba lo abre; el cierre, en 0x512bea..0x512cc6 o en línea en 0x512efa..0x5130cf,
    // es el de este acceso, también cuando MuRet corta antes.
    const Render::Models::LogicalAccess ownAccess(context.ownRecordAccesses, context.stop.random,
                                                  Render::Models::OwnRecordCloseRandoms);
    ProjectCarried(actor, context.own);
    ActorStopResult stop;
    if (CallsStop(actor, context.stop.runtime.map))
    {
        auto stopped = SetActorStop(actor, context.stop);
        if (!stopped.HasValue())
        {
            return Core::Unexpected(stopped.Error());
        }
        stop = stopped.Value();
    }
    if (actor.byte3e3 != 0)
    {
        return stop;
    }
    const auto dressed = Dress(actor, context);
    if (!dressed.HasValue())
    {
        return Core::Unexpected(dressed.Error());
    }
    return stop;
}
} // namespace Client::Scenes
