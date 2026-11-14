#include "Scenes/ActorShock.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t Uniria = 0x1c14;
constexpr std::uint16_t Dinorant = 0x1c15;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint8_t PlayerShockAction = 0xc2;
constexpr std::uint8_t MonsterShockAction = 5;
bool IgnoresShock(const ActorState& actor, std::uint16_t damage)
{
    const auto action = actor.animation.action;
    return damage == 0 || actor.dying != 0 || actor.pet.model == Uniria || actor.pet.model == Dinorant ||
           actor.pet.model == DarkHorse || action == 0x3e || action == 0x3f || action == 0x44;
}
std::uint8_t FenrirAction(const ActorState& actor)
{
    const bool right = actor.hands[0].model != NoItemModel;
    const bool left = actor.hands[1].model != NoItemModel;
    if (right && left)
        return 0x67;
    if (right)
        return 0x68;
    if (left)
        return 0x69;
    return 0x66;
}
void PlayerHitSound(const ActorState& actor, const WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    auto& sound = resources.world.Runtime().lastSoundId;
    constexpr std::uint8_t ClassMask = 7;
    constexpr std::uint8_t ElfClass = 2;
    constexpr std::uint8_t DarkLordClass = 4;
    const auto characterClass = actor.characterClass & ClassMask;
    if (characterClass == ElfClass)
    {
        sound = 0x6d + (random.Next() & 1);
        return;
    }
    if (characterClass == DarkLordClass && random.Next() % 5 != 0)
    {
        sound = 0x1c6;
        return;
    }
    sound = 0x69 + random.Next() % 3;
}
void HitSound(const ActorState& actor, const WorldEntryResources& resources)
{
    // 0x4f5496 mira C3: unordered también entra en el sonido.
    if (actor.animation.frame < 0.0f || actor.animation.frame > 0.0f)
        return;
    auto& sound = resources.world.Runtime().lastSoundId;
    if (actor.subtype >= 209 && actor.subtype <= 211)
    {
        sound = 0x7b;
        return;
    }
    if (actor.type == Data::Models::PlayerSkeletonModel)
    {
        PlayerHitSound(actor, resources);
        return;
    }
    constexpr std::uint16_t SilentMonster = 0x160;
    if (actor.type == SilentMonster)
        return;
    // 0x4f553d..0x4f55a7: el WORD +0xae del modelo (el tercero de los diez de 0x612c80, que carga el alta; en 0 en un
    // modelo sin tabla o sin cargar); en −1, sin sonido ni rand(). Si no, rand() & 1 elige +0xae o +0xb0, y el sonido
    // es ese WORD + 0xd2, + 5 con el subtipo 9 (CL10).
    constexpr std::size_t HitSoundWord = 2;
    constexpr std::int32_t HitSoundBase = 0xd2;
    constexpr std::int32_t ShiftedSubtype = 9;
    constexpr std::int32_t ShiftedSound = 5;
    const std::span<const std::int16_t, 10> sounds = resources.world.FaunaResources().SoundActions(actor.type);
    if (sounds[HitSoundWord] == -1)
        return;
    const std::int32_t shift = actor.subtype == ShiftedSubtype ? ShiftedSound : 0;
    const auto chosen = HitSoundWord + static_cast<std::size_t>(resources.cloth.random.Next() & 1);
    sound = sounds[chosen] + shift + HitSoundBase;
}
} // namespace
Core::Expected<void, std::string> SetActorShock(ActorState& actor, std::uint16_t damage,
                                                const WorldEntryResources& resources)
{
    if (IgnoresShock(actor, damage))
        return {};
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        // 0x4f5475..0x4f5480: 0x4ea030(c, 5) con las acciones del modelo del monstruo (el que cargó su alta, no
        // Player.bmd); un modelo que no está es el registro sin acciones y no pone nada.
        if (actor.animation.action < 3 || actor.animation.action > 4)
        {
            if (auto set = SetMonsterAction(actor, MonsterShockAction, 0x4f5480, resources); !set.HasValue())
                return set;
        }
    }
    else
    {
        const auto* skeleton = resources.models.SkeletonOf(actor.type);
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4f53a0: falta Player.bmd para el personaje golpeado"));
        const bool fenrir = actor.pet.model == Fenrir;
        Render::Models::SetAction(actor.animation, *skeleton, fenrir ? FenrirAction(actor) : PlayerShockAction);
        if (fenrir && resources.cloth.random.Next() % 3 == 0)
            resources.world.Runtime().lastSoundId = 0x27b + (resources.cloth.random.Next() & 1);
        actor.movement = 0;
    }
    HitSound(actor, resources);
    constexpr std::uint16_t SparkMonster = 0x18f;
    if (actor.type == SparkMonster)
        return Core::Unexpected(std::string("0x4f55bb: las cinco chispas del monstruo no están portadas"));
    return {};
}
} // namespace Client::Scenes
