#include "Scenes/VertexEffects.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skinning.h"
#include "Scenes/CharacterDrawing.h"
#include "World/ModelEffects.h"
#include "World/SiegeVertexEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
void EmitAt(const glm::vec3& position, std::int32_t type, World::ModelEffects& effects,
            Render::Models::CrtRandom& random)
{
    constexpr std::int32_t Stone = 0x10a, Gate = 0x12f;
    const glm::vec3 angles{};
    const glm::vec3 light(type == 0x131 ? 0.2f : 1.0f);
    const auto create = [&](std::int16_t child, std::int32_t childSubtype = 0)
    {
        if (type == Stone)
            static_cast<void>(effects.CreateDebris(child, position, angles, light, random));
        else
            static_cast<void>(World::CreateSiegeVertexEffect(
                {.type = child, .position = position, .angles = angles, .light = light, .subtype = childSubtype},
                effects, random));
    };
    if (type == Stone)
    {
        if (random.Next() % 2 == 0)
            create(0x10b);
        if (random.Next() % 10 == 0)
            create(0x10a);
        return;
    }
    if (type == Gate || type == 0x12d || type == 0x12e)
    {
        if (random.Next() % 3 == 0)
            create(static_cast<std::int16_t>((type == Gate ? Gate : 0x12d) + random.Next() % 2), type == 0x12d ? 1 : 0);
        return;
    }
    // 4ee82c..4ee961: orden132,131,133, cada constructor antes del sorteo siguiente.
    if (random.Next() % 12 == 0)
        create(0x132);
    if (random.Next() % 40 == 0)
        create(0x131);
    if (random.Next() % 40 == 0)
        create(0x133);
}

// Skill\golem_stone: lo que 0x4ee770 pide con el tipo 0x13c (0x441400 al morir el 0x1a4 y el 0x1a6).
constexpr std::int32_t GolemStones = 0x13c;

// 0x4eed94..0x4eee66, el 0x13c: un rand() % 45 por esquina; con 0 y menos de 20 ya contados ([ebp+8], que no vuelve a
// 0 entre polígonos), el /0 crea el 0x13c/0 y el /1 el 0x12d/2 y el 0x12e/2, con los ángulos en 0 y la luz (1; 1; 1),
// sin dueño; con cualquier subtipo, uno más en la cuenta (aunque la tabla esté llena).
void ScatterGolemStones(const glm::vec3& position, std::int32_t subtype, int& counted, World::ModelEffects& effects,
                        Render::Models::CrtRandom& random)
{
    constexpr int Chance = 45, Most = 20;
    constexpr std::int32_t Stones = 0, Shower = 1, ShowerSubtype = 2;
    constexpr std::int16_t FirstShower = 0x12d, SecondShower = 0x12e;
    if (random.Next() % Chance != 0 || counted >= Most)
        return;
    const glm::vec3 angles{};
    const glm::vec3 light(1.0f);
    if (subtype == Stones)
    {
        static_cast<void>(World::CreateSiegeVertexEffect(
            {.type = static_cast<std::int16_t>(GolemStones), .position = position, .angles = angles, .light = light},
            effects, random));
    }
    else if (subtype == Shower)
    {
        for (const std::int16_t type : {FirstShower, SecondShower})
            static_cast<void>(World::CreateSiegeVertexEffect(
                {.type = type, .position = position, .angles = angles, .light = light, .subtype = ShowerSubtype},
                effects, random));
    }
    ++counted;
}
} // namespace

Core::Expected<void, std::string> EmitVertexEffects(const IModelLibrary& models, const DrawnObject& object,
                                                    std::span<const Render::Models::SkinnedMesh> meshes,
                                                    std::int32_t mesh, std::int32_t type, std::int32_t subtype,
                                                    World::ModelEffects& effects, Render::Models::CrtRandom& random)
{
    const auto* model = models.Find(object.model);
    if (model == nullptr || mesh < 0 || static_cast<std::size_t>(mesh) >= model->meshes.size())
        return {};
    const auto index = static_cast<std::size_t>(mesh);
    const auto& triangles = model->meshes[index].triangles;
    if (triangles.empty())
        return {};
    const bool golem = type == GolemStones;
    if (!golem && ((type != 0x10a && type != 0x12d && type != 0x12e && type != 0x12f && type != 0x131) || subtype != 0))
        return Core::Unexpected(std::format("0x4ee770: falta la familia {:#x}/{}", type, subtype));
    int counted = 0;
    if (index >= meshes.size())
        return Core::Unexpected(std::string("0x4ee770: falta el scratch posado de la malla"));
    for (const auto& triangle : triangles)
    {
        // MOVSX desde el byte de polygon: 0x80..0xff no recorre esquinas.
        const auto corners = static_cast<std::int8_t>(triangle.polygon);
        if (corners <= 0)
            continue;
        if (static_cast<std::size_t>(corners) > triangle.vertices.size())
            return Core::Unexpected(std::string("0x4ee770: el polígono excede los cuatro índices retenidos"));
        for (std::int8_t corner = 0; corner < corners; ++corner)
        {
            const auto vertex = triangle.vertices[static_cast<std::size_t>(corner)];
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= meshes[index].positions.size())
                return Core::Unexpected(std::string("0x4ee770: el vértice excede el scratch posado"));
            const glm::vec3& position = meshes[index].positions[static_cast<std::size_t>(vertex)];
            if (golem)
                ScatterGolemStones(position, subtype, counted, effects, random);
            else
                EmitAt(position, type, effects, random);
        }
    }
    return {};
}
} // namespace Client::Scenes
