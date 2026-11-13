#include "Scenes/ActorCombat.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t Uniria = 0x1c14;
constexpr std::uint16_t Dinorant = 0x1c15;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t DarkLordClass = 4;
constexpr int NoModel = -1;
// 0x4f2c4c y 0x4f4c28: el byte +0x1e del registro del arma en Item.bmd ([0x7758550] − 0xadca + modelo · 0x54).
constexpr std::size_t WeaponMarkOffset = 0x1e;
// 0x4f5af0: el frame que deja 0x4f5660, 0,001f.
constexpr float DeathFrame = std::bit_cast<float>(0x3a83126fu);
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;

// 0x4f4a10 compara los modelos como WORD con signo.
int Signed(std::uint16_t model)
{
    return static_cast<std::int16_t>(model);
}
// En la izquierda, los índices 0 a 6, 17, 20 y 21 del grupo 4 (los arcos de MuMain).
bool IsLeftGroup4(int model)
{
    return (model >= 0xa12 && model < 0xa19) || model == 0xa23 || model == 0xa26 || model == 0xa27;
}
// En la derecha, los índices 8 a 14, 16 y de 18 en adelante del grupo 4 (las ballestas de MuMain).
bool IsRightGroup4(int model)
{
    return (model >= 0xa1a && model < 0xa21) || model == 0xa22 || (model >= 0xa24 && model < 0xc12);
}
// Los grupos 0 a 2 (0x212..0x811).
bool IsMelee(int model)
{
    return model >= 0x212 && model < 0x812;
}
bool HasModelSoundSubtype(const ActorState& actor)
{
    return actor.subtype >= 0xd1 && actor.subtype <= 0xd3;
}
// 0x4f50b8 y 0x4f5a09 miran C3 después de FCOMP con 0: igual a 0 o unordered.
bool FrameIsZero(const ActorState& actor)
{
    return !(actor.animation.frame < 0.0f || actor.animation.frame > 0.0f);
}
bool IsHero(const ActorState& actor, const WorldState& world)
{
    return &actor == &world.Actors()[world.Runtime().hero];
}

Core::Expected<bool, std::string> IsMarked(int model, const ActorCombatContext& context)
{
    if (context.items == nullptr)
        return Core::Unexpected(std::string("0x4f4a10: falta Item.bmd para leer la marca +0x1e del arma"));
    const auto record = context.items->Record(static_cast<std::size_t>(model - Data::Models::FirstItemModel));
    if (!record)
        return Core::Unexpected(std::string("0x4f4a10: Item.bmd no tiene el registro del arma de la derecha"));
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

// 0x4ea030, con el esqueleto del modelo del personaje.
Core::Expected<void, std::string> SetTheAction(ActorState& actor, const ActorCombatContext& context,
                                               std::uint8_t action)
{
    const Render::Models::Skeleton* skeleton = nullptr;
    if (actor.type == Data::Models::PlayerSkeletonModel && context.playerSkeleton != nullptr)
        skeleton = context.playerSkeleton;
    else if (context.models != nullptr)
        skeleton = context.models->SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: falta el esqueleto del modelo {:#x}", actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

// 0x4f4a4e..0x4f4ae8: Fenrir fuera de la zona segura.
std::uint8_t FenrirAttack(int right, int left)
{
    if (right >= 0x812 && right < 0x817)
        return 0x5f;
    if (IsLeftGroup4(left))
        return 0x61;
    if (IsRightGroup4(right))
        return 0x5e;
    if (right == NoModel)
        return left == NoModel ? 0x56 : 0x60;
    return left == NoModel ? 0x60 : 0x5c;
}

// 0x4f4b4b..0x4f4c5e: Uniria y Dinorant fuera de la zona segura.
Core::Expected<std::uint8_t, std::string> RideAttack(int right, int left, const ActorCombatContext& context)
{
    if (right >= 0x812 && right < 0x817)
        return std::uint8_t{0x34};
    if (right >= 0x817 && right < 0xa12)
        return std::uint8_t{0x35};
    if (IsLeftGroup4(left))
        return std::uint8_t{0x36};
    if (IsRightGroup4(right))
        return std::uint8_t{0x37};
    if (right == NoModel)
        return std::uint8_t{0x32};
    const auto marked = IsMarked(right, context);
    if (!marked.HasValue())
        return Core::Unexpected(marked.Error());
    return marked.Value() ? std::uint8_t{0x33} : std::uint8_t{0x32};
}

// 0x4f4c63..0x4f4f2f: a pie, por las armas. Las dos que sortean sacan rand() & 1.
Core::Expected<std::uint8_t, std::string> GroundAttack(const ActorState& actor, int right, int left,
                                                       const ActorCombatContext& context)
{
    const std::uint8_t count = actor.attackCount;
    if (right == NoModel && left == NoModel)
        return std::uint8_t{0x22};
    if (IsMelee(right))
    {
        const auto marked = IsMarked(right, context);
        if (!marked.HasValue())
            return Core::Unexpected(marked.Error());
        if (!marked.Value())
        {
            if (!IsMelee(left))
                return static_cast<std::uint8_t>((count & 1) + 0x23);
            // 0x4f4cf0: la tabla 0x4f5278 por +0x38f & 3.
            constexpr std::array<std::uint8_t, 4> TwoWeapons{0x23, 0x25, 0x24, 0x26};
            return TwoWeapons[count & 3];
        }
        if (right == 0x227 || right == 0x231 || right == 0x229)
            return std::uint8_t{0x8d};
        return static_cast<std::uint8_t>(count % 3 + 0x27);
    }
    if (IsMelee(left))
        return static_cast<std::uint8_t>((context.random.Next() & 1) + 0x25);
    if (right >= 0xc12 && right < 0xe12)
    {
        const auto marked = IsMarked(right, context);
        if (!marked.HasValue())
            return Core::Unexpected(marked.Error());
        const auto drawn = context.random.Next() & 1;
        return static_cast<std::uint8_t>(drawn + (marked.Value() ? 0x90 : 0x23));
    }
    if (right == 0x813 || right == 0x814)
        return std::uint8_t{0x2a};
    if (right >= 0x812 && right < 0xa12)
        return static_cast<std::uint8_t>(count % 3 + 0x2b);
    const bool noWings = actor.wings.model == NoItemModel;
    if (IsLeftGroup4(left))
        return noWings ? std::uint8_t{0x2e} : std::uint8_t{0x30};
    if (IsRightGroup4(right))
        return noWings ? std::uint8_t{0x2f} : std::uint8_t{0x31};
    return std::uint8_t{0x22};
}

Core::Expected<void, std::string> PlayerAttack(ActorState& actor, const ActorCombatContext& context)
{
    // 0x4f4a2a: 0x4f3f80, las velocidades de Player.bmd desde el registro propio (3848 rand() al cerrarlo).
    RefreshCombatActionSpeeds(context.world, context.random);
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool outside = actor.safeZone == 0;
    const std::uint16_t pet = actor.pet.model;
    if (pet == Fenrir && outside)
    {
        if (auto set = SetTheAction(actor, context, FenrirAttack(right, left)); !set.HasValue())
            return set;
        // 0x4f4af0: el Dark Lord pisa la acción con la 0x59.
        if ((actor.characterClass & ClassMask) == DarkLordClass)
            return SetTheAction(actor, context, 0x59);
        return {};
    }
    if (pet == DarkHorse && outside)
        return SetTheAction(actor, context, 0x50);
    const auto action = (pet == Uniria || pet == Dinorant) && outside ? RideAttack(right, left, context)
                                                                      : GroundAttack(actor, right, left, context);
    if (!action.HasValue())
        return Core::Unexpected(action.Error());
    return SetTheAction(actor, context, action.Value());
}

// Los +0x376 de los monstruos de Kalima que atiende 0x43a2f0 (las tablas 0x43a644 y 0x43a628 del binario).
bool IsKalimaAttacker(std::uint16_t word376)
{
    constexpr std::array<std::uint16_t, 36> Kalima{0x91, 0xaf,  0xb7,  0xbf, 0x105, 0x10d, 0x93,  0xb1,  0xb9,
                                                   0xc1, 0x107, 0x10f, 0x94, 0xb2,  0xba,  0xc2,  0x108, 0x110,
                                                   0x95, 0xb3,  0xbb,  0xc3, 0x109, 0x111, 0xa0,  0xa1,  0xb4,
                                                   0xb5, 0xbc,  0xbd,  0xc4, 0xc5,  0x10a, 0x10b, 0x112, 0x113};
    return std::ranges::find(Kalima, word376) != Kalima.end();
}

// 0x4f5034..0x4f50b2: los cinco ayudantes, en su orden, y la 3 o la 4 si ninguno atendió.
Core::Expected<void, std::string> MonsterAttack(ActorState& actor, const ActorCombatContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    const std::uint16_t kind = actor.word376;
    // 0x43a2f0: lee el byte protegido +0x38e (sus cierres sacan rand()) y elige la 3 o la 4 según su valor.
    if (IsKalimaAttacker(kind))
        return Core::Unexpected(std::format("0x43a2f0: el ataque del monstruo {:#x} de Kalima (+0x38e protegido) no "
                                            "está portado (CL9 hito 3, fase 2)",
                                            kind));
    // 0x443750 (el mapa 0x1f, Land of Trials), 0x429270 (0x425070: los mapas 0x21 y 0x36) y 0x437580 (0x4303c0: el
    // 0x22, Crywolf) llaman a 0x4f92f0 con estos +0x376.
    if (map == 0x1f && kind == 0x123)
        return Core::Unexpected(std::string("0x443750: el ataque del 0x123 en el mapa 31 no está portado (CL9 hito 3, "
                                            "fase 2)"));
    if ((map == 0x21 || map == 0x36) && (kind == 0x130 || kind == 0x131 || kind == 0x135))
        return Core::Unexpected(std::format("0x429270: el ataque del {:#x} (0x4f92f0) no está portado (CL9 hito 3, "
                                            "fase 2)",
                                            kind));
    if (map == 0x22 && (kind == 0x154 || kind == 0x155 || kind == 0x158 || kind == 0x159 || kind == 0x15d))
        return Core::Unexpected(std::format("0x437580: el ataque del {:#x} en Crywolf (0x4f92f0) no está portado (CL9 "
                                            "hito 3, fase 2)",
                                            kind));
    // 0x42dcb0: el 0x19e y el 0x19f ponen +0x110 en 3 y la acción 5.
    if (actor.type == 0x19e || actor.type == 0x19f)
    {
        actor.byte110 = 3;
        return SetTheAction(actor, context, 5);
    }
    if (auto set = SetTheAction(actor, context, actor.attackCount % 3 == 0 ? 3 : 4); !set.HasValue())
        return set;
    ++actor.attackCount;
    return {};
}

// 0x4f50cf..0x4f51bb: el sonido del jugador por el arma.
void PlayerAttackSound(const ActorState& actor, const ActorCombatContext& context)
{
    auto& sound = context.world.Runtime().lastSoundId;
    const int left = Signed(actor.hands[1].model);
    const int right = Signed(actor.hands[0].model);
    if (IsLeftGroup4(left))
        sound = 0x55;
    else if (IsRightGroup4(right))
        sound = 0x56;
    else if (right >= 0xa1f && right < 0xa21)
        sound = 0x76;
    else if (right == 0x21c || right == 0x812)
        sound = 0x3e;
    else if (right != NoModel || left != NoModel)
        sound = static_cast<std::int32_t>((context.random.Next() & 1) + 0x3c);
}

Core::Expected<void, std::string> AttackSound(const ActorState& actor, const ActorCombatContext& context)
{
    if (!FrameIsZero(actor))
        return {};
    if (actor.type == Data::Models::PlayerSkeletonModel && !HasModelSoundSubtype(actor))
    {
        PlayerAttackSound(actor, context);
        return {};
    }
    // 0x4f51c0..0x4f51ee: los subtipos 0xd1 a 0xd3, rand() & 1 + 0x3c.
    if (HasModelSoundSubtype(actor))
        context.world.Runtime().lastSoundId = static_cast<std::int32_t>((context.random.Next() & 1) + 0x3c);
    return Core::Unexpected(std::format("0x4f51f1: el sonido de pegar del modelo {:#x} sale de su tabla de sonidos "
                                        "(0x5769ad8 + tipo · 0xe0 + 0xae), de la CL10",
                                        actor.type));
}

// 0x4f5a09..0x4f5ae8: el sonido de morir con el frame en 0.
Core::Expected<void, std::string> DeathSound(const ActorState& actor, const ActorCombatContext& context)
{
    auto& sound = context.world.Runtime().lastSoundId;
    const std::int32_t map = context.world.Runtime().map;
    if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
    {
        sound = static_cast<std::int32_t>((context.random.Next() & 1) + 0x5e);
        return {};
    }
    if (actor.type != Data::Models::PlayerSkeletonModel || HasModelSoundSubtype(actor))
        return Core::Unexpected(std::format("0x4f5a7a: el sonido de morir del modelo {:#x} sale de su tabla de "
                                            "sonidos (0x5769ad8 + tipo · 0xe0 + 0xb2), de la CL10",
                                            actor.type));
    const auto characterClass = actor.characterClass & ClassMask;
    sound = characterClass == ElfClass ? 0x6e : characterClass == DarkLordClass ? 0x1c7 : 0x6c;
    // 0x4f5a6b: con Fenrir, también el 0x278.
    if (actor.pet.model == Fenrir)
        sound = 0x278;
    return {};
}
} // namespace

ActorCombatContext CombatContextOf(const WorldEntryResources& resources)
{
    return ActorCombatContext{.world = resources.world,
                              .random = resources.cloth.random,
                              .playerSkeleton = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel),
                              .models = &resources.models,
                              .items = resources.items};
}

Core::Expected<void, std::string> SetActorAttack(ActorState& actor, const ActorCombatContext& context)
{
    switch (actor.type)
    {
    case Data::Models::PlayerSkeletonModel:
        if (auto attacked = PlayerAttack(actor, context); !attacked.HasValue())
            return attacked;
        break;
    case 0x27:
        return Core::Unexpected(std::string("0x4f4f5b: el efecto 0xd4 del 0x27 al pegar (0x518cf0) es de la CL11"));
    case 0x28:
        if (auto set = SetTheAction(actor, context, 1); !set.HasValue())
            return set;
        context.world.Runtime().lastSoundId = 0x10;
        break;
    case 0x33:
        return Core::Unexpected(std::string("0x4f4fb9: el efecto 0x5da del 0x33 al pegar (0x518cf0) es de la CL11"));
    case 0x172:
    {
        // 0x4f4fd7..0x4f502f: rand() & 7; con 0 la 9, con 1 o 2 la 8, y si no rand() & 1 + 3.
        const auto first = context.random.Next() & 7;
        const auto action = first < 3 ? (first > 0 ? 8u : 9u) : (context.random.Next() & 1) + 3;
        if (auto set = SetTheAction(actor, context, static_cast<std::uint8_t>(action)); !set.HasValue())
            return set;
        break;
    }
    default:
        if (auto attacked = MonsterAttack(actor, context); !attacked.HasValue())
            return attacked;
        break;
    }
    if (auto sounded = AttackSound(actor, context); !sounded.HasValue())
        return sounded;
    // 0x4f5265: +0x38f + 1 siempre (los monstruos comunes ya sumaron uno en 0x4f50b2).
    ++actor.attackCount;
    return {};
}

Core::Expected<void, std::string> SetActorDie(ActorState& actor, const ActorCombatContext& context)
{
    // 0x4f5682..0x4f587b: el héroe abre el registro propio, pone la vida (+0x22) en 0 y lo cierra.
    if (IsHero(actor, context.world))
    {
        const Render::Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        context.world.OwnCharacter().life = 0;
    }
    if (actor.type == Data::Models::PlayerSkeletonModel && !HasModelSoundSubtype(actor))
    {
        if (auto set = SetTheAction(actor, context, 0xc3); !set.HasValue())
            return set;
    }
    else if (actor.type == Data::Models::PlayerSkeletonModel || actor.type == 0x16b || actor.type == 0x170)
    {
        // 0x4f58a3..0x4f59fc: +0 en 0, los efectos 0xd5 y diez 0xd6 (o ocho pares 0xe5/0xe6 el 0x16b) y el sonido 0x7c.
        return Core::Unexpected(std::format("0x4f58a3: los efectos de la muerte del modelo {:#x} (0x518cf0) son de "
                                            "la CL11",
                                            actor.type));
    }
    else if (auto set = SetTheAction(actor, context, 6); !set.HasValue())
    {
        return set;
    }
    if (!FrameIsZero(actor))
        return {};
    if (auto sounded = DeathSound(actor, context); !sounded.HasValue())
        return sounded;
    actor.animation.frame = DeathFrame;
    return {};
}
} // namespace Client::Scenes
