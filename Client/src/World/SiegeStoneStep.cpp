#include "World/SiegeStoneEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Math/Ftol.h"
#include "World/GameModelEffects.h"
#include "World/BloodCastleParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <algorithm>
#include <cstdint>
#include <string>

namespace Client::World
{
namespace
{
using Math::F32;
using Result = Core::Expected<void, std::string>;
Result ParticleAt(ModelEffect& effect, Particles& particles, Crt::CrtRandom& random, std::int32_t type,
                  std::int32_t subtype, float scale, const glm::vec3& light)
{
    const auto created = particles.CreateChecked(type, effect.transform.position, effect.transform.angles, light,
                                                 subtype, scale, 0.0f, random);
    if (!created.HasValue())
        return Core::Unexpected(std::string("0x527dd2: falla una partícula de las armas del asedio"));
    return {};
}
void Sound(const SiegeStoneStepServices& services, Audio::SoundId id, const glm::vec3& position)
{
    if (services.sound)
        services.sound(id, position);
}
Result Camera(ModelEffect& effect, const SiegeStoneStepServices& services)
{
    if (services.cameraTarget == nullptr)
        return Core::Unexpected(std::string("0x44dc40: falta el destino de cámara del arma"));
    *services.cameraTarget = effect.transform.position;
    return {};
}
Result AreaAction(const SiegeStoneStepServices& services, ModelEffect& effect, float radius, bool all)
{
    const auto& action = all ? services.actorsAction : services.heroAction;
    if (!action)
        return Core::Unexpected(std::string(all ? "0x429e40: falta la acción de los actores alrededor del impacto"
                                                : "0x4295f0: falta la acción del héroe alrededor del impacto"));
    return action(effect.transform.position, radius);
}
Result ImpactVisuals(ModelEffect& effect, Crt::CrtRandom& random, const SiegeStoneStepServices& services)
{
    if (services.effects == nullptr)
        return Core::Unexpected(std::string("0x52817d: falta el pool del cráter de impacto"));
    const ModelEffectArguments crater{
        .type = 0x636, .position = effect.transform.position, .angles = effect.transform.angles, .light = effect.light};
    static_cast<void>(services.effects->CreateCraterEffect(crater));
    Sound(services, Audio::SoundId{0x1eb}, effect.transform.position);
    if (effect.type == SiegeStoneEffect)
    {
        if (!services.collide)
            return Core::Unexpected(std::string("0x429ec0: faltan los objetos del impacto"));
        const auto collided = services.collide(effect, 350, 250, true);
        if (!collided.HasValue())
            return Core::Unexpected(collided.Error());
    }
    if (effect.subtype == 88 && effect.ownerContext == nullptr)
        return AreaAction(services, effect, 350, false);
    if (effect.subtype != 99 && !(effect.subtype == 88 && effect.ownerContext != nullptr))
        return {};
    const ModelEffectArguments wave{.type = ShockWaveEffect,
                                    .position = effect.transform.position,
                                    .angles = effect.transform.angles,
                                    .light = {1, .3f, .1f},
                                    .subtype = 7};
    static_cast<void>(services.effects->CreateCastleShockWave(wave, random));
    return {};
}
Result Contact(ModelEffect& effect, Crt::CrtRandom& random, const SiegeStoneStepServices& services)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x527f44: falta la altura del terreno del arma"));
    auto& position = effect.transform.position;
    const double height = services.terrainHeight(position.x, position.y);
    if ((effect.type == SiegeIceStoneEffect || effect.life < 250) && static_cast<double>(position.z) < height + 300.0 &&
        effect.subtype == 1 && effect.state != 0 && effect.byte89 != 0)
    {
        if (services.notify)
            services.notify(effect.state, effect.byte89);
        effect.state = 0;
        effect.byte89 = 0;
    }
    if (!(static_cast<double>(position.z) < height))
    {
        if (effect.type == SiegeStoneEffect)
        {
            if (!services.collide)
                return Core::Unexpected(std::string("0x429ec0: faltan los objetos del vuelo"));
            const auto collided = services.collide(effect, 200, 250, false);
            if (!collided.HasValue())
                return Core::Unexpected(collided.Error());
            if (collided.Value() && effect.seen)
                Sound(services, Audio::SoundId{0x1eb}, effect.transform.position);
        }
        return {};
    }
    position.z = F32(height - 5.0);
    if (effect.subtype == 0)
    {
        effect.life = 15;
        effect.byte90 = 0;
        effect.subtype = 88;
    }
    else if (effect.subtype == 1)
    {
        effect.life = 40;
        effect.byte90 = 25;
        effect.subtype = 99;
    }
    if (effect.objectState.hiddenMesh != 99 && effect.seen)
    {
        if (auto visual = ImpactVisuals(effect, random, services); !visual.HasValue())
            return visual;
    }
    effect.objectState.hiddenMesh = 99;
    if (height > 150)
    {
        const auto x = Math::OriginalFtolLow(F32(static_cast<double>(position.x) * static_cast<double>(.01f)));
        const auto y = Math::OriginalFtolLow(F32(static_cast<double>(position.y) * static_cast<double>(.01f)));
        if (!x || !y)
            return Core::Unexpected(std::string("0x52836d: celda de impacto fuera de _ftol"));
        if (services.cameraShake == nullptr)
            return Core::Unexpected(std::string("0x5283a6: falta el temblor de impacto"));
        *services.cameraShake = F32(static_cast<double>(random.Next() % 8 - 8) * static_cast<double>(.1f));
        if (*y < 104 && effect.seen)
        {
            if (!services.deform)
                return Core::Unexpected(std::string("0x5f9580: falta deformar el terreno del impacto"));
            if (auto deformed = services.deform(position.x, position.y, *x, *y); !deformed.HasValue())
                return deformed;
            *services.cameraShake = F32(static_cast<double>(*services.cameraShake) * .5);
        }
    }
    return {};
}
Result Flight(ModelEffect& effect, float brightness, Particles& particles, Crt::CrtRandom& random,
              const SiegeStoneStepServices& services)
{
    auto& p = effect.transform.position;
    auto& a = effect.transform.angles;
    p.x = F32(static_cast<double>(p.x) + static_cast<double>(effect.velocity.x) * effect.speed);
    p.y = F32(static_cast<double>(p.y) + static_cast<double>(effect.velocity.y) * effect.speed);
    effect.start.z = F32(static_cast<double>(effect.start.z) + effect.velocity.z);
    if (p.z <= 1000)
    {
        effect.objectState.hiddenMesh = -1;
        if (effect.type == SiegeStoneEffect)
        {
            const glm::vec3 light{brightness, F32(static_cast<double>(brightness) * .5),
                                  F32(static_cast<double>(brightness) * static_cast<double>(.1f))};
            if (auto emitted = ParticleAt(effect, particles, random, FireParticle, 5, 2, light); !emitted.HasValue())
                return emitted;
        }
        else if (random.Next() % 2 == 0)
        {
            if (auto emitted = ParticleAt(effect, particles, random, TorchFlameParticle, 1, 2, glm::vec3(1));
                !emitted.HasValue())
                return emitted;
        }
    }
    else
    {
        p.z = 1000;
        effect.objectState.hiddenMesh = -2;
    }
    effect.speed = F32(static_cast<double>(effect.speed) - static_cast<double>(.45f));
    if (effect.speed < 5)
        effect.speed = 5;
    effect.velocity.z = F32(static_cast<double>(effect.velocity.z) + effect.turnDirection);
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - static_cast<double>(.1f));
    a.x = F32(static_cast<double>(a.x) + 5);
    a.y = F32(static_cast<double>(a.y) + 5);
    if (effect.subtype == 0)
        p.z = effect.start.z;
    else if (effect.ownerContext == services.hero && services.hero != nullptr)
    {
        p.z = std::min(effect.start.z, 800.0f);
        if (auto camera = Camera(effect, services); !camera.HasValue())
            return camera;
    }
    return Contact(effect, random, services);
}
Result PersistentImpact(ModelEffect& effect, Particles& particles, Crt::CrtRandom& random,
                        const SiegeStoneStepServices& services)
{
    if (effect.subtype == 99 && effect.ownerContext == services.hero && services.hero != nullptr)
    {
        if (auto camera = Camera(effect, services); !camera.HasValue())
            return camera;
    }
    if (effect.life <= effect.byte90)
        return {};
    const glm::vec3 light = effect.type == SiegeStoneEffect ? glm::vec3{1, .6f, .2f} : effect.light;
    if (auto emitted = ParticleAt(effect, particles, random, TorchFlameParticle, 0, 1, light); !emitted.HasValue())
        return emitted;
    if (effect.type == SiegeStoneEffect)
    {
        if (auto emitted = ParticleAt(effect, particles, random, FireParticle, 2, 2, light); !emitted.HasValue())
            return emitted;
    }
    if (effect.subtype == 88 && effect.ownerContext != nullptr)
        return AreaAction(services, effect, static_cast<float>((15 - effect.life) * 20 + 350), false);
    if (effect.subtype == 99)
        return AreaAction(services, effect, static_cast<float>((40 - effect.life) * 7 + 350), true);
    return {};
}
Result Fragment(ModelEffect& effect, Particles& particles, Crt::CrtRandom& random,
                const SiegeStoneStepServices& services)
{
    auto& p = effect.transform.position;
    auto& a = effect.transform.angles;
    p.z = F32(static_cast<double>(p.z) - effect.turnDirection);
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) + static_cast<double>(1.9f));
    a.x = F32(static_cast<double>(a.x) + random.Next() % 20 + 20);
    a.y = F32(static_cast<double>(a.y) + random.Next() % 5);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x527cc9: falta la altura del fragmento"));
    const double height = services.terrainHeight(p.x, p.y);
    if (height > p.z)
    {
        p.z = F32(height + 50);
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) * static_cast<double>(-.2f));
        effect.life -= 5;
        a.x = F32(static_cast<double>(a.x) - static_cast<double>(effect.transform.scale) * 128);
        effect.velocity = {0, 5, 0};
    }
    else
        a.x = F32(static_cast<double>(a.x) - static_cast<double>(effect.transform.scale) * 16);
    const auto delta = Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.copiedAngles));
    for (int axis = 0; axis < 3; ++axis)
        p[axis] = F32(static_cast<double>(p[axis]) + delta[axis]);
    if (effect.life < 10)
        effect.alpha = F32(static_cast<double>(effect.alpha) * static_cast<double>(.666666687f));
    if (effect.life % 3 == 0)
        return ParticleAt(effect, particles, random, TorchFlameParticle, 1, .2f, {1, .6f, .2f});
    return {};
}
void Epilogue(ModelEffect& effect, const Models::Skeleton* skeleton)
{
    constexpr std::uint8_t NoAction = 255;
    if (effect.animation.action != NoAction && skeleton != nullptr &&
        effect.animation.action < skeleton->actions.size())
    {
        const auto& action = skeleton->actions[effect.animation.action];
        if (Models::AdvanceAnimation(effect.animation, action, effect.speed, Models::AnimationScene::Other))
        {
            effect.animation.frame = static_cast<float>(action.keyCount);
            effect.animation.action = NoAction;
        }
    }
    --effect.life;
    if (effect.life <= 0)
        effect.alive = false;
}
} // namespace
Core::Expected<void, std::string> StepSiegeStone(ModelEffect& effect, const Models::Skeleton* skeleton,
                                                 Particles& particles, Crt::CrtRandom& random,
                                                 const SiegeStoneStepServices& services)
{
    if (!effect.alive)
        return {};
    const float brightness = StepLuminosity(random.Next(), effect.life);
    const auto before = effect.transform.position;
    if (effect.type == SiegeStoneEffect && effect.subtype == 2)
    {
        if (auto stepped = Fragment(effect, particles, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.subtype <= 1 || effect.subtype == 88 || effect.subtype == 99)
    {
        const auto stepped = effect.subtype <= 1 ? Flight(effect, brightness, particles, random, services)
                                                 : PersistentImpact(effect, particles, random, services);
        if (!stepped.HasValue())
            return stepped;
        if (services.lighting == nullptr)
            return Core::Unexpected(std::string("0x528768: falta la luz dinámica del impacto"));
        Terrain::AddTerrainLight(*services.lighting, effect.transform.position.x, effect.transform.position.y,
                                 {-.5f, -.5f, -.5f}, 4);
    }
    Epilogue(effect, skeleton);
    effect.previousPosition = before;
    return {};
}
} // namespace Client::World
