#include "Actors/ActorShock.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Kernel/ActorState.h"
#include "Scenes/MonsterCreation.h"
#include "Sound/MonsterSounds.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "World/Particles.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors
{
namespace
{
constexpr std::uint16_t Uniria = 0x1c14;
constexpr std::uint16_t Dinorant = 0x1c15;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint8_t PlayerShockAction = 0xc2;
constexpr std::uint8_t MonsterShockAction = 5;
bool IgnoresShock(const Kernel::ActorState& actor, std::uint16_t damage)
{
    const auto action = actor.animation.action;
    return damage == 0 || actor.dying != 0 || actor.pet.model == Uniria || actor.pet.model == Dinorant ||
           actor.pet.model == DarkHorse || action == 0x3e || action == 0x3f || action == 0x44;
}
std::uint8_t FenrirAction(const Kernel::ActorState& actor)
{
    const bool right = actor.hands[0].model != Kernel::NoItemModel;
    const bool left = actor.hands[1].model != Kernel::NoItemModel;
    if (right && left)
        return 0x67;
    if (right)
        return 0x68;
    if (left)
        return 0x69;
    return 0x66;
}
void PlayerHitSound(const Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    const auto requestSound = [&actor, &resources](Audio::SoundId id)
    { resources.world.Runtime().RequestSound(id, actor.position); };
    const Kernel::CharacterClass characterClass = Kernel::BaseClassOf(actor.characterClass);
    if (characterClass == Kernel::CharacterClass::Elf)
    {
        requestSound(Audio::SoundId{0x6d + (random.Next() & 1)});
        return;
    }
    if (characterClass == Kernel::CharacterClass::DarkLord && random.Next() % 5 != 0)
    {
        requestSound(Audio::SoundId{0x1c6});
        return;
    }
    requestSound(Audio::SoundId{0x69 + random.Next() % 3});
}
void HitSound(const Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    // 0x4f5496 looks at C3: unordered also goes into the sound.
    if (actor.animation.frame < 0.0f || actor.animation.frame > 0.0f)
        return;
    const auto requestSound = [&actor, &resources](Audio::SoundId id)
    { resources.world.Runtime().RequestSound(id, actor.position); };
    if (actor.subtype >= 209 && actor.subtype <= 211)
    {
        requestSound(Audio::SoundId{0x7b});
        return;
    }
    if (actor.type == Tables::PlayerSkeletonModel)
    {
        PlayerHitSound(actor, resources);
        return;
    }
    constexpr std::uint16_t SilentMonster = 0x160;
    if (actor.type == SilentMonster)
        return;
    // 0x4f553d..0x4f55a7: the model's WORD +0xae (the third of the ten of 0x612c80, which the creation loads; at 0 on a
    // model without a table or not loaded); at −1, no sound and no rand(). Otherwise rand() & 1 picks +0xae or +0xb0,
    // and the sound is that WORD + 0xd2, + 5 with subtype 9.
    constexpr std::size_t HitSoundWord = 2;
    if (const auto drawn =
            Sound::DrawModelSound(Sound::WrittenSounds(resources.world.Runtime().modelSounds, actor.type), HitSoundWord,
                                  actor.subtype, resources.cloth.random))
        requestSound(Audio::SoundId{*drawn});
}
// 0x4f55bb..0x4f5649: five attempts, even with the pool full, and the sound after all of them.
Core::Expected<void, std::string> CreateShockDust(const Kernel::ActorState& actor,
                                                  const Scenes::WorldEntryResources& resources)
{
    constexpr int Attempts = 5;
    constexpr int GateSpan = 2;
    constexpr int HorizontalSpan = 128;
    constexpr int HorizontalCenter = 64;
    constexpr int VerticalSpan = 50;
    constexpr double Rise = 200.0;
    constexpr Audio::SoundId DustSound{0x86};
    auto& random = resources.cloth.random;
    for (int attempt = 0; attempt < Attempts; ++attempt)
    {
        if (random.Next() % GateSpan != 0)
            continue;
        const float x = static_cast<float>(static_cast<double>(actor.position.x) +
                                           static_cast<double>(random.Next() % HorizontalSpan - HorizontalCenter));
        const float z = static_cast<float>(
            (static_cast<double>(actor.position.z) + static_cast<double>(random.Next() % VerticalSpan)) + Rise);
        const auto created = resources.world.Particles().CreateChecked(
            World::DustParticle, {x, actor.position.y, z}, actor.angles, actor.light, 0, 1.0f, 0.0f, random);
        if (!created.HasValue())
            return Core::Unexpected(
                std::format("0x4f5635: falla el alta de 0x5f3/0 con error {}", static_cast<unsigned>(created.Error())));
    }
    resources.world.Runtime().RequestSound(DustSound);
    return {};
}
} // namespace
std::uint8_t FenrirHitActionOf(const Kernel::ActorState& actor)
{
    return FenrirAction(actor);
}

Core::Expected<void, std::string> SetActorShock(Kernel::ActorState& actor, std::uint16_t damage,
                                                const Scenes::WorldEntryResources& resources)
{
    if (IgnoresShock(actor, damage))
        return {};
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        // 0x4f5475..0x4f5480: 0x4ea030(c, 5) with the actions of the monster's model (the one its creation loaded,
        // not Player.bmd); a model that is not there is the record without actions and sets nothing.
        if (actor.animation.action < 3 || actor.animation.action > 4)
        {
            if (auto set = Scenes::SetMonsterAction(actor, MonsterShockAction, 0x4f5480, resources); !set.HasValue())
                return set;
        }
    }
    else
    {
        const auto* skeleton = resources.models.SkeletonOf(Models::ModelId{actor.type});
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4f53a0: falta Player.bmd para el personaje golpeado"));
        const bool fenrir = actor.pet.model == Fenrir;
        Models::SetAction(actor.animation, *skeleton, fenrir ? FenrirAction(actor) : PlayerShockAction);
        if (fenrir && resources.cloth.random.Next() % 3 == 0)
            resources.world.Runtime().RequestSound(Audio::SoundId{0x27b + (resources.cloth.random.Next() & 1)},
                                                   actor.position);
        actor.movement = 0;
    }
    HitSound(actor, resources);
    constexpr std::uint16_t SparkMonster = 0x18f;
    if (actor.type == SparkMonster)
        return CreateShockDust(actor, resources);
    return {};
}
} // namespace Client::Actors
