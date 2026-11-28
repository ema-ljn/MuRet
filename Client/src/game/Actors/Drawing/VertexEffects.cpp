#include "Actors/Drawing/VertexEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/EffectHooks.h"
#include "Actors/JointHooks.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "CastleSiege/SiegeVertexEffects.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Actors::Drawing
{
namespace
{
using Crt::CrtRandom;
using Result = Core::Expected<void, std::string>;

// The light of 0x4ee770: 1 at the start (0x4ee7b3..0x4ee7c1), 0.2f with 0x10c and 0x131 (0x3e4ccccd) and 0.08f with
// 0x5ac (0x3da3d70a, 0x4eeec8).
constexpr float FullLight = 1.0f;
constexpr float DimLight = std::bit_cast<float>(0x3e4ccccdu);
constexpr float GlowLight = std::bit_cast<float>(0x3da3d70au);

constexpr std::int16_t StoneEffect = 0x10a, StoneShard = 0x10b;
constexpr std::int16_t GateStone = 0x10c, GateShard = 0x10d;
constexpr std::int16_t ShowerEffect = 0x12d, SecondShower = 0x12e, GateEffect = 0x12f;
constexpr std::int16_t SiegeShard = 0x131, SiegeDust = 0x132, SiegeSpark = 0x133;
constexpr std::int16_t SkinShell = 0x137, GolemStones = 0x13c;
constexpr std::int32_t RavenSprite = 0x5ac, FadingParticle = 0x607;

// rand() & mask with the sign fix (AND 0x80000001 or 0x80000007): with a non-negative rand(), the remainder.
std::int32_t Masked(CrtRandom& random, std::int32_t modulus)
{
    return random.Next() % modulus;
}

// 0x518cf0(type, point, angles, light, subtype, 0, −1, 0, 0, 0, 0) of the stones 0x10a and 0x10b (0x51bafb).
void Debris(const IEffectHooks& hooks, World::ModelEffects& effects, std::int16_t type, const glm::vec3& position,
            const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    static_cast<void>(hooks.CreateTheDebris(effects, type, position, angles, light, random, subtype));
}

// The same with the siege fragments and the stones of 0x13c (0x51d2a9, 0x51d5b8).
void Siege(World::ModelEffects& effects, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
           const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    static_cast<void>(World::CreateSiegeVertexEffect(
        {.type = type, .position = position, .angles = angles, .light = light, .subtype = subtype}, effects, random));
}

// The same with the gates' stones 0x10c and 0x10d (0x51b99f).
Result GateDebris(const IEffectHooks& hooks, World::ModelEffects& effects, std::int16_t type, const glm::vec3& position,
                  const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype, CrtRandom& random,
                  std::uint32_t address)
{
    const auto made = hooks.CreateTheBlastEffect(
        effects, {.type = type, .position = position, .angles = angles, .light = light, .subtype = subtype}, random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}

// 0x4ee835..0x4ee9d3: 0x10a.
void ScatterStones(const IEffectHooks& hooks, World::ModelEffects& effects, const glm::vec3& position,
                   const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr std::int32_t Falling = 0, Blue = 1, Green = 2, Mixed = 3;
    constexpr std::int32_t BlueShard = 2, GreenShard = 3, MixedSubtype = 4;
    constexpr int ShardChance = 2, StoneChance = 10;
    switch (subtype)
    {
    case Falling:
        if (Masked(random, ShardChance) == 0)
            Debris(hooks, effects, StoneShard, position, angles, light, 0, random);
        if (random.Next() % StoneChance == 0)
            Debris(hooks, effects, StoneEffect, position, angles, light, 0, random);
        return;
    case Blue:
        Debris(hooks, effects, StoneShard, position, angles, light, BlueShard, random);
        return;
    case Green:
        Debris(hooks, effects, StoneShard, position, angles, light, GreenShard, random);
        return;
    case Mixed:
        // 0x4ee981..0x4ee9cb: the arguments already on the stack and the rand() & 1 last, added to 0x10a.
        Debris(hooks, effects, static_cast<std::int16_t>(StoneEffect + Masked(random, ShardChance)), position, angles,
               light, MixedSubtype, random);
        return;
    default:
        return;
    }
}

// 0x4ee9d8..0x4eeb4e: 0x10c, with the light at 0.2f.
Result ScatterGateStones(const IEffectHooks& hooks, World::ModelEffects& effects, const glm::vec3& position,
                         const glm::vec3& angles, glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr std::int32_t Wide = 0, Narrow = 1, NarrowChildren = 2;
    if (subtype == Narrow)
    {
        light = glm::vec3(DimLight);
        constexpr int ShardChance = 5, StoneChance = 10;
        if (random.Next() % ShardChance == 0)
        {
            if (auto made =
                    GateDebris(hooks, effects, GateShard, position, angles, light, NarrowChildren, random, 0x4eea3a);
                !made.HasValue())
                return made;
        }
        if (random.Next() % StoneChance == 0)
            return GateDebris(hooks, effects, GateStone, position, angles, light, NarrowChildren, random, 0x4eea8a);
        return {};
    }
    if (subtype == Wide)
    {
        light = glm::vec3(DimLight);
        constexpr int ShardChance = 12, StoneChance = 50;
        if (random.Next() % ShardChance == 0)
        {
            if (auto made = GateDebris(hooks, effects, GateShard, position, angles, light, Wide, random, 0x4eeaf7);
                !made.HasValue())
                return made;
        }
        if (random.Next() % StoneChance == 0)
            return GateDebris(hooks, effects, GateStone, position, angles, light, Wide, random, 0x4eeb46);
    }
    return {};
}

// 0x4eeb53..0x4eec6b: 0x12d, 0x12e and 0x12f; the second rand() & 1 chooses between the base type and the next.
void ScatterShower(World::ModelEffects& effects, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                   const glm::vec3& light, CrtRandom& random)
{
    constexpr int Chance = 3, Pair = 2;
    constexpr std::int32_t ShowerSubtype = 1;
    if (random.Next() % Chance != 0)
        return;
    const std::int16_t base = type == GateEffect ? GateEffect : ShowerEffect;
    Siege(effects, static_cast<std::int16_t>(base + Masked(random, Pair)), position, angles, light,
          type == ShowerEffect ? ShowerSubtype : 0, random);
}

// 0x4eec70..0x4eed6a: 0x131, with the light at 0.2f; each addition before the next rand().
void ScatterSiegeShards(World::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                        glm::vec3& light, CrtRandom& random)
{
    constexpr int DustChance = 12, ShardChance = 40, SparkChance = 40;
    light = glm::vec3(DimLight);
    if (random.Next() % DustChance == 0)
        Siege(effects, SiegeDust, position, angles, light, 0, random);
    if (random.Next() % ShardChance == 0)
        Siege(effects, SiegeShard, position, angles, light, 0, random);
    if (random.Next() % SparkChance == 0)
        Siege(effects, SiegeSpark, position, angles, light, 0, random);
}

// 0x4eed94..0x4eee66, 0x13c: one rand() % 45 per corner; with 0 and fewer than 20 already counted ([ebp+8], which does
// not go back to 0 between polygons), /0 creates the 0x13c/0 and /1 the 0x12d/2 and the 0x12e/2, without an owner; with
// any subtype, one more in the count (even if the table is full).
void ScatterGolemStones(World::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                        const glm::vec3& light, std::int32_t subtype, int& counted, CrtRandom& random)
{
    constexpr int Chance = 45, Most = 20;
    constexpr std::int32_t Stones = 0, Shower = 1, ShowerSubtype = 2;
    if (random.Next() % Chance != 0 || counted >= Most)
        return;
    if (subtype == Stones)
    {
        Siege(effects, GolemStones, position, angles, light, 0, random);
    }
    else if (subtype == Shower)
    {
        Siege(effects, ShowerEffect, position, angles, light, ShowerSubtype, random);
        Siege(effects, SecondShower, position, angles, light, ShowerSubtype, random);
    }
    ++counted;
}

// 0x4eee6b..0x4eeec1: 0x137, rand() & 7 and with 0, 0x518cf0(0x137, point, angles, light, subtype, 0, −1, 0, 0, 0, 0):
// the shell of World::CreateSkinShell (with the table full, nothing, and without its rand() calls).
void ScatterSkinShells(const IEffectHooks& hooks, World::ModelEffects& effects, const glm::vec3& position,
                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr int Chance = 8;
    if (Masked(random, Chance) != 0)
        return;
    static_cast<void>(hooks.CreateTheSkinShell(
        effects, {.type = SkinShell, .position = position, .angles = angles, .light = light, .subtype = subtype},
        random));
}

// 0x4eef19..0x4eefca: 0x5ac/1 on the triangles whose index is a multiple of 22.
Result PullTheRavenJoints(const glm::vec3& position, std::int32_t triangle, glm::vec3& angles,
                          const VertexEffectCall& call, const VertexEffectTables& tables, CrtRandom& random)
{
    constexpr int Every = 22, Pitch = 90, Turn = 120, TurnCenter = 60;
    constexpr std::int32_t RavenJoint = 0x613, RavenJointSubtype = 0xd, NoTarget = -1;
    constexpr float RavenJointWidth = 20.0f; // 0x41a00000
    if (triangle % Every != 0)
        return {};
    // 0x4eef4a..0x4eefb2: y at 0, x at −(rand() % 90) (FILD, FCHS) and z at (rand() % 120 − 60) plus the fourth
    // argument's z (FILD, FADD).
    angles.y = 0.0f;
    angles.x = static_cast<float>(-static_cast<double>(random.Next() % Pitch));
    const std::int32_t turn = (random.Next() % Turn) - TurnCenter;
    angles.z = static_cast<float>(static_cast<double>(turn) + static_cast<double>(call.angles.z));
    if (call.target == nullptr)
        return Core::Unexpected(
            std::string("0x4eefc5: el 0x5ac/1 de los vértices sin el personaje del quinto argumento "
                        "(0x530860 lee +0x10 de un puntero nulo)"));
    glm::vec3 first = position;
    glm::vec3 second = call.target->position;
    const Kernel::JointRequest request{.type = RavenJoint,
                                       .firstPosition = first,
                                       .secondPosition = second,
                                       .angles = angles,
                                       .subtype = RavenJointSubtype,
                                       .ownerContext = call.target,
                                       .scale = RavenJointWidth,
                                       .key = 0,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = NoTarget};
    if (tables.jointHooks == nullptr)
        return Core::Unexpected(std::string("0x4eefc5: no joint hooks (0x530860)"));
    const auto made = tables.jointHooks->CreateTheJoint(tables.joints, request, random, Kernel::JointOwnerKind::Actor);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x4eefc5: {}", made.Error()));
    return {};
}

// One corner: the type's case (0x4ee801..0x4ef033).
Result EmitAt(const glm::vec3& position, std::int32_t triangle, glm::vec3& angles, glm::vec3& light, int& counted,
              float modelScale, const VertexEffectCall& call, const VertexEffectTables& tables, CrtRandom& random)
{
    const auto effects = [&]() -> Core::Expected<World::ModelEffects*, std::string>
    {
        if (tables.effects == nullptr)
            return Core::Unexpected(
                std::format("0x518cf0: faltan los efectos con modelo para el {:#x} de 0x4ee770", call.type));
        return tables.effects;
    };
    switch (call.type)
    {
    case StoneEffect:
    case GateStone:
    case ShowerEffect:
    case SecondShower:
    case GateEffect:
    case SiegeShard:
    case SkinShell:
    case GolemStones:
    {
        const auto table = effects();
        if (!table.HasValue())
            return Core::Unexpected(table.Error());
        World::ModelEffects& pool = *table.Value();
        const bool hooked = call.type == StoneEffect || call.type == GateStone || call.type == SkinShell;
        if (hooked && tables.effectHooks == nullptr)
            return Core::Unexpected(std::format("0x518cf0: no effect hooks for the {:#x} of 0x4ee770", call.type));
        switch (call.type)
        {
        case StoneEffect:
            ScatterStones(*tables.effectHooks, pool, position, angles, light, call.subtype, random);
            return {};
        case GateStone:
            return ScatterGateStones(*tables.effectHooks, pool, position, angles, light, call.subtype, random);
        case SiegeShard:
            ScatterSiegeShards(pool, position, angles, light, random);
            return {};
        case SkinShell:
            ScatterSkinShells(*tables.effectHooks, pool, position, angles, light, call.subtype, random);
            return {};
        case GolemStones:
            ScatterGolemStones(pool, position, angles, light, call.subtype, counted, random);
            return {};
        default:
            ScatterShower(pool, static_cast<std::int16_t>(call.type), position, angles, light, random);
            return {};
        }
    }
    case RavenSprite:
    {
        // 0x4eeec6..0x4eef14: the light at 0.08f; /0, 0x5491e0(0x5ac, point, +0x68 of the model, light, 0, 0, 0).
        constexpr std::int32_t Sprite = 0, Joints = 1;
        light = glm::vec3(GlowLight);
        if (call.subtype == Sprite)
        {
            if (tables.sprites == nullptr)
                return Core::Unexpected(std::string("0x4eef0f: faltan los sprites del 0x5ac/0 de 0x4ee770 (0x5491e0)"));
            static_cast<void>(tables.sprites->Create(RavenSprite, position, modelScale, light, 0.0f, 0));
            return {};
        }
        if (call.subtype == Joints)
        {
            // 0x4eef2d..0x4eef3b: the light at (1, 0.8f, 0.2f) before the triangle's index.
            light = {FullLight, std::bit_cast<float>(0x3f4ccccdu), DimLight};
            return PullTheRavenJoints(position, triangle, angles, call, tables, random);
        }
        return {};
    }
    case FadingParticle:
    {
        // 0x4eefd6..0x4ef02e: the light at 1, rand() % 30 and with 0, 0x53fe70(0x607, point, angles, light, 2, 1.0f,
        // 0).
        constexpr int Chance = 30;
        constexpr std::int32_t FadingSubtype = 2;
        light = glm::vec3(FullLight);
        if (random.Next() % Chance != 0)
            return {};
        if (tables.particles == nullptr)
            return Core::Unexpected(std::string("0x4ef02e: faltan las partículas del 0x607 de 0x4ee770 (0x53fe70)"));
        const auto made = tables.particles->CreateChecked(FadingParticle, position, angles, light, FadingSubtype, 1.0f,
                                                          tables.worldClock, random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x4ef02e: la partícula 0x607/2 de 0x4ee770 falla con el error {}",
                                                static_cast<unsigned>(made.Error())));
        return {};
    }
    default:
        return {};
    }
}
} // namespace

Core::Expected<void, std::string> EmitVertexEffectsOf(const Data::Models::Model& model, float modelScale,
                                                      std::span<const Models::SkinnedMesh> meshes,
                                                      const VertexEffectCall& call, const VertexEffectTables& tables,
                                                      Crt::CrtRandom& random)
{
    // 0x4ee776..0x4ee7a4: the mesh between 0 and +0x24 (MOVSX) and with +0xa (the triangles) above 0.
    if (call.mesh < 0 || static_cast<std::size_t>(call.mesh) >= model.meshes.size())
        return {};
    const auto index = static_cast<std::size_t>(call.mesh);
    const auto& triangles = model.meshes[index].triangles;
    if (triangles.empty())
        return {};
    if (index >= meshes.size())
        return Core::Unexpected(std::format("0x4ee770: falta el scratch posado de la malla {}", call.mesh));
    // 0x4ee7aa..0x4ee7c1: the angles at 0 and the light at 1, once per call.
    glm::vec3 angles{};
    glm::vec3 light(FullLight);
    int counted = 0;
    for (std::size_t triangle = 0; triangle < triangles.size(); ++triangle)
    {
        // MOVSX from the count's byte: 0x80..0xff walks no corners.
        const auto corners = static_cast<std::int8_t>(triangles[triangle].polygon);
        if (corners <= 0)
            continue;
        if (static_cast<std::size_t>(corners) > triangles[triangle].vertices.size())
            return Core::Unexpected(std::string("0x4ee770: el polígono excede los cuatro índices retenidos"));
        for (std::int8_t corner = 0; corner < corners; ++corner)
        {
            const auto vertex = triangles[triangle].vertices[static_cast<std::size_t>(corner)];
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= meshes[index].positions.size())
                return Core::Unexpected(std::string("0x4ee770: el vértice excede el scratch posado"));
            const glm::vec3& position = meshes[index].positions[static_cast<std::size_t>(vertex)];
            if (auto emitted = EmitAt(position, static_cast<std::int32_t>(triangle), angles, light, counted, modelScale,
                                      call, tables, random);
                !emitted.HasValue())
                return emitted;
        }
    }
    return {};
}

Core::Expected<void, std::string> EmitVertexEffects(const Models::IModelLibrary& models, const DrawnObject& object,
                                                    std::span<const Models::SkinnedMesh> meshes, std::int32_t mesh,
                                                    std::int32_t type, std::int32_t subtype,
                                                    World::ModelEffects& effects, const IEffectHooks* effectHooks,
                                                    Crt::CrtRandom& random)
{
    const auto* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return {};
    return EmitVertexEffectsOf(*model, object.transform.scale, meshes, {.mesh = mesh, .type = type, .subtype = subtype},
                               {.effects = &effects, .effectHooks = effectHooks}, random);
}
} // namespace Client::Actors::Drawing
