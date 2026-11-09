#include "Scenes/ActorActions.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// 0x4f2250, cotejado con el PE c32bbd5f (1.607 instrucciones, 5.481 bytes): 06, "Caminar".
namespace Client::Scenes
{
namespace
{
// Lo que cambia la acción en la mascota (+0x318) y en la mano izquierda (+0x2d8).
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::array<std::uint16_t, 2> RidingPets = {0x1c14, 0x1c15};
constexpr std::uint16_t Raven = 0x1c17;

// Las acciones de parado (P-C14 c). Los nombres dicen qué las elige en 0x4f2250, no cómo se ven.
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
constexpr std::uint8_t SittingAction = 0xca;

constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t ClassMask = 7;

// +0x7c con este bit: sentado.
constexpr std::uint32_t SittingFlag = 0x200000;
// +0x376 con este valor: vuela.
constexpr std::uint16_t FlyingWord376 = 0x101;

// Los mapas de vuelo de 0x4f23ad..0x4f23bf (Atlans y Kalima): ahí 0x4f2250 lee la celda del personaje
// en el TerrainWall (0x7d0d348) en la escena 5.
constexpr std::int32_t AtlansMap = 7;
constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;

// En la zona segura de los mapas 11 a 23 se elige la acción del arma; fuera de ese rango, la de sin
// armas (0x4f2c16..0x4f2c1e).
constexpr std::int32_t FirstArmedSafeMap = 11;
constexpr std::int32_t LastArmedSafeMap = 23;

// 0x4f2c4c: el byte +0x1e del registro del arma en Item.bmd (World::ItemState::byte0a).
constexpr std::size_t WeaponMarkOffset = 0x1e;
// Los de los grupos 0 a 2 que, con la marca, van a la 0x8a.
constexpr std::array<int, 3> SpecialMarkedModels = {0x227, 0x229, 0x231};
// 0x813 y 0x814, del grupo 3, van a la 5 antes que el resto del grupo.
constexpr std::array<int, 2> Model813Pair = {0x813, 0x814};

// 0x41ed10 con Fenrir: 0x279 más rand() & 1, con rand() % 3 en 0 antes (0x4f3704..0x4f372c).
constexpr std::int32_t FenrirSoundBase = 0x279;
constexpr int FenrirSoundChance = 3;
constexpr int FenrirSoundVariants = 2;
// El sufijo: rand() % 16 distinto de 0 termina ahí (0x4f36be..0x4f36cf).
constexpr int SuffixChance = 16;
// Los subtipos que van a la tabla de sonidos del modelo aunque sean jugadores (0x4f36e2..0x4f36f3).
constexpr std::int32_t FirstModelSoundSubtype = 209;
constexpr std::int32_t LastModelSoundSubtype = 211;

// 0x4f2250 compara los modelos como WORD con signo.
int Signed(std::uint16_t model)
{
    return static_cast<std::int16_t>(model);
}

// El primer modelo de un grupo de ítems (Data::Models::ItemModel).
int GroupStart(int group)
{
    return Data::Models::FirstItemModel + group * Data::Models::ItemModelsPerGroup;
}

bool InGroups(int model, int firstGroup, int lastGroup)
{
    return model >= GroupStart(firstGroup) && model < GroupStart(lastGroup + 1);
}

bool Contains(std::span<const int> models, int model)
{
    return std::ranges::find(models, model) != models.end();
}

// En la derecha, los índices 8 a 14, 16 y de 18 en adelante del grupo 4 (0x4f2d2f..0x4f2d51; las
// ballestas de MuMain).
bool IsRightGroup4(int model)
{
    return (model >= 0xa1a && model < 0xa21) || model == 0xa22 || (model >= 0xa24 && model < 0xc12);
}

// En la izquierda, los índices 0 a 6, 17, 20 y 21 del grupo 4 (0x4f2d05..0x4f2d2d; los arcos de
// MuMain).
bool IsLeftGroup4(int model)
{
    return (model >= 0xa12 && model < 0xa19) || model == 0xa23 || model == 0xa26 || model == 0xa27;
}

bool IsFlyingMap(std::int32_t map)
{
    return map == AtlansMap || (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

std::uint8_t UnarmedFor(const ActorState& actor)
{
    return (actor.characterClass & ClassMask) == ElfClass ? UnarmedElfStand : UnarmedStand;
}

// La marca de un arma de la derecha: el byte +0x1e de su registro de Item.bmd.
Core::Expected<bool, std::string> IsMarked(int model, const Data::Tables::ItemTable* items)
{
    if (items == nullptr)
    {
        return Core::Unexpected(std::string("0x4f2250: falta Item.bmd para leer la marca +0x1e del arma"));
    }
    const auto record = items->Record(static_cast<std::size_t>(model - Data::Models::FirstItemModel));
    if (!record)
    {
        return Core::Unexpected(std::string("0x4f2250: Item.bmd no tiene el registro del arma de la derecha"));
    }
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

// La de un arma de la derecha con la marca leída: sin ella, la 3; con ella, `marked`.
Core::Expected<std::uint8_t, std::string> ByMark(int right, const Data::Tables::ItemTable* items, std::uint8_t marked)
{
    const auto isMarked = IsMarked(right, items);
    if (!isMarked.HasValue())
    {
        return Core::Unexpected(isMarked.Error());
    }
    return isMarked.Value() ? marked : ArmedStand;
}

// 0x4f2bf6..0x4f2d82: con los pies en el piso, por las armas.
Core::Expected<std::uint8_t, std::string> GroundStand(const ActorState& actor, std::int32_t map,
                                                      const Data::Tables::ItemTable* items)
{
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool unarmed = actor.hands[0].model == NoItemModel && actor.hands[1].model == NoItemModel;
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

// 0x4f228c..0x4f22f6: Fenrir fuera de la zona segura, por las dos manos.
std::uint8_t FenrirStand(const ActorState& actor)
{
    const bool right = actor.hands[0].model != NoItemModel;
    const bool left = actor.hands[1].model != NoItemModel;
    if (right)
    {
        return left ? FenrirBothHandsStand : FenrirRightHandStand;
    }
    return left ? FenrirLeftHandStand : FenrirUnarmedStand;
}

// 0x4f2365..0x4f23dd: vuela con alas fuera de la zona segura o con +0x376 en 0x101. En los mapas de
// vuelo de la escena 5 decide además la celda del TerrainWall, que MuRet no tiene en el juego.
Core::Expected<bool, std::string> IsFlying(const ActorState& actor, const ActorStopContext& context)
{
    if (context.scene == SceneId::Game && IsFlyingMap(context.runtime.map))
    {
        return Core::Unexpected(std::string("0x4f2250: en los mapas de vuelo de la escena 5 falta la celda del "
                                            "personaje en el TerrainWall vivo (0x7d0d348, 0x4f23c1)"));
    }
    const bool withWings = actor.safeZone == 0 && actor.wings.model != NoItemModel;
    return withWings || actor.word376 == FlyingWord376;
}

// La acción de parado, o std::nullopt cuando ya está sentado y no hay que poner ninguna.
Core::Expected<std::optional<std::uint8_t>, std::string> ChooseStand(ActorState& actor, const ActorStopContext& context)
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
        const bool unarmed = actor.hands[0].model == NoItemModel && actor.hands[1].model == NoItemModel;
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
        Render::Models::SetAction(actor.animation, context.playerSkeleton, SittingAction);
        return Core::Unexpected(std::string("0x4f2250: sentarse manda un C1 .. 18 con el XOR32 en línea "
                                            "(0x4f2416..0x4f2bea, CL9) que MuRet no tiene"));
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

// 0x4f36be..0x4f37a5: el sufijo, con la acción ya puesta.
Core::Expected<ActorStopResult, std::string> Suffix(const ActorState& actor, Render::Models::CrtRandom& random)
{
    if (random.Next() % SuffixChance != 0)
    {
        return ActorStopResult{};
    }
    if (actor.subtype >= FirstModelSoundSubtype && actor.subtype <= LastModelSoundSubtype)
    {
        return Core::Unexpected(std::string("0x4f2250: con el subtipo 209 a 211 el sufijo lee la tabla de "
                                            "sonidos de los modelos (0x5769ad8 + tipo · 0xe0 + 0xaa)"));
    }
    if (actor.pet.model != Fenrir || random.Next() % FenrirSoundChance != 0)
    {
        return ActorStopResult{};
    }
    return ActorStopResult{.sound = FenrirSoundBase + random.Next() % FenrirSoundVariants};
}
} // namespace

Core::Expected<ActorStopResult, std::string> SetActorStop(ActorState& actor, const ActorStopContext& context)
{
    actor.run = 0;
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        return Core::Unexpected(std::string("0x4f2250: el personaje que no es jugador (0x4f2d87..0x4f36b5) lee "
                                            "sus celdas protegidas y el TerrainWall, sin portar"));
    }
    const auto stand = ChooseStand(actor, context);
    if (!stand.HasValue())
    {
        return Core::Unexpected(stand.Error());
    }
    if (stand.Value())
    {
        Render::Models::SetAction(actor.animation, context.playerSkeleton, *stand.Value());
    }
    return Suffix(actor, context.random);
}
} // namespace Client::Scenes
