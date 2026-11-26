#include "Scenes/ActorShock.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Scenes/ActorState.h"
#include "Scenes/MonsterCreation.h"
#include "Sound/MonsterSounds.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"

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
    const auto requestSound = [&actor, &resources](std::int32_t id)
    { resources.world.Runtime().RequestSound(id, actor.position); };
    constexpr std::uint8_t ClassMask = 7;
    constexpr std::uint8_t ElfClass = 2;
    constexpr std::uint8_t DarkLordClass = 4;
    const auto characterClass = actor.characterClass & ClassMask;
    if (characterClass == ElfClass)
    {
        requestSound(0x6d + (random.Next() & 1));
        return;
    }
    if (characterClass == DarkLordClass && random.Next() % 5 != 0)
    {
        requestSound(0x1c6);
        return;
    }
    requestSound(0x69 + random.Next() % 3);
}
void HitSound(const ActorState& actor, const WorldEntryResources& resources)
{
    // 0x4f5496 mira C3: unordered también entra en el sonido.
    if (actor.animation.frame < 0.0f || actor.animation.frame > 0.0f)
        return;
    const auto requestSound = [&actor, &resources](std::int32_t id)
    { resources.world.Runtime().RequestSound(id, actor.position); };
    if (actor.subtype >= 209 && actor.subtype <= 211)
    {
        requestSound(0x7b);
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
    // 0x4f553d..0x4f55a7: el WORD +0xae del modelo (el tercero de los diez de 0x612c80, que carga el alta; en 0 en un
    // modelo sin tabla o sin cargar); en −1, sin sonido ni rand(). Si no, rand() & 1 elige +0xae o +0xb0, y el sonido
    // es ese WORD + 0xd2, + 5 con el subtipo 9 (CL10).
    constexpr std::size_t HitSoundWord = 2;
    if (const auto drawn =
            Sound::DrawModelSound(Sound::WrittenSounds(resources.world.Runtime().modelSounds, actor.type), HitSoundWord,
                                  actor.subtype, resources.cloth.random))
        requestSound(*drawn);
}
// 0x4f55bb..0x4f5649: cinco intentos, aun con el pool lleno, y el sonido después de todos ellos.
Core::Expected<void, std::string> CreateShockDust(const ActorState& actor, const WorldEntryResources& resources)
{
    constexpr int Attempts = 5;
    constexpr int GateSpan = 2;
    constexpr int HorizontalSpan = 128;
    constexpr int HorizontalCenter = 64;
    constexpr int VerticalSpan = 50;
    constexpr double Rise = 200.0;
    constexpr std::int32_t DustSound = 0x86;
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
std::uint8_t FenrirHitActionOf(const ActorState& actor)
{
    return FenrirAction(actor);
}

Core::Expected<void, std::string> SetActorShock(ActorState& actor, std::uint16_t damage,
                                                const WorldEntryResources& resources)
{
    if (IgnoresShock(actor, damage))
        return {};
    if (actor.type != Tables::PlayerSkeletonModel)
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
        Models::SetAction(actor.animation, *skeleton, fenrir ? FenrirAction(actor) : PlayerShockAction);
        if (fenrir && resources.cloth.random.Next() % 3 == 0)
            resources.world.Runtime().RequestSound(0x27b + (resources.cloth.random.Next() & 1), actor.position);
        actor.movement = 0;
    }
    HitSound(actor, resources);
    constexpr std::uint16_t SparkMonster = 0x18f;
    if (actor.type == SparkMonster)
        return CreateShockDust(actor, resources);
    return {};
}
} // namespace Client::Scenes
