#include "Scenes/VertexEffects.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/IModelLibrary.h"
#include "Render/Models/Skinning.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "World/BlastModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/SiegeVertexEffects.h"
#include "World/SkinShellEffects.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;
using Result = Core::Expected<void, std::string>;

// La luz de 0x4ee770: 1 al empezar (0x4ee7b3..0x4ee7c1), 0,2f con el 0x10c y el 0x131 (0x3e4ccccd) y 0,08f con el
// 0x5ac (0x3da3d70a, 0x4eeec8).
constexpr float FullLight = 1.0f;
constexpr float DimLight = std::bit_cast<float>(0x3e4ccccdu);
constexpr float GlowLight = std::bit_cast<float>(0x3da3d70au);

constexpr std::int16_t StoneEffect = 0x10a, StoneShard = 0x10b;
constexpr std::int16_t GateStone = 0x10c, GateShard = 0x10d;
constexpr std::int16_t ShowerEffect = 0x12d, SecondShower = 0x12e, GateEffect = 0x12f;
constexpr std::int16_t SiegeShard = 0x131, SiegeDust = 0x132, SiegeSpark = 0x133;
constexpr std::int16_t SkinShell = World::SkinShellEffect, GolemStones = 0x13c;
constexpr std::int32_t RavenSprite = 0x5ac, FadingParticle = 0x607;

// rand() & máscara con el ajuste del signo (AND 0x80000001 o 0x80000007): con rand() no negativo, el resto.
std::int32_t Masked(CrtRandom& random, std::int32_t modulus)
{
    return random.Next() % modulus;
}

// 0x518cf0(tipo, punto, ángulos, luz, subtipo, 0, −1, 0, 0, 0, 0) de las piedras 0x10a y 0x10b (0x51bafb).
void Debris(World::ModelEffects& effects, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
            const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    static_cast<void>(effects.CreateDebris(type, position, angles, light, random, subtype));
}

// Lo mismo con los fragmentos del asedio y las piedras del 0x13c (0x51d2a9, 0x51d5b8).
void Siege(World::ModelEffects& effects, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
           const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    static_cast<void>(World::CreateSiegeVertexEffect(
        {.type = type, .position = position, .angles = angles, .light = light, .subtype = subtype}, effects, random));
}

// Lo mismo con las piedras 0x10c y 0x10d de las puertas (0x51b99f).
Result GateDebris(World::ModelEffects& effects, std::int16_t type, const glm::vec3& position, const glm::vec3& angles,
                  const glm::vec3& light, std::int32_t subtype, CrtRandom& random, std::uint32_t address)
{
    const auto made = World::CreateBlastModelEffect(
        effects, {.type = type, .position = position, .angles = angles, .light = light, .subtype = subtype}, {}, random,
        {});
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}

// 0x4ee835..0x4ee9d3: el 0x10a.
void ScatterStones(World::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                   const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr std::int32_t Falling = 0, Blue = 1, Green = 2, Mixed = 3;
    constexpr std::int32_t BlueShard = 2, GreenShard = 3, MixedSubtype = 4;
    constexpr int ShardChance = 2, StoneChance = 10;
    switch (subtype)
    {
    case Falling:
        if (Masked(random, ShardChance) == 0)
            Debris(effects, StoneShard, position, angles, light, 0, random);
        if (random.Next() % StoneChance == 0)
            Debris(effects, StoneEffect, position, angles, light, 0, random);
        return;
    case Blue:
        Debris(effects, StoneShard, position, angles, light, BlueShard, random);
        return;
    case Green:
        Debris(effects, StoneShard, position, angles, light, GreenShard, random);
        return;
    case Mixed:
        // 0x4ee981..0x4ee9cb: los argumentos ya en la pila y el rand() & 1 al final, sumado al 0x10a.
        Debris(effects, static_cast<std::int16_t>(StoneEffect + Masked(random, ShardChance)), position, angles, light,
               MixedSubtype, random);
        return;
    default:
        return;
    }
}

// 0x4ee9d8..0x4eeb4e: el 0x10c, con la luz en 0,2f.
Result ScatterGateStones(World::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                         glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr std::int32_t Wide = 0, Narrow = 1, NarrowChildren = 2;
    if (subtype == Narrow)
    {
        light = glm::vec3(DimLight);
        constexpr int ShardChance = 5, StoneChance = 10;
        if (random.Next() % ShardChance == 0)
        {
            if (auto made = GateDebris(effects, GateShard, position, angles, light, NarrowChildren, random, 0x4eea3a);
                !made.HasValue())
                return made;
        }
        if (random.Next() % StoneChance == 0)
            return GateDebris(effects, GateStone, position, angles, light, NarrowChildren, random, 0x4eea8a);
        return {};
    }
    if (subtype == Wide)
    {
        light = glm::vec3(DimLight);
        constexpr int ShardChance = 12, StoneChance = 50;
        if (random.Next() % ShardChance == 0)
        {
            if (auto made = GateDebris(effects, GateShard, position, angles, light, Wide, random, 0x4eeaf7);
                !made.HasValue())
                return made;
        }
        if (random.Next() % StoneChance == 0)
            return GateDebris(effects, GateStone, position, angles, light, Wide, random, 0x4eeb46);
    }
    return {};
}

// 0x4eeb53..0x4eec6b: el 0x12d, el 0x12e y el 0x12f; el segundo rand() & 1 elige entre el tipo base y el siguiente.
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

// 0x4eec70..0x4eed6a: el 0x131, con la luz en 0,2f; cada alta antes del rand() siguiente.
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

// 0x4eed94..0x4eee66, el 0x13c: un rand() % 45 por esquina; con 0 y menos de 20 ya contados ([ebp+8], que no vuelve a
// 0 entre polígonos), el /0 crea el 0x13c/0 y el /1 el 0x12d/2 y el 0x12e/2, sin dueño; con cualquier subtipo, uno más
// en la cuenta (aunque la tabla esté llena).
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

// 0x4eee6b..0x4eeec1: el 0x137, rand() & 7 y con 0 0x518cf0(0x137, punto, ángulos, luz, subtipo, 0, −1, 0, 0, 0, 0):
// la cáscara de World::CreateSkinShell (con la tabla llena, nada y sin sus rand()).
void ScatterSkinShells(World::ModelEffects& effects, const glm::vec3& position, const glm::vec3& angles,
                       const glm::vec3& light, std::int32_t subtype, CrtRandom& random)
{
    constexpr int Chance = 8;
    if (Masked(random, Chance) != 0)
        return;
    static_cast<void>(World::CreateSkinShell(
        {.type = SkinShell, .position = position, .angles = angles, .light = light, .subtype = subtype}, effects,
        random));
}

// 0x4eef19..0x4eefca: el 0x5ac/1 en los triángulos de índice múltiplo de 22.
Result PullTheRavenJoints(const glm::vec3& position, std::int32_t triangle, glm::vec3& angles,
                          const VertexEffectCall& call, const VertexEffectTables& tables, CrtRandom& random)
{
    constexpr int Every = 22, Pitch = 90, Turn = 120, TurnCenter = 60;
    constexpr std::int32_t RavenJoint = 0x613, RavenJointSubtype = 0xd, NoTarget = -1;
    constexpr float RavenJointWidth = 20.0f; // 0x41a00000
    if (triangle % Every != 0)
        return {};
    // 0x4eef4a..0x4eefb2: la y en 0, la x en −(rand() % 90) (FILD, FCHS) y la z en (rand() % 120 − 60) más la z del
    // cuarto argumento (FILD, FADD).
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
    const World::FishEffectRequest request{.type = RavenJoint,
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
    const auto made = CreateFaunaJoint(tables.joints, request, random, JointOwnerKind::Actor);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x4eefc5: {}", made.Error()));
    return {};
}

// Una esquina: el caso del tipo (0x4ee801..0x4ef033).
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
        switch (call.type)
        {
        case StoneEffect:
            ScatterStones(pool, position, angles, light, call.subtype, random);
            return {};
        case GateStone:
            return ScatterGateStones(pool, position, angles, light, call.subtype, random);
        case SiegeShard:
            ScatterSiegeShards(pool, position, angles, light, random);
            return {};
        case SkinShell:
            ScatterSkinShells(pool, position, angles, light, call.subtype, random);
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
        // 0x4eeec6..0x4eef14: la luz en 0,08f; el /0, 0x5491e0(0x5ac, punto, +0x68 del modelo, luz, 0, 0, 0).
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
            // 0x4eef2d..0x4eef3b: la luz en (1; 0,8f; 0,2f) antes del índice del triángulo.
            light = {FullLight, std::bit_cast<float>(0x3f4ccccdu), DimLight};
            return PullTheRavenJoints(position, triangle, angles, call, tables, random);
        }
        return {};
    }
    case FadingParticle:
    {
        // 0x4eefd6..0x4ef02e: la luz en 1, rand() % 30 y con 0 0x53fe70(0x607, punto, ángulos, luz, 2, 1,0f, 0).
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
                                                      std::span<const Render::Models::SkinnedMesh> meshes,
                                                      const VertexEffectCall& call, const VertexEffectTables& tables,
                                                      Render::Models::CrtRandom& random)
{
    // 0x4ee776..0x4ee7a4: la malla entre 0 y +0x24 (MOVSX) y con +0xa (los triángulos) por encima de 0.
    if (call.mesh < 0 || static_cast<std::size_t>(call.mesh) >= model.meshes.size())
        return {};
    const auto index = static_cast<std::size_t>(call.mesh);
    const auto& triangles = model.meshes[index].triangles;
    if (triangles.empty())
        return {};
    if (index >= meshes.size())
        return Core::Unexpected(std::format("0x4ee770: falta el scratch posado de la malla {}", call.mesh));
    // 0x4ee7aa..0x4ee7c1: los ángulos en 0 y la luz en 1, una vez por llamada.
    glm::vec3 angles{};
    glm::vec3 light(FullLight);
    int counted = 0;
    for (std::size_t triangle = 0; triangle < triangles.size(); ++triangle)
    {
        // MOVSX desde el byte de la cuenta: 0x80..0xff no recorre esquinas.
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

Core::Expected<void, std::string> EmitVertexEffects(const Render::Models::IModelLibrary& models,
                                                    const DrawnObject& object,
                                                    std::span<const Render::Models::SkinnedMesh> meshes,
                                                    std::int32_t mesh, std::int32_t type, std::int32_t subtype,
                                                    World::ModelEffects& effects, Render::Models::CrtRandom& random)
{
    const auto* model = models.Find(object.model);
    if (model == nullptr)
        return {};
    return EmitVertexEffectsOf(*model, object.transform.scale, meshes, {.mesh = mesh, .type = type, .subtype = subtype},
                               {.effects = &effects}, random);
}
} // namespace Client::Scenes
