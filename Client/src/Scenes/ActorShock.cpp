#include "Scenes/ActorShock.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"

#include <cstdint>
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
Core::Expected<void, std::string> HitSound(const ActorState& actor, const WorldEntryResources& resources)
{
    // 0x4f5496 mira C3: unordered también entra en el sonido.
    if (actor.animation.frame < 0.0f || actor.animation.frame > 0.0f)
        return {};
    if (actor.subtype >= 209 && actor.subtype <= 211)
    {
        resources.world.Runtime().lastSoundId = 0x7b;
        return {};
    }
    if (actor.type == Data::Models::PlayerSkeletonModel)
    {
        PlayerHitSound(actor, resources);
        return {};
    }
    constexpr std::uint16_t SilentMonster = 0x160;
    if (actor.type == SilentMonster)
        return {};
    return Core::Unexpected(std::string("0x4f5552: el golpe necesita la tabla de sonidos del monstruo"));
}
} // namespace
Core::Expected<void, std::string> SetActorShock(ActorState& actor, std::uint16_t damage,
                                                const WorldEntryResources& resources)
{
    if (IgnoresShock(actor, damage))
        return {};
    const auto* skeleton = resources.models.SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4f53a0: falta el esqueleto del personaje golpeado"));
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        if (actor.animation.action < 3 || actor.animation.action > 4)
            Render::Models::SetAction(actor.animation, *skeleton, MonsterShockAction);
    }
    else
    {
        const bool fenrir = actor.pet.model == Fenrir;
        Render::Models::SetAction(actor.animation, *skeleton, fenrir ? FenrirAction(actor) : PlayerShockAction);
        if (fenrir && resources.cloth.random.Next() % 3 == 0)
            resources.world.Runtime().lastSoundId = 0x27b + (resources.cloth.random.Next() & 1);
        actor.movement = 0;
    }
    if (auto sounded = HitSound(actor, resources); !sounded.HasValue())
        return sounded;
    constexpr std::uint16_t SparkMonster = 0x18f;
    if (actor.type == SparkMonster)
        return Core::Unexpected(std::string("0x4f55bb: las cinco chispas del monstruo no están portadas"));
    return {};
}
} // namespace Client::Scenes
