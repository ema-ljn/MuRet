#include "Scenes/FaunaEffects.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaStep.h"
#include "Scenes/FaunaModels.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <glm/ext/vector_float3.hpp>
#include <string>
#include <span>
#include <system_error>
namespace Client::Scenes
{
std::optional<std::size_t> CreateFaunaLightning(World::ModelEffects& effects, const glm::vec3& position)
{
    auto pool = effects.Effects();
    for (std::size_t i = 0; i < pool.size(); ++i)
    {
        auto& effect = pool[i];
        if (effect.alive)
            continue;
        effect.alive = true;
        effect.type = 0xc2;
        effect.subtype = 3;
        effect.vertexLight = true;
        effect.objectState.hiddenMesh = -1;
        effect.objectState.blendMesh = 1;
        effect.objectState.light = 1;
        effect.objectState.uv = {};
        effect.animation.frame = 0;
        effect.animation.previousKey = 0;
        effect.animation.action = 0;
        effect.alpha = 1;
        effect.transform.scale = .3f;
        effect.transform.position = position;
        effect.transform.angles = {};
        effect.light = {1, 1, 1};
        effect.state = 0;
        effect.speed = .3f;
        effect.life = 80;
        effect.velocity = {0, -12, 0};
        effect.previousPosition = position;
        return i;
    }
    return std::nullopt;
}
Core::Expected<void, std::string> StepFaunaLightning(World::ModelEffect& effect, std::size_t physicalIndex,
                                                     const Render::Models::Skeleton* skeleton,
                                                     World::Particles& particles, World::FrameSprites& sprites,
                                                     World::TerrainLighting& lighting,
                                                     Render::Models::ClothWorld& cloth)
{
    if (!effect.alive)
        return {};
    if (effect.type != 0xc2 || effect.subtype != 3)
        return Core::Unexpected(std::string("5247b1: familia de rayo no portada"));
    const auto f32 = [](double value) { return static_cast<float>(value); };
    // 5208c5..52091a: un sorteo para todo efecto vivo, antes del dispatch.
    float light = f32(static_cast<double>(cloth.random.Next() % 4 + 7) * .1f);
    if (effect.life < 5)
        light = std::max(f32(static_cast<double>(light) - static_cast<double>(5 - effect.life) * .2f), 0.0f);
    effect.objectState.hiddenMesh = 0;
    effect.objectState.light = 0;
    const std::array<float, 3> terrainLight{light, f32(static_cast<double>(light) * .1f), 0};
    World::AddTerrainLight(lighting, effect.transform.position.x, effect.transform.position.y, terrainLight, 2);
    const auto steps = cloth.logicSteps;
    if (cloth.openCounterAccesses == 0)
        for (int byte = 0; byte < 4; ++byte)
            static_cast<void>(cloth.random.Next());
    const auto phase = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(steps) +
                                                   5371U * static_cast<std::uint32_t>(physicalIndex));
    const auto direction = Render::Models::Detail::FaunaDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(std::string("53d1a0: dirección fuera del perfil PC53"));
    glm::vec3 position;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float offset = f32(static_cast<double>((*direction)[static_cast<std::size_t>(axis)]) * 50);
        position[axis] = f32(static_cast<double>(effect.transform.position[axis]) + offset);
    }
    const glm::vec3 white{1, 1, 1};
    const auto created =
        particles.CreateChecked(World::FireParticle, position, {}, white, 8,
                                f32(static_cast<double>(effect.transform.scale) * 2.5f), 0, cloth.random);
    if (!created.HasValue())
        return Core::Unexpected(std::string("525102: no se pudo crear fuego 5d9/8"));
    const float rotation = static_cast<float>(cloth.random.Next() % 360);
    static_cast<void>(
        sprites.Create(0x5fd, position, f32(static_cast<double>(effect.transform.scale) * 3), white, rotation, 0));
    World::StepModelEffect(effect, skeleton);
    return {};
}
FaunaStepEffects BindFaunaStepEffects(FaunaModels& models, FaunaModelLoadContext load, std::span<JointState> joints,
                                      World::ModelEffects& effects, Render::Models::CrtRandom& random)
{
    FaunaStepEffects result;
    result.effect = [joints, &random, &log = load.log](
                        const World::FishEffectRequest& request) -> Core::Expected<std::uint32_t, std::error_code>
    {
        const auto created = CreateFaunaJoint(joints, request, random);
        if (!created.HasValue())
        {
            log.Warning("{}", created.Error());
            return Core::Unexpected(std::make_error_code(std::errc::function_not_supported));
        }
        return static_cast<std::uint32_t>(created.Value().value_or(0));
    };
    result.lightning = [&effects](const glm::vec3& position)
    {
        static_cast<void>(CreateFaunaLightning(effects, position));
        return std::error_code{};
    };
    result.prepare = [&models, load](std::int32_t monster) mutable
    {
        const auto prepared = models.Prepare(monster, load);
        if (!prepared.HasValue())
        {
            load.log.Warning("{}", prepared.Error());
            return std::make_error_code(std::errc::io_error);
        }
        return std::error_code{};
    };
    return result;
}
} // namespace Client::Scenes
