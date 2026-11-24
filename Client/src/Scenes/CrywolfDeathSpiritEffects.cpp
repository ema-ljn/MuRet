#include "Scenes/CrywolfDeathSpiritEffects.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include <glm/ext/vector_float3.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
namespace Client::Scenes
{
namespace
{
using Math::F32;
bool HasRoom(const World::ModelEffects& effects)
{
    return std::any_of(effects.Effects().begin(), effects.Effects().end(), [](const auto& e) { return !e.alive; });
}
} // namespace
Core::Expected<std::optional<std::size_t>, std::string>
CreateCrywolfDeathSpirit(const World::ModelEffectArguments& args, World::ModelEffects& effects,
                         World::Particles* particles, Crt::CrtRandom& random, float clock)
{
    const auto slot = effects.Begin(args);
    if (!slot)
        return slot;
    auto& e = effects.Effects()[*slot];
    if (args.subtype == 0)
    {
        e.life = 30;
        e.turnDirection = 1;
        e.speed = 10;
        e.objectState.hiddenMesh = 1;
        e.transform.scale = .3f;
        e.objectState.blendMesh = -1;
        e.alpha = .8f;
        e.light = glm::vec3(1);
        e.velocity = {0, -26, 0};
        e.start = args.light;
        e.copiedAngles = args.angles;
        e.transform.angles = {0, 0, args.angles.z};
    }
    else if (args.subtype == 1)
    {
        const auto* owner = static_cast<const ActorState*>(args.owner);
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x51c9af: falta el dueño del eco147/1"));
        e.life = owner->dword60;
        e.objectState.hiddenMesh = 1;
        e.transform.scale = .2f;
        e.objectState.blendMesh = -1;
        // 51c9ce resta entera con wrap antes de FILD.
        const auto remaining = static_cast<std::int32_t>(20u - static_cast<std::uint32_t>(e.life));
        e.alpha = F32(static_cast<double>(remaining) * static_cast<double>(.2f));
        e.light = {.3f, .4f, 1};
        e.velocity = {};
        if (particles == nullptr)
            return Core::Unexpected(std::string("0x51ca22: falta el pool de partículas del eco147/1"));
        const auto fire =
            particles->CreateChecked(0x5db, e.transform.position, e.transform.angles, e.light, 0, 1, clock, random);
        if (!fire.HasValue())
            return Core::Unexpected(std::string("0x51ca22: no se pudo crear el fuego5db/0 del eco"));
    }
    return slot;
}
Core::Expected<void, std::string> StepCrywolfDeathSpirit(World::ModelEffect& e, Crt::CrtRandom& random,
                                                         const World::ModelEffectStepServices& services,
                                                         std::optional<glm::vec3> point)
{
    if (e.subtype == 1)
    {
        if (e.life < 5)
            e.alpha = F32(static_cast<double>(e.alpha) * static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)));
        return {};
    }
    if (e.subtype != 0 || e.ownerContext == nullptr)
        return {};
    const auto* owner = static_cast<const ActorState*>(e.ownerContext);
    glm::vec3 target;
    for (int axis = 0; axis < 3; ++axis)
        target[axis] = F32(static_cast<double>(owner->position[axis]) + e.start[axis]);
    if (!std::isfinite(e.turnDirection) ||
        static_cast<double>(e.turnDirection) >= std::numeric_limits<std::int32_t>::max())
        return Core::Unexpected(std::string("0x52710e: iteraciones147 fuera del dominio finito del bucle nativo"));
    for (std::int32_t iteration = 1; static_cast<double>(iteration) < static_cast<double>(e.turnDirection); ++iteration)
    {
        if ((random.Next() & 1) == 0)
            e.transform.angles.x = F32(static_cast<double>(e.transform.angles.x) +
                                       (e.transform.angles.x < -90 || std::isnan(e.transform.angles.x) ? 20 : -20));
        // El resultado de distancia sólo llega a un cálculo local muerto de altura; no se usa para mover.
        static_cast<void>(Render::Models::MoveToward(e.transform.position, e.transform.angles, target, e.speed));
        const double accelerated = static_cast<double>(e.speed) + static_cast<double>(.4f);
        e.speed = F32(accelerated);
        if (e.life < 10)
        {
            e.speed = F32(accelerated + static_cast<double>(.1f));
            if (services.effects == nullptr)
                return Core::Unexpected(std::string("0x5271a8: falta el pool de calcos61e/1"));
            if (HasRoom(*services.effects))
            {
                if (!point)
                    return Core::Unexpected(
                        std::string("0x5271a8: el primer61e/1 lee [EBP−28] sin inicializar; falta la captura nativa"));
                const auto child = services.effects->CreatePetSummon(*point, e.transform.angles, e.light, &e, 1);
                if (child)
                    services.effects->Effects()[*child].owner.effect = &e;
            }
        }
        point =
            Render::Models::OriginalVectorRotate(e.velocity, Render::Models::OriginalAngleMatrix(e.transform.angles));
        for (int axis = 0; axis < 3; ++axis)
            e.transform.position[axis] = F32(static_cast<double>((*point)[axis]) + e.transform.position[axis]);
        if (services.effects == nullptr)
            return Core::Unexpected(std::string("0x52720c: falta el pool de la estela104/0"));
        static_cast<void>(World::CreateSpiritTrail({.type = 0x104,
                                                    .position = e.transform.position,
                                                    .angles = e.transform.angles,
                                                    .light = e.light,
                                                    .owner = &e},
                                                   *services.effects));
    }
    e.turnDirection = F32(static_cast<double>(e.turnDirection) + static_cast<double>(.1f));
    if (services.sound)
        services.sound(0x1ca);
    return {};
}
} // namespace Client::Scenes
