#include "Actors/ActorActions.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Tables/PlayerActions.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/CharacterClass.h"
#include "Maps/MapId.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Math/Ftol.h"
#include "Pathfinding/TerrainWalls.h"
#include "Audio/SoundId.h"
#include "Actors/MovementSpeed.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// 0x4f2250, checked against PE c32bbd5f (1,607 instructions, 5,481 bytes): 06, "Caminar".
namespace Client::Actors
{
namespace
{
// What changes the action on the pet (+0x318) and in the left hand (+0x2d8).
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::array<std::uint16_t, 2> RidingPets = {0x1c14, 0x1c15};
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::uint16_t WallCheckedModel = 0x172;

// The standing actions (P-C14 c). The names say what picks them in 0x4f2250, not how they look.
constexpr std::uint8_t UnarmedStand = 1;
constexpr std::uint8_t UnarmedElfStand = 2;
constexpr std::uint8_t ArmedStand = 3;
constexpr std::uint8_t MarkedGroup0To2Stand = 4;
constexpr std::uint8_t Model813Stand = 5;
constexpr std::uint8_t MarkedGroup3Or5Stand = 6;
constexpr std::uint8_t LeftGroup4Stand = 7;
constexpr std::uint8_t RightGroup4Stand = 8;
constexpr std::uint8_t FlyingStand = 9;
constexpr std::uint8_t FlyingRightGroup4Stand = 10;
constexpr std::uint8_t RidingUnarmedStand = 0xb;
constexpr std::uint8_t RidingArmedStand = 0xc;
constexpr std::uint8_t RavenInSafeZoneStand = 0x48;
constexpr std::uint8_t DarkHorseStand = 0x4a;
constexpr std::uint8_t FenrirUnarmedStand = 0x76;
constexpr std::uint8_t FenrirBothHandsStand = 0x77;
constexpr std::uint8_t FenrirRightHandStand = 0x78;
constexpr std::uint8_t FenrirLeftHandStand = 0x79;
constexpr std::uint8_t MarkedSpecialStand = 0x8a;
using Tables::SittingAction;
constexpr std::uint8_t DefaultModelStandAction = 0;
constexpr std::uint8_t WallStandAction = 7;

// +0x7c with this bit: sitting.
constexpr std::uint32_t SittingFlag = 0x200000;
// +0x376 with this value: flying.
constexpr std::uint16_t FlyingWord376 = 0x101;

// The flying maps of 0x4f23ad..0x4f23bf (Atlans and Kalima): there 0x4f2250 reads the character's cell in the
// TerrainWall (0x7d0d348) in scene 5.
using Maps::AtlansMap;
using Maps::FirstKalimaMap;
using Maps::LastKalimaMap;
using Maps::SeventhKalimaMap;

// In the safe zone of maps 11 to 23 the weapon's action is chosen; outside that range, the unarmed one
// (0x4f2c16..0x4f2c1e).
constexpr std::int32_t FirstArmedSafeMap = 11;
constexpr std::int32_t LastArmedSafeMap = 23;

// 0x4f2c4c: byte +0x1e of the weapon's Item.bmd record (Kernel::ItemState::byte0a).
constexpr std::size_t WeaponMarkOffset = 0x1e;
// Those of groups 0 to 2 that, with the mark, go to 0x8a.
constexpr std::array<int, 3> SpecialMarkedModels = {0x227, 0x229, 0x231};
// 0x813 and 0x814, of group 3, go to 5 before the rest of the group.
constexpr std::array<int, 2> Model813Pair = {0x813, 0x814};

// 0x41ed10 with Fenrir: 0x279 plus rand() & 1, with rand() % 3 at 0 first (0x4f3704..0x4f372c).
constexpr std::int32_t FenrirSoundBase = 0x279;
constexpr int FenrirSoundChance = 3;
constexpr int FenrirSoundVariants = 2;
// The suffix: rand() % 16 other than 0 ends there (0x4f36be..0x4f36cf).
constexpr int SuffixChance = 16;
// The subtypes that go to the model's sound table even though they are players (0x4f36e2..0x4f36f3).
constexpr std::int32_t FirstModelSoundSubtype = 209;
constexpr std::int32_t LastModelSoundSubtype = 211;
constexpr std::int32_t ModelSoundBase = 0xd2;
constexpr std::int32_t ModelSoundSubtypeNineOffset = 5;

// 0x4f2250 compares the models as a signed WORD.
int Signed(std::uint16_t model)
{
    return static_cast<std::int16_t>(model);
}

// The first model of an item group (Tables::ItemModel).
int GroupStart(int group)
{
    return Tables::FirstItemModel + group * Tables::ItemModelsPerGroup;
}

bool InGroups(int model, int firstGroup, int lastGroup)
{
    return model >= GroupStart(firstGroup) && model < GroupStart(lastGroup + 1);
}

bool Contains(std::span<const int> models, int model)
{
    return std::ranges::find(models, model) != models.end();
}

// On the right, indices 8 to 14, 16 and from 18 on of group 4 (0x4f2d2f..0x4f2d51; MuMain's crossbows).
bool IsRightGroup4(int model)
{
    return (model >= 0xa1a && model < 0xa21) || model == 0xa22 || (model >= 0xa24 && model < 0xc12);
}

// On the left, indices 0 to 6, 17, 20 and 21 of group 4 (0x4f2d05..0x4f2d2d; MuMain's bows).
bool IsLeftGroup4(int model)
{
    return (model >= 0xa12 && model < 0xa19) || model == 0xa23 || model == 0xa26 || model == 0xa27;
}

bool IsFlyingMap(std::int32_t map)
{
    return map == AtlansMap || (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

std::uint8_t UnarmedFor(const Kernel::ActorState& actor)
{
    return Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf ? UnarmedElfStand : UnarmedStand;
}

// The mark of a right-hand weapon: byte +0x1e of its Item.bmd record.
Core::Expected<bool, std::string> IsMarked(int model, const Tables::ItemTable* items)
{
    if (items == nullptr)
    {
        return Core::Unexpected(std::string("0x4f2250: falta Item.bmd para leer la marca +0x1e del arma"));
    }
    const auto record = items->Record(static_cast<std::size_t>(model - Tables::FirstItemModel));
    if (!record)
    {
        return Core::Unexpected(std::string("0x4f2250: Item.bmd no tiene el registro del arma de la derecha"));
    }
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

// That of a right-hand weapon with the mark read: without it, 3; with it, `marked`.
Core::Expected<std::uint8_t, std::string> ByMark(int right, const Tables::ItemTable* items, std::uint8_t marked)
{
    const auto isMarked = IsMarked(right, items);
    if (!isMarked.HasValue())
    {
        return Core::Unexpected(isMarked.Error());
    }
    return isMarked.Value() ? marked : ArmedStand;
}

// 0x4f2bf6..0x4f2d82: with the feet on the ground, by the weapons.
Core::Expected<std::uint8_t, std::string> GroundStand(const Kernel::ActorState& actor, std::int32_t map,
                                                      const Tables::ItemTable* items)
{
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool unarmed = actor.hands[0].model == Kernel::NoItemModel && actor.hands[1].model == Kernel::NoItemModel;
    const bool unarmedSafeMap = map < FirstArmedSafeMap || map > LastArmedSafeMap;
    if (unarmed || (actor.safeZone != 0 && unarmedSafeMap))
    {
        return UnarmedFor(actor);
    }
    if (InGroups(right, 0, 2))
    {
        return ByMark(right, items, Contains(SpecialMarkedModels, right) ? MarkedSpecialStand : MarkedGroup0To2Stand);
    }
    if (Contains(Model813Pair, right))
    {
        return Model813Stand;
    }
    if (InGroups(right, 3, 3) || InGroups(right, 5, 5))
    {
        return ByMark(right, items, MarkedGroup3Or5Stand);
    }
    if (IsLeftGroup4(left))
    {
        return LeftGroup4Stand;
    }
    return IsRightGroup4(right) ? RightGroup4Stand : UnarmedFor(actor);
}

// 0x4f228c..0x4f22f6: Fenrir outside the safe zone, by both hands.
std::uint8_t FenrirStand(const Kernel::ActorState& actor)
{
    const bool right = actor.hands[0].model != Kernel::NoItemModel;
    const bool left = actor.hands[1].model != Kernel::NoItemModel;
    if (right)
    {
        return left ? FenrirBothHandsStand : FenrirRightHandStand;
    }
    return left ? FenrirLeftHandStand : FenrirUnarmedStand;
}

// 0x4f2365..0x4f23dd: it flies with wings outside the safe zone or with +0x376 at 0x101. On the flying maps of
// scene 5 the TerrainWall cell decides too, which MuRet does not have in the game.
Core::Expected<bool, std::string> IsFlying(const Kernel::ActorState& actor, const ActorStopContext& context)
{
    bool flyingCell = false;
    if (context.scene == Kernel::SceneId::Game && IsFlyingMap(context.runtime.map))
    {
        if (context.terrainWalls == nullptr)
        {
            return Core::Unexpected(std::string("0x4f2250: en los mapas de vuelo de la escena 5 falta la celda del "
                                                "personaje en el TerrainWall vivo (0x7d0d348, 0x4f23c1)"));
        }
        constexpr double CellsPerWorldUnit = static_cast<double>(0.01f);
        const auto y = Math::OriginalFtolLow(static_cast<double>(actor.position.y) * CellsPerWorldUnit);
        const auto x = Math::OriginalFtolLow(static_cast<double>(actor.position.x) * CellsPerWorldUnit);
        if (!x || !y)
        {
            return Core::Unexpected(std::string("0x4f23c1: la posición no es representable como celda"));
        }
        const auto cell = Data::Terrain::CellIndex(static_cast<std::uint8_t>(*x), static_cast<std::uint8_t>(*y));
        flyingCell = ((*context.terrainWalls)[cell] & Pathfinding::SafeZoneWallBit) == 0;
    }
    const bool withWings = actor.safeZone == 0 && actor.wings.model != Kernel::NoItemModel;
    return withWings || actor.word376 == FlyingWord376 || flyingCell;
}

// The standing action, or std::nullopt when it is already sitting and none has to be set.
Core::Expected<std::optional<std::uint8_t>, std::string> ChooseStand(Kernel::ActorState& actor,
                                                                     const ActorStopContext& context)
{
    const bool safe = actor.safeZone != 0;
    const std::uint16_t pet = actor.pet.model;
    if (pet == Fenrir && !safe)
    {
        context.runtime.movementSpeed.itemLatch = 0;
        return std::optional{FenrirStand(actor)};
    }
    if (pet == DarkHorse && !safe)
    {
        return std::optional{DarkHorseStand};
    }
    if (safe && actor.hands[1].model == Raven)
    {
        return std::optional{RavenInSafeZoneStand};
    }
    if (std::ranges::find(RidingPets, pet) != RidingPets.end() && !safe)
    {
        const bool unarmed = actor.hands[0].model == Kernel::NoItemModel && actor.hands[1].model == Kernel::NoItemModel;
        return std::optional{unarmed ? RidingUnarmedStand : RidingArmedStand};
    }
    const auto flying = IsFlying(actor, context);
    if (!flying.HasValue())
    {
        return Core::Unexpected(flying.Error());
    }
    if ((actor.flags7c & SittingFlag) == SittingFlag)
    {
        if (actor.animation.action == SittingAction)
        {
            return std::optional<std::uint8_t>{};
        }
        Models::SetAction(actor.animation, context.playerSkeleton, SittingAction);
        // 0x4f2416..0x4f2bea (0x4f2834..0x4f2bea on the ground): C1 05 18 with the direction of the character's angle
        // (+0x24, not the hero's) and 0x82, and the suffix (0x4f36be), even if the send fails.
        auto sent = SendTheAction(actor.angles.z, Protocol::Movement::SittingAction, context.sendAction, "0x4f2416");
        if (!sent.HasValue())
        {
            return Core::Unexpected(sent.Error());
        }
        return std::optional<std::uint8_t>{};
    }
    if (flying.Value())
    {
        return std::optional{IsRightGroup4(Signed(actor.hands[0].model)) ? FlyingRightGroup4Stand : FlyingStand};
    }
    const auto ground = GroundStand(actor, context.runtime.map, context.items);
    if (!ground.HasValue())
    {
        return Core::Unexpected(ground.Error());
    }
    return std::optional{ground.Value()};
}

// 0x4f36be..0x4f37a5: the suffix, with the action already set.
Core::Expected<ActorStopResult, std::string> Suffix(const Kernel::ActorState& actor, const ActorStopContext& context)
{
    Crt::CrtRandom& random = context.random;
    if (random.Next() % SuffixChance != 0)
    {
        return ActorStopResult{};
    }
    const bool modelSoundSubtype = actor.subtype >= FirstModelSoundSubtype && actor.subtype <= LastModelSoundSubtype;
    if (actor.type != Tables::PlayerSkeletonModel || modelSoundSubtype)
    {
        if (context.modelSounds.empty())
        {
            return Core::Unexpected(std::string("0x4f2250: falta la tabla de sonidos del modelo "
                                                "(0x5769ad8 + tipo · 0xe0 + 0xaa)"));
        }
        if (context.modelSounds.front() == -1)
        {
            return ActorStopResult{};
        }
        const std::size_t variant = static_cast<std::size_t>(random.Next() & 1);
        if (variant >= context.modelSounds.size())
        {
            return Core::Unexpected(std::string("0x4f2250: la tabla de sonidos del modelo no contiene los dos "
                                                "WORD del sufijo (0x5769ad8 + tipo · 0xe0 + 0xaa)"));
        }
        const std::int32_t subtypeOffset = actor.subtype == 9 ? ModelSoundSubtypeNineOffset : 0;
        const std::int32_t sound = ModelSoundBase + context.modelSounds[variant] + subtypeOffset;
        context.runtime.RequestSound(Audio::SoundId{sound}, actor.position);
        return ActorStopResult{.sound = Audio::SoundId{sound}};
    }
    if (actor.pet.model != Fenrir || random.Next() % FenrirSoundChance != 0)
    {
        return ActorStopResult{};
    }
    const std::int32_t sound = FenrirSoundBase + random.Next() % FenrirSoundVariants;
    context.runtime.RequestSound(Audio::SoundId{sound}, actor.position);
    return ActorStopResult{.sound = Audio::SoundId{sound}};
}
} // namespace

Core::Expected<ActorStopResult, std::string> SetActorStop(Kernel::ActorState& actor, const ActorStopContext& context)
{
    actor.run = 0;
    if (actor.type == Tables::PlayerSkeletonModel)
    {
        const auto stand = ChooseStand(actor, context);
        if (!stand.HasValue())
        {
            return Core::Unexpected(stand.Error());
        }
        if (stand.Value())
        {
            Models::SetAction(actor.animation, context.playerSkeleton, *stand.Value());
        }
        return Suffix(actor, context);
    }

    std::uint8_t cellY = 0;
    {
        const Models::LogicalAccess access(actor.cellYAccesses, context.random, Models::SmallLogicalCloseRandoms);
        cellY = actor.cellY;
    }
    std::uint8_t cellX = 0;
    {
        const Models::LogicalAccess access(actor.cellXAccesses, context.random, Models::SmallLogicalCloseRandoms);
        cellX = actor.cellX;
    }

    std::uint8_t stand = DefaultModelStandAction;
    if (actor.type == WallCheckedModel)
    {
        if (context.terrainWalls == nullptr)
        {
            return Core::Unexpected(std::string("0x4f2250: falta TerrainWall para el tipo 0x172 en la celda lógica "
                                                "(0x5f82a0, 0x4f366e)"));
        }
        const std::size_t cell = Data::Terrain::CellIndex(cellX, cellY);
        if (((*context.terrainWalls)[cell] & Pathfinding::SafeZoneWallBit) != 0)
        {
            stand = WallStandAction;
        }
    }
    if (context.actorSkeleton != nullptr)
    {
        Models::SetAction(actor.animation, *context.actorSkeleton, stand);
    }
    return Suffix(actor, context);
}
} // namespace Client::Actors
