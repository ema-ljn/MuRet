#include "Scenes/CastleWeaponEffects.h"
#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Network/PacketWriter.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Models/Cloth.h"
#include "Actors/ActionSend.h"
#include "Actors/ActorShock.h"
#include "Kernel/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/MonsterCreation.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/SiegeStoneEffects.h"
#include "Terrain/SiegeTerrain.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"
#include <glm/ext/vector_float3.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;
Result Fire(std::uint8_t kind, const glm::vec3& origin, const glm::vec3& target, const glm::vec3& light,
            std::int32_t subtype, std::uint8_t hi, std::uint8_t lo, const WorldEntryResources& resources)
{
    if (kind != 1 && kind != 2)
        return {};
    auto& hero = resources.world.Hero();
    const World::ModelEffectArguments arguments{.type =
                                                    kind == 1 ? World::SiegeStoneEffect : World::SiegeIceStoneEffect,
                                                .position = origin,
                                                .angles = target,
                                                .light = light,
                                                .subtype = subtype,
                                                .owner = &hero,
                                                .key = 1,
                                                .byte89 = lo,
                                                .byte88 = hi};
    const auto created =
        World::CreateSiegeStone(arguments, resources.world.Map().effects, hero.position, resources.cloth.random);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}
bool InRange(const glm::vec3& a, const glm::vec3& b, float radius)
{
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return !(std::sqrt(dx * dx + dy * dy) > radius);
}
Result KnockDownTheHero(const glm::vec3& point, float radius, const WorldEntryResources& resources)
{
    auto& hero = resources.world.Hero();
    constexpr std::uint8_t Knockdown = 137;
    if (hero.animation.action == Knockdown || !InRange(point, hero.position, radius))
        return {};
    const bool fenrir = hero.pet.model == 0x1c37;
    if (auto set = SetMonsterAction(hero, fenrir ? FenrirHitActionOf(hero) : Knockdown, 0x4295f0, resources);
        !set.HasValue())
        return set;
    // 429724 pasa5d alwireaunquelaacciónlocaldelFenrirseeligecon4e9cd0.
    const auto direction = ActionDirection(hero.angles.z);
    if (!direction)
        return Core::Unexpected(std::string("4295f0: ángulodeacciónfuera de_ftol"));
    if (resources.sendGamePacket)
        resources.sendGamePacket(Protocol::Movement::WriteAction(*direction, fenrir ? 0x5d : Knockdown));
    return {};
}
Result KnockDownTheActors(const glm::vec3& point, float radius, const WorldEntryResources& resources)
{
    auto actors = resources.world.Actors();
    for (std::size_t slot = 0; slot < 400; ++slot)
    {
        auto& actor = actors[slot];
        if (!actor.alive || actor.byte88 != 0x80 || actor.type != Tables::PlayerSkeletonModel ||
            actor.animation.action == 137 || !InRange(point, actor.position, radius))
            continue;
        if (auto set = SetMonsterAction(actor, 137, 0x429e40, resources); !set.HasValue())
            return set;
    }
    return {};
}
} // namespace
Core::Expected<void, std::string> ApplyTheSiegeWeaponFire(std::uint16_t key, std::uint8_t kind, std::uint8_t mode,
                                                          std::uint8_t x, std::uint8_t y,
                                                          const WorldEntryResources& resources)
{
    const auto slot = ActorWithKey(resources.world, key);
    // WorldState retiene el lugar400sentinela que5110d0 devuelve; no se fabricaactor0.
    auto& actor = resources.world.Actors()[slot];
    if (auto set = SetMonsterAction(actor, 1, 0x44eec6, resources); !set.HasValue())
        return set;
    const bool aimed = mode == 1;
    const glm::vec3 target{static_cast<float>(x * 100), static_cast<float>(y * 100), 100};
    if (auto fired = Fire(kind, {actor.position.x, actor.position.y, 500}, target, actor.light, aimed ? 1 : 0,
                          aimed ? static_cast<std::uint8_t>(key >> 8) : 0, aimed ? static_cast<std::uint8_t>(key) : 0,
                          resources);
        !fired.HasValue())
        return fired;
    resources.world.Runtime().RequestSound(0x1ea);
    return {};
}
Core::Expected<void, std::string> ApplyTheSiegeWeaponImpact(std::uint8_t kind, std::uint8_t x, std::uint8_t y,
                                                            const WorldEntryResources& resources)
{
    const glm::vec3 target{static_cast<float>(x * 100), static_cast<float>(y * 100), 100};
    if (auto fired = Fire(kind, kind == 1 ? glm::vec3{9200, 3000, 500} : glm::vec3{9400, 19000, 500}, target,
                          resources.world.Hero().light, 0, 0, 0, resources);
        !fired.HasValue())
        return fired;
    auto& hero = resources.world.Hero();
    const World::ModelEffectArguments wave{.type = World::ShockWaveEffect,
                                           .position = target,
                                           .angles = hero.angles,
                                           .light = {1, .3f, .1f},
                                           .subtype = 6};
    static_cast<void>(resources.world.Map().effects.CreateCastleShockWave(wave, resources.cloth.random));
    resources.world.Runtime().RequestSound(0x1ea);
    return {};
}
World::SiegeStoneStepServices SiegeStoneServicesOf(const WorldEntryResources& resources)
{
    auto& world = resources.world;
    World::SiegeStoneStepServices services{
        .effects = &world.Map().effects,
        .lighting = &world.DynamicLighting().Lighting(),
        .hero = &world.Hero(),
        .cameraTarget = &resources.cameraInput.effectTarget,
        .cameraShake = &resources.cameraInput.shake,
        .collide = [&world](World::ModelEffect& projectile, float radius, float height, bool mark)
        { return World::CollideTheSiegeObject(projectile, world.Map().objects, radius, height, mark); },
        .heroAction = [&resources](const glm::vec3& point, float radius)
        { return KnockDownTheHero(point, radius, resources); },
        .actorsAction = [&resources](const glm::vec3& point, float radius)
        { return KnockDownTheActors(point, radius, resources); },
        .notify =
            [&resources](std::uint8_t hi, std::uint8_t lo)
        {
            if (resources.sendGamePacket)
                resources.sendGamePacket(Protocol::Xor32C1(
                    Network::PacketWriter::C1(Protocol::SiegeWeaponOpcode, Protocol::SiegeWeapon::UseSubcode)
                        .PutU8(hi)
                        .PutU8(lo)));
        },
        .deform = [&world](float x, float y, std::int32_t tx, std::int32_t ty)
        { return World::DeformTheSiegeTerrain(world.Map().terrain, x, y, tx, ty); },
        .sound = [&world](std::int32_t sound, const glm::vec3& position)
        { world.Runtime().RequestSound(sound, position, false); }};
    const auto& terrain = world.Map().terrain;
    if (terrain.heights && terrain.attributes)
        services.terrainHeight = [&terrain, &resources](float x, float y)
        { return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, resources.camera.fixedHeight); };
    return services;
}
} // namespace Client::Scenes
