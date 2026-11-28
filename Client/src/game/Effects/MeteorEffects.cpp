#include "Effects/MeteorEffects.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/FaunaModels.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Cotejado con el PE c32bbd5f: 0x51c727 y 0x51efdc (las altas), 0x525f6a (el paso del 0x13f) y 0x5260bd (el del
// 0x13d).
namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

// 0x525f7b..0x526028: un joint 0x60f/0x18 de la lluvia, con sus dos rand() del lugar antes de crearlo.
Core::Expected<void, std::string> DropTheRain(const Effects::ModelEffect& effect, int drop,
                                              const HighEffectStepContext& context)
{
    constexpr float Pitch = -10.0f;                    // 0xc1200000
    constexpr double Turn = 10.0;                      // 0x6b87a8, 10,0f
    constexpr double Lift = 100.0;                     // 0x6b86b4, 100,0f
    constexpr int Spread = 600;                        // 0x258
    constexpr int ForwardShift = 200, SideShift = 400; // 0xc8 y 0x190
    constexpr std::int32_t RainJoint = 0x60f, RainSubtype = 0x18;
    constexpr float RainWidth = 90.0f; // 0x42b40000
    const glm::vec3 angles{Pitch, 0.0f, F32(static_cast<double>(drop) * Turn)};
    glm::vec3 point{effect.transform.position.x, effect.transform.position.y,
                    F32(D(effect.transform.position.z) + Lift)};
    point.x = F32(static_cast<double>(context.random.Next() % Spread - ForwardShift) + D(point.x));
    point.y = F32(static_cast<double>(context.random.Next() % Spread - SideShift) + D(point.y));
    return CreateHighJoint(context.world, context.random, RainJoint, point, point, angles, RainSubtype, nullptr,
                           RainWidth, 0x526028);
}

// 0x5260bd..0x526103: la luz dinámica (v, v, v + 0,1) de radio 10, con v = FSIN(vida · π/120) · −0,5 en la x87 (el
// FMUL por un float negativo de potencia de dos, exacto: el producto retenido de SineDoubleProductPC53).
Core::Expected<void, std::string> LightTheMeteor(const Effects::ModelEffect& effect,
                                                 const HighEffectStepContext& context)
{
    constexpr double Pulse = 0.02617993950843811; // 0x6ba250
    constexpr double Depth = -0.5;                // 0x6ba298, −0,5f
    constexpr double Blue = 0.10000000149011612;  // 0x6ba248
    constexpr std::int32_t Radius = 10;
    const auto wave = Math::Detail::SineDoubleProductPC53(static_cast<double>(effect.life) * Pulse, Depth);
    if (!wave)
        return Core::Unexpected(std::string("0x5260d4: el seno de la vida del 0x13d fuera del perfil PC53"));
    const glm::vec3 color{F32(*wave), F32(*wave), F32(*wave + Blue)};
    return WorldObjects::AddDynamicLight(context.services.lighting, effect.transform.position, color, Radius,
                                         "0x5260fe");
}

// 0x52614a..0x526207: la piedrita 0xc8 o 0xc9, rand() % 150 + 300 adelante con el giro rand() % 360 en z, y el tipo.
void DropTheStone(const Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr int ReachSpan = 150;           // 0x96
    constexpr double Reach = 300.0;          // 0x6b8900, 300,0f
    constexpr int FullTurn = Math::FullTurn; // 0x168
    constexpr std::int16_t Stone = 0xc8;
    glm::vec3 forward{0.0f};
    forward.y = F32(static_cast<double>(context.random.Next() % ReachSpan) + Reach);
    const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(context.random.Next() % FullTurn)};
    const glm::vec3 offset = Math::OriginalVectorRotate(forward, Math::OriginalAngleMatrix(turn));
    const glm::vec3& position = effect.transform.position;
    const glm::vec3 point{F32(D(offset.x) + D(position.x)), F32(D(offset.y) + D(position.y)),
                          F32(D(offset.z) + D(position.z))};
    const auto type = static_cast<std::int16_t>(Stone + (context.random.Next() & 1));
    static_cast<void>(context.world.Map().effects.CreateStoneChip(type, point, effect.transform.angles, effect.light,
                                                                  context.random));
}

// 0x52620f..0x52624f: 0x4eb860 con el modelo del 0x13d (0x5769ad8 + tipo · 0xe0) sobre el buffer global de la pose
// (0x68b181c, PoseCarry), con la acción publicada del modelo (+0xa0, que pasa a 0 fuera de sus acciones), el frame
// (+0x114), el anterior (+0x118) y su acción (+0x112), los ángulos (+0x1c), los de la cabeza (+0x28 y +0x2c) y las
// banderas (0, 0). Sin acciones no hace nada; si no, deja en el modelo el frame y su key (+0xa4 y +0xa8), como
// PoseWorldModel.
Core::Expected<void, std::string> PoseTheMeteor(const Effects::ModelEffect& effect, const Models::Skeleton& skeleton,
                                                Kernel::WorldState& world, Assets::FaunaModels* faunaModels)
{
    auto& carry = world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), Math::BoneTransform(0.0f));
    if (skeleton.actions.empty())
        return {};
    const auto type = static_cast<std::uint16_t>(effect.type);
    Kernel::SharedModelPlacement placement = world.ModelPlacement(type);
    if (placement.action >= skeleton.actions.size())
    {
        placement.action = 0;
        world.PlaceModel(type, placement);
    }
    const Models::AnimationState animation{.action = placement.action,
                                           .frame = effect.animation.frame,
                                           .previousAction = effect.animation.previousAction,
                                           .previousKey = effect.animation.previousKey};
    // Con las banderas (0, 0) la raíz sólo gira: la escala y la posición del modelo no entran en la pose.
    const Models::ObjectTransform transform{.angles = effect.transform.angles};
    const glm::vec2 head{effect.copiedAngles.x, effect.copiedAngles.y};
    if (!Models::PoseSkeleton(skeleton, animation, transform, head,
                              std::span(carry.bones).first(skeleton.bones.size())))
        return Core::Unexpected(std::string("0x52624f: no se pudo posar el modelo del 0x13d"));
    const auto whole = Math::OriginalFtolLow(D(effect.animation.frame));
    if (!whole)
        return Core::Unexpected(std::string("0x4eb8bf: el frame del 0x13d no tiene conversión nativa observada"));
    auto key = static_cast<std::int16_t>(*whole);
    if (key < 0 || key >= skeleton.actions[placement.action].keyCount)
        key = 0;
    if (faunaModels == nullptr)
        return Core::Unexpected(std::string("0x4eb860: no fauna models (0x5769ad8) for the meteor"));
    faunaModels->RecordAnimation(type, effect.animation.frame, key);
    return {};
}

// 0x526254..0x526331: por cada hueso que no es dummy, rand() % 12; con 0, su punto (0x4ec110(modelo, hueso, 0, punto,
// 1): la escala y el origen que el dibujo del efecto dejó en el modelo, PoseCarry::models, o los de la tabla en cero)
// y las dos partículas.
Core::Expected<void, std::string> SparkTheBones(const Effects::ModelEffect& effect, const Models::Skeleton& skeleton,
                                                const HighEffectStepContext& context)
{
    constexpr int SparkOdds = 12; // 0xc
    constexpr std::int32_t Shard = 0x6ec, ShardSubtype = 3, Spark = 0x5f2, SparkSubtype = 0x15;
    constexpr float ShardScale = 4.0f; // 0x40800000
    const glm::vec3 white(1.0f);
    const auto& carry = context.world.PoseCarry();
    const auto placed = carry.models.find(static_cast<std::uint16_t>(effect.type));
    const float scale = placed != carry.models.end() ? placed->second.scale : 0.0f;
    const glm::vec3 origin = placed != carry.models.end() ? placed->second.position : glm::vec3(0.0f);
    // 0x5262cf: push ebx, el efecto, como dueño de la 0x6ec/3; la 0x5f2/0x15 va sin dueño (0x5262ed: push 0).
    const auto pool = context.world.Map().effects.Effects();
    const auto found =
        std::ranges::find_if(pool, [&](const Effects::ModelEffect& candidate) { return &candidate == &effect; });
    if (found == pool.end())
        return Core::Unexpected(std::string("0x5262cf: el 0x13d fuera de su pool durable"));
    const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::ModelEffect,
                                         static_cast<std::uint64_t>(found - pool.begin())};
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone)
    {
        if (skeleton.bones[bone].dummy || context.random.Next() % SparkOdds != 0)
            continue;
        const glm::vec3 point = Math::OriginalTransformPosition(
            Math::OriginalMatrixOf(context.world.PoseCarry().bones[bone]), glm::vec3(0.0f), scale, origin);
        // La 0x6ec/3 con el efecto de dueño: su paso busca la mano del Monster82 en la lista de 0x401000, donde el
        // efecto no está, y sigue como la fanta común (0x547c28).
        if (auto shard = CreateHighParticle(context.world, context.random, context.worldClock, Shard, point,
                                            effect.transform.angles, white, ShardSubtype, ShardScale, 0x5262e8, owner);
            !shard.HasValue())
            return Core::Unexpected(shard.Error());
        if (auto spark = CreateHighParticle(context.world, context.random, context.worldClock, Spark, point,
                                            effect.transform.angles, white, SparkSubtype, 1.0f, 0x526307);
            !spark.HasValue())
            return Core::Unexpected(spark.Error());
    }
    return {};
}
} // namespace

void BeginTheMeteor(Effects::ModelEffect& effect)
{
    if (effect.type == MeteorCallEffect)
    {
        constexpr std::int32_t CallLife = 20; // 0x51efdd
        effect.life = CallLife;
        return;
    }
    constexpr std::int32_t MeteorLife = 120;                         // 0x51c727
    constexpr float MeteorScale = std::bit_cast<float>(0x3f4ccccdu); // 0x51c72f, 0,8f
    effect.life = MeteorLife;
    effect.transform.scale = MeteorScale;
}

Core::Expected<void, std::string> StepTheMeteorCall(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t RainLife = 8, FallLife = 2;
    if (effect.life == RainLife)
    {
        constexpr int Drops = 16; // 0x10
        for (int drop = 0; drop < Drops; ++drop)
            if (auto made = DropTheRain(effect, drop, context); !made.HasValue())
                return made;
        return {};
    }
    if (effect.life != FallLife)
        return {};
    constexpr int FullTurn = Math::FullTurn; // 0x168
    const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(context.random.Next() % FullTurn)};
    const HighEffectContext creation{.world = context.world,
                                     .random = context.random,
                                     .worldClock = context.worldClock,
                                     .models = context.models,
                                     .terrain = context.terrain,
                                     .faunaModels = context.faunaModels};
    for (const auto& [type, address] : {std::pair{MeteorEffect, 0x526090u}, std::pair{MeteorShellEffect, 0x5260adu}})
    {
        const auto made = CreateHighModelEffect({.type = type,
                                                 .position = effect.transform.position,
                                                 .angles = angles,
                                                 .light = effect.light,
                                                 .owner = &effect},
                                                creation);
        if (!made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    }
    effect.alive = false; // 0x5260b5
    return {};
}

Core::Expected<void, std::string> StepTheMeteor(Effects::ModelEffect& effect, const Models::Skeleton* skeleton,
                                                const HighEffectStepContext& context)
{
    if (auto lit = LightTheMeteor(effect, context); !lit.HasValue())
        return lit;
    constexpr int ShakeMask = 3, ShakeCenter = 2;
    constexpr double ShakeStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0,1f
    const int shake = (context.random.Next() & ShakeMask) - ShakeCenter;
    if (context.services.cameraShake == nullptr)
        return Core::Unexpected(std::string("0x526126: el 0x13d escribe el temblor de la cámara (0x7d325c8)"));
    *context.services.cameraShake = F32(static_cast<double>(shake) * ShakeStep);
    if ((context.random.Next() & 1) == 0)
        DropTheStone(effect, context);
    // Sin el BMD del 0x13d (0x5769ad8 en cero): sin acciones ni huesos, ni la pose ni las chispas.
    if (skeleton == nullptr)
        return {};
    if (auto posed = PoseTheMeteor(effect, *skeleton, context.world, context.faunaModels); !posed.HasValue())
        return posed;
    return SparkTheBones(effect, *skeleton, context);
}

Core::Expected<bool, std::string> DrawTheMeteorBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                    Effects::ModelEffect& effect,
                                                    const Actors::Drawing::DrawnObject& object,
                                                    Core::Duration frameClock, std::optional<float> worldClock)
{
    if (effect.type != MeteorEffect && effect.type != MeteorShellEffect)
        return false;
    constexpr float VisibleAlpha = 0.01f; // 0x6b88d4, el de 0x5ffdf0
    constexpr std::uint32_t BodyFlags = 2;
    constexpr Render::TextureSlot RockTexture{0x63c};
    std::vector<Models::SkinnedMesh> posed;
    std::span<const Models::SkinnedMesh> meshes = object.retainedMeshes;
    if (meshes.empty())
    {
        // Sin el scratch publicado, el de 0x5ffdf0 con el segundo argumento en 0 (0x52da99 → 0x602e90(efecto, 0, …)):
        // WorldScratchOf, la normal girada por R · s. Nada sin el modelo, su esqueleto o la pose.
        if (object.alpha < VisibleAlpha)
            return true;
        posed = Actors::Drawing::WorldScratchOf(models, object);
        if (posed.empty())
            return true;
        meshes = posed;
    }
    Actors::Drawing::DrawnObject pass = object;
    pass.color = glm::vec3(1.0f);
    pass.state.light = 1.0f;
    if (effect.type == MeteorEffect)
    {
        // 0x443598..0x4435d4: la U y la V del efecto en los dos.
        pass.state.blendMesh = -1;
        if (auto drawn = Actors::Drawing::DrawItemMeshPass(batch, models, pass, frameClock, meshes, 0, BodyFlags);
            !drawn.HasValue())
            return Core::Unexpected("0x4435b2: " + drawn.Error());
        pass.state.blendMesh = 0;
        if (auto drawn =
                Actors::Drawing::DrawItemMeshPass(batch, models, pass, frameClock, meshes, 0, BodyFlags, RockTexture);
            !drawn.HasValue())
            return Core::Unexpected("0x4435d4: " + drawn.Error());
        return true;
    }
    // 0x443605..0x44362a: FLD T, FMUL 0,00008f (0x6b8be8), FSIN, FMUL 2,5f (0x6b89a8) y FST a +0x6c.
    if (!worldClock)
        return Core::Unexpected(std::string("0x443605: la cáscara 0x13e pide el reloj del mundo (0x5767b88)"));
    constexpr float Drift = std::bit_cast<float>(0x38a7c5acu); // 0x6b8be8
    constexpr float Sway = 2.5f;                               // 0x6b89a8
    const auto sway = Math::Detail::SineProductPC53(D(*worldClock) * D(Drift), Sway);
    if (!sway)
        return Core::Unexpected(std::string("0x44361c: el seno del reloj fuera del perfil PC53"));
    effect.objectState.uv.x = F32(*sway);
    pass.state.blendMesh = 0;
    pass.state.uv.x = effect.objectState.uv.x;
    if (auto drawn = Actors::Drawing::DrawItemMeshPass(batch, models, pass, frameClock, meshes, 0, BodyFlags);
        !drawn.HasValue())
        return Core::Unexpected("0x443636: " + drawn.Error());
    effect.objectState.uv.x = 1.0f; // 0x44363d
    return true;
}
} // namespace Client::Effects
