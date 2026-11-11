#include "Scenes/GameParticles.h"
#include "World/PortableParticleTrigonometry.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/OriginalFtol.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t ScrollPeriod = 1000;
constexpr float ScrollFactor = std::bit_cast<float>(0x3a83126fu); // 0x6b89c8.
constexpr int DropSpread = 32;
constexpr int DropCenter = 16;
constexpr int DropGuard = 2;
constexpr std::int32_t DropSubtype = 0;
constexpr float DropScale = 1.0f;

struct WaterspoutDrop
{
    std::size_t bone;
    float y;
    std::string_view poseAddress;
    std::string_view createAddress;
};

// 0x603053..0x60313f: la segunda pareja de sorteos va DESPUÉS de Create de la primera gota.
constexpr std::array<WaterspoutDrop, 2> Drops{
    {{1, -20.0f, "0x6030a5", "0x6030c7"}, {4, -80.0f, "0x603121", "0x60313a"}}};

Core::Expected<void, std::string> EmitDrop(const WaterspoutDrop& drop, const World::WorldObject& object,
                                           World::Particles& particles, Render::Models::CrtRandom& random,
                                           const float& worldClock, const WaterspoutBonePoint& bonePoint)
{
    const int x = (random.Next() % DropSpread) - DropCenter;
    const int z = (random.Next() % DropSpread) - DropCenter;
    const glm::vec3 offset(static_cast<float>(x), drop.y, static_cast<float>(z));
    if (!bonePoint)
    {
        return Core::Unexpected(
            std::format("{}: falta el buffer global del hueso {} de la fuente", drop.poseAddress, drop.bone));
    }
    const auto position = bonePoint(drop.bone, offset);
    if (!position.HasValue())
    {
        return Core::Unexpected(std::format("{}: {}", drop.poseAddress, position.Error()));
    }
    const auto created = particles.CreateChecked(World::BreathParticle, position.Value(), object.transform.angles,
                                                 object.light, DropSubtype, DropScale, worldClock, random);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de la gota 0x5f2 falla con error {}", drop.createAddress,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<glm::vec3, std::string> PosedDropPoint(const IModelLibrary& models, const DrawnObject& drawn,
                                                      std::size_t bone, const glm::vec3& offset,
                                                      const FaunaDrawCarry* retainedPose)
{
    if (retainedPose != nullptr)
    {
        if (bone >= retainedPose->bones.size())
            return Core::Unexpected(std::format("falta el hueso {} del buffer global prestado", bone));
        // 6030a5/603121 pasan Translate=0: 4ec16d..17e no consulta escala/origen del modelo.
        return Render::Models::OriginalVectorTransform(offset,
                                                       Render::Models::OriginalMatrixOf(retainedPose->bones[bone]));
    }
    // 0x5ffdf0 sale antes de posar con alpha<0,01; los dummy tampoco escriben el buffer global.
    constexpr float VisibleAlpha = 0.01f;
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(drawn.model);
    if (drawn.alpha < VisibleAlpha || models.Find(drawn.model) == nullptr || skeleton == nullptr ||
        bone >= skeleton->bones.size() || skeleton->bones[bone].dummy)
    {
        return Core::Unexpected(std::format("el hueso {} quedó de una pose anterior que MuRet no conserva", bone));
    }
    const std::vector<Render::Models::OriginalMatrix> world = PosedWorldBones(models, drawn);
    if (bone >= world.size())
    {
        return Core::Unexpected(std::format("no se pudo posar el hueso {} de la fuente", bone));
    }
    // 0x4ec179 → 0x5fccc0, Translate=0: el hueso YA está escalado y trasladado al mundo.
    return Render::Models::OriginalVectorTransform(offset, world[bone]);
}
} // namespace

Core::Expected<void, std::string> StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles,
                                                          Render::Models::CrtRandom& random, const float& worldClock,
                                                          const WaterspoutBonePoint& bonePoint)
{
    object.state.light = 1.0f; // 0x603011, incluso cuando el ftol siguiente falla.
    const std::optional<std::int32_t> clock = World::OriginalFtolLow(static_cast<double>(worldClock));
    if (!clock)
    {
        return Core::Unexpected(std::string("0x60301e: ftol del reloj del mundo fuera de lo acreditado"));
    }
    const auto negated = std::bit_cast<std::int32_t>(0u - std::bit_cast<std::uint32_t>(*clock));
    object.state.uv.y =
        static_cast<float>(static_cast<double>(negated % ScrollPeriod) * static_cast<double>(ScrollFactor));
    if (random.Next() % DropGuard != 0)
    {
        return {};
    }
    for (const WaterspoutDrop& drop : Drops)
    {
        const auto emitted = EmitDrop(drop, object, particles, random, worldClock, bonePoint);
        if (!emitted.HasValue())
        {
            return emitted;
        }
    }
    return {};
}

Core::Expected<void, std::string> StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles,
                                                          Render::Models::CrtRandom& random, const float& worldClock,
                                                          const IModelLibrary& models, const DrawnObject& drawn,
                                                          const FaunaDrawCarry* retainedPose)
{
    const auto point = [&models, &drawn, retainedPose](std::size_t bone, const glm::vec3& offset)
    { return PosedDropPoint(models, drawn, bone, offset, retainedPose); };
    return StepGameWaterspoutFrame(object, particles, random, worldClock, point);
}

Core::Expected<void, std::string>
StepGameParticles(World::Particles& particles, Render::Models::CrtRandom& random, const std::int32_t& map,
                  const std::function<double(float, float)>& terrainHeight, const float& worldClock,
                  const std::function<World::WorldObject*(std::uint64_t)>& owners,
                  const std::function<std::optional<std::uint8_t>(std::uint64_t)>& actorActions,
                  const std::function<std::optional<float>(std::int32_t)>& textureHeight)
{
    World::PortableParticleTrigonometry trigonometry;
    const auto stepped = World::StepParticlesChecked(particles, random, std::cref(worldClock), &map, trigonometry,
                                                     terrainHeight, owners, actorActions, textureHeight);
    if (!stepped.HasValue())
    {
        return Core::Unexpected(std::format("0x637128 → 0x5446e0: el paso de las partículas falla con error {}",
                                            static_cast<unsigned>(stepped.Error())));
    }
    return {};
}

std::function<std::optional<float>(std::int32_t)> ParticleTextureHeightsOf(const IModelLibrary& textures)
{
    return [&textures](std::int32_t slot) -> std::optional<float>
    {
        const auto metadata = textures.TextureMetadataOf(static_cast<std::uint32_t>(slot));
        return metadata ? std::optional(metadata->size.y) : std::nullopt;
    };
}

std::function<World::WorldObject*(std::uint64_t)> ParticleOwnersAmong(std::span<World::WorldObject> objects)
{
    return [objects](std::uint64_t identity) -> World::WorldObject*
    {
        for (World::WorldObject& object : objects)
        {
            if (object.identity == identity)
                return &object;
        }
        return nullptr;
    };
}

std::function<std::optional<std::uint8_t>(std::uint64_t)> ParticleActorActionsAmong(std::span<const ActorState> actors)
{
    return [actors](std::uint64_t slot) -> std::optional<std::uint8_t>
    {
        if (slot >= actors.size())
            return std::nullopt;
        return actors[static_cast<std::size_t>(slot)].animation.action;
    };
}

Core::Expected<void, std::string> DrawGameParticles(Render::ModelBatch& batch, World::Particles& particles,
                                                    const IModelLibrary& textures,
                                                    const Render::Models::OriginalMatrix& view, float worldClock,
                                                    std::uint8_t mode)
{
    const auto drawn = DrawParticles(batch, particles, textures, view, worldClock, mode);
    if (!drawn.HasValue())
    {
        const World::ParticleDrawingFailure& failure = drawn.Error();
        return Core::Unexpected(
            std::format("0x637558 → 0x548550: el dibujo se corta en el lugar {}: error {}, fase {}{}", failure.slot,
                        static_cast<unsigned>(failure.error.reason), static_cast<unsigned>(failure.error.phase),
                        failure.error.backendError ? ": " + failure.error.backendError.message() : ""));
    }
    return {};
}
} // namespace Client::Scenes
