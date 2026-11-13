#include "Scenes/ActorCombat.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/MonsterSkillTable.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/MonsterSounds.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
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

// Una lectura de un byte protegido: abre su entrada, lee y la cierra, con el rand() del cierre que la deja sin accesos
// (0x40b3f0: uno, el byte).
constexpr std::uint16_t ByteCloseRandoms = 1;
std::uint8_t ReadProtected(const std::uint8_t& value, std::uint8_t& accesses, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(accesses, random, ByteCloseRandoms);
    return value;
}

template <typename T, std::size_t N> bool IsOneOf(const std::array<T, N>& values, T value)
{
    return std::ranges::find(values, value) != values.end();
}

// Los grupos de +0x376 de 0x43a2f0 (la tabla de bytes de 0x43a644 sobre +0x376 − 0x91 y sus saltos de 0x43a628).
constexpr std::array<std::uint16_t, 6> KalimaFirstGroup{0x91, 0xaf, 0xb7, 0xbf, 0x105, 0x10d};
constexpr std::array<std::uint16_t, 12> KalimaElevenGroups{0x93, 0xb1, 0xb9, 0xc1, 0x107, 0x10f,
                                                           0x94, 0xb2, 0xba, 0xc2, 0x108, 0x110};
constexpr std::array<std::uint16_t, 6> KalimaFourthGroup{0x95, 0xb3, 0xbb, 0xc3, 0x109, 0x111};
constexpr std::array<std::uint16_t, 12> KalimaDrawingGroup{0xa0, 0xa1, 0xb4,  0xb5,  0xbc,  0xbd,
                                                           0xc4, 0xc5, 0x10a, 0x10b, 0x112, 0x113};
// 0x43a3c5: los valores de +0x38e − 1 que la tabla de 0x43a6d0 manda a la 4 (0x43a3d2).
constexpr std::array<std::uint8_t, 6> KalimaSecondActionSkills{1, 0x11, 0x37, 0xc8, 0xc9, 0xca};
constexpr std::uint8_t KalimaSkill = 0x11;
constexpr std::uint8_t FirstKalimaSkill = 1;
// Las dos acciones de pegar de los monstruos.
constexpr std::uint8_t FirstAttack = 3;
constexpr std::uint8_t SecondAttack = 4;

// 0x43a2f0: los monstruos de Kalima, en cualquier mapa. Lee +0x38e (y cierra su entrada) antes de elegir la acción;
// los del último grupo sacan rand() & 1 (0x43a5d4 y 0x43a5fb, con el AND con signo de un rand() que no es negativo).
// Devuelve si atendió (AL en 1): todos los de la tabla, aunque no pongan acción (0x43a5ce).
Core::Expected<bool, std::string> AttackOfKalima(ActorState& actor, const ActorCombatContext& context)
{
    const std::uint16_t kind = actor.word376;
    std::optional<std::uint8_t> action;
    if (IsOneOf(KalimaFirstGroup, kind))
    {
        // 0x43a322..0x43a3e6: el cierre en línea (0x43a385..0x43a3ab) y el switch de 0x43a3cb.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        action = IsOneOf(KalimaSecondActionSkills, skill) ? SecondAttack : FirstAttack;
    }
    else if (IsOneOf(KalimaElevenGroups, kind))
    {
        // 0x43a3fa..0x43a48b (cierra con 0x40b0a0) y 0x43a48c..0x43a533 (en línea): 0x11, la 4; si no, la 3.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        action = skill == KalimaSkill ? SecondAttack : FirstAttack;
    }
    else if (IsOneOf(KalimaFourthGroup, kind))
    {
        // 0x43a534..0x43a5ce: 1, la 4; 0x11, la 3; otro, ninguna.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        if (skill == FirstKalimaSkill)
            action = SecondAttack;
        else if (skill == KalimaSkill)
            action = FirstAttack;
    }
    else if (IsOneOf(KalimaDrawingGroup, kind))
    {
        action = static_cast<std::uint8_t>((context.random.Next() & 1) + FirstAttack);
    }
    else
    {
        return false;
    }
    if (action)
    {
        if (auto set = SetTheAction(actor, context, *action); !set.HasValue())
            return Core::Unexpected(set.Error());
    }
    return true;
}

// 0x443750: el 0x123 del mapa 0x1f (0x43ff10). Con +0x38e abierto, '2' (0x32) la 3 y otro la 4 (0x44382a..0x443897);
// después cierra la entrada (0x443946..0x443974). Atiende siempre.
Core::Expected<bool, std::string> AttackOfTheTrials(ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::int32_t TrialsMap = 0x1f;
    constexpr std::uint16_t TrialsKind = 0x123;
    constexpr std::uint8_t FirstActionSkill = 0x32;
    if (context.world.Runtime().map != TrialsMap || actor.word376 != TrialsKind)
        return false;
    const Render::Models::LogicalAccess access(actor.byte38eAccesses, context.random, ByteCloseRandoms);
    if (auto set = SetTheAction(actor, context, actor.byte38e == FirstActionSkill ? FirstAttack : SecondAttack);
        !set.HasValue())
        return Core::Unexpected(set.Error());
    return true;
}

// 0x4f92f0: con +0x42e abierto, busca su valor (sin signo) entre las cinco skills de +0x376 en MonsterSkill.bmd
// (0x7437670 + +0x376 · 0x18); en el lugar 0 a 4, las acciones 3, 4, 8, 9 y 0xb (la tabla de 0x4f9564). Después deja
// +0x42e en 0xff (0x4f9476) y cierra la entrada. Devuelve si la encontró.
Core::Expected<bool, std::string> AttackFromTheSkillTable(ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::array<std::uint8_t, Data::Tables::MonsterSkillCount> SkillActions{3, 4, 8, 9, 0xb};
    constexpr std::uint8_t UsedSkill = 0xff;
    const Render::Models::LogicalAccess access(actor.byte42eAccesses, context.random, ByteCloseRandoms);
    const Data::Tables::MonsterSkills listed = context.world.Runtime().monsterSkills.Of(actor.word376);
    const auto found = std::ranges::find(listed.skills, static_cast<std::int32_t>(actor.byte42e));
    const bool matched = found != listed.skills.end();
    if (matched)
    {
        const auto index = static_cast<std::size_t>(found - listed.skills.begin());
        if (auto set = SetTheAction(actor, context, SkillActions[index]); !set.HasValue())
            return Core::Unexpected(set.Error());
    }
    actor.byte42e = UsedSkill;
    return matched;
}

// 0x429270 (0x425070: los mapas 0x21 y 0x36) con 0x130, 0x131 y 0x135, y 0x437580 (0x4303c0: el 0x22, Crywolf) con
// 0x154, 0x155, 0x158, 0x159 y 0x15d (la tabla de 0x4375cc): los dos llaman a 0x4f92f0 y devuelven lo suyo.
Core::Expected<bool, std::string> AttackOfTheSkillMaps(ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::array<std::uint16_t, 3> DoppelgangerKinds{0x130, 0x131, 0x135};
    constexpr std::array<std::uint16_t, 5> CrywolfKinds{0x154, 0x155, 0x158, 0x159, 0x15d};
    const std::int32_t map = context.world.Runtime().map;
    const bool doppelganger = (map == 0x21 || map == 0x36) && IsOneOf(DoppelgangerKinds, actor.word376);
    const bool crywolf = map == 0x22 && IsOneOf(CrywolfKinds, actor.word376);
    if (!doppelganger && !crywolf)
        return false;
    return AttackFromTheSkillTable(actor, context);
}

// 0x4f5034..0x4f50b2: los cinco ayudantes, en su orden y todos (0x43a2f0, 0x42dcb0, 0x443750, 0x429270 y 0x437580), y
// la 3 o la 4 por +0x38f si ninguno atendió (BL en 1). 0x437580 en 1 salta directo a 0x4f50b8.
Core::Expected<void, std::string> MonsterAttack(ActorState& actor, const ActorCombatContext& context)
{
    auto kalima = AttackOfKalima(actor, context);
    if (!kalima.HasValue())
        return Core::Unexpected(kalima.Error());
    // 0x42dcb0, el segundo: el 0x19e y el 0x19f ponen +0x110 en 3 y la acción 5.
    const bool special = actor.type == 0x19e || actor.type == 0x19f;
    if (special)
    {
        actor.byte110 = 3;
        if (auto set = SetTheAction(actor, context, 5); !set.HasValue())
            return set;
    }
    auto trials = AttackOfTheTrials(actor, context);
    if (!trials.HasValue())
        return Core::Unexpected(trials.Error());
    // 0x429270 y 0x437580 no se pisan: sus mapas son otros.
    auto skilled = AttackOfTheSkillMaps(actor, context);
    if (!skilled.HasValue())
        return Core::Unexpected(skilled.Error());
    if (kalima.Value() || special || trials.Value() || skilled.Value())
        return {};
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

// 0x4f5acb..0x4f5ad8: con el subtipo 9, 5 más (los otros sonidos de la tabla, en DrawModelSound).
std::int32_t SubtypeOffset(const ActorState& actor)
{
    constexpr std::int32_t NinthSubtype = 9;
    return actor.subtype == NinthSubtype ? 5 : 0;
}

// La tabla del modelo: la del contexto o, sin ella, la que escribió 0x612c80 (WorldRuntime::modelSounds).
std::optional<ModelSounds> SoundsOf(const ActorState& actor, const ActorCombatContext& context)
{
    if (context.modelSounds)
        return context.modelSounds(actor.type);
    return WrittenSounds(context.world.Runtime().modelSounds, actor.type);
}

void AttackSound(const ActorState& actor, const ActorCombatContext& context)
{
    if (!FrameIsZero(actor))
        return;
    if (actor.type == Data::Models::PlayerSkeletonModel && !HasModelSoundSubtype(actor))
    {
        PlayerAttackSound(actor, context);
        return;
    }
    auto& sound = context.world.Runtime().lastSoundId;
    // 0x4f51c0..0x4f51ee: los subtipos 0xd1 a 0xd3, rand() & 1 + 0x3c.
    if (HasModelSoundSubtype(actor))
        sound = static_cast<std::int32_t>((context.random.Next() & 1) + 0x3c);
    // 0x4f51f1..0x4f5262: la WORD 2 o la 3 de la tabla del modelo (DrawModelSound).
    constexpr std::size_t FirstAttackSound = 2;
    if (const auto drawn = DrawModelSound(SoundsOf(actor, context), FirstAttackSound, actor.subtype, context.random))
        sound = drawn;
}

// 0x4f5a09..0x4f5ae8: el sonido de morir con el frame en 0.
void DeathSound(const ActorState& actor, const ActorCombatContext& context)
{
    auto& sound = context.world.Runtime().lastSoundId;
    const std::int32_t map = context.world.Runtime().map;
    if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
    {
        sound = static_cast<std::int32_t>((context.random.Next() & 1) + 0x5e);
        return;
    }
    if (actor.type != Data::Models::PlayerSkeletonModel || HasModelSoundSubtype(actor))
    {
        // 0x4f5aa8..0x4f5ae8: la WORD 4 de la tabla del modelo; 0xffff, nada; si no, con signo, más 5 con el subtipo 9
        // y más 0xd2. Sin la tabla, la WORD en 0 de 0x60dc80 (SoundsOrZeros): el 0xd2. No saca rand().
        const ModelSounds table = SoundsOrZeros(SoundsOf(actor, context));
        constexpr std::size_t DeathSoundWord = 4;
        if (table[DeathSoundWord] == -1)
            return;
        constexpr std::int32_t MonsterSoundBase = 0xd2;
        sound = table[DeathSoundWord] + SubtypeOffset(actor) + MonsterSoundBase;
        return;
    }
    const auto characterClass = actor.characterClass & ClassMask;
    sound = characterClass == ElfClass ? 0x6e : characterClass == DarkLordClass ? 0x1c7 : 0x6c;
    // 0x4f5a6b: con Fenrir, también el 0x278.
    if (actor.pet.model == Fenrir)
        sound = 0x278;
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
    AttackSound(actor, context);
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
    DeathSound(actor, context);
    actor.animation.frame = DeathFrame;
    return {};
}
} // namespace Client::Scenes
