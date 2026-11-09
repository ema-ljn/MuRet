#pragma once

#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Los vectores de la tela (tests/vectors/cloth, su README): lo que hizo el código real del main.exe,
// emulado, frame por frame. Cada float viene como los bits que tenía en el binario.
namespace Client::Tests
{
struct ClothVectorShape
{
    std::int32_t bone = 0;
    glm::vec3 anchor{};
    std::int32_t columns = 0;
    std::int32_t rows = 0;
    float width = 0.0f;
    float height = 0.0f;
    std::uint16_t front = 0;
    std::uint16_t back = 0;
    std::uint32_t flags = 0;
    std::uint8_t windMinimum = 1;
    std::uint8_t windRange = 1;
    struct Sphere
    {
        glm::vec3 local{};
        float radius = 0.0f;
        std::int32_t bone = 0;
    };
    std::vector<Sphere> spheres;
};

struct ClothVectorParticle
{
    glm::vec3 position{};
    // Solo en los pasos "full": el último de cada tela en el frame, o el que la rompe.
    std::optional<glm::vec3> velocity;
    std::optional<glm::vec3> force;
    bool anchored = false;
};

struct ClothVectorStep
{
    std::int32_t cloth = 0;
    std::int32_t iteration = 0;
    bool ok = true;
    float wind = 0.0f;
    bool full = false;
    std::vector<ClothVectorParticle> particles;
};

struct ClothVectorFace
{
    std::int32_t cloth = 0;
    // "opaque", "alpha" o "additive": el estado de GL que dejaron las llamadas del binario.
    std::string blend;
    std::uint16_t texture = 0;
    glm::vec3 color{};
    struct Corner
    {
        std::int32_t particle = 0;
        glm::vec2 uv{};
    };
    std::vector<Corner> corners;
};

struct ClothVectorFlare
{
    glm::vec3 position{};
    float scale = 0.0f;
    float light = 0.0f;
};

// Un evento del frame, en el orden del binario.
struct ClothVectorEvent
{
    enum class Kind : std::uint8_t
    {
        Step,
        Face,
        Flare,
        Destroy,
        Broken,
    };
    Kind kind = Kind::Step;
    ClothVectorStep step;
    ClothVectorFace face;
    ClothVectorFlare flare;
    std::int32_t broken = 0;
};

struct ClothVectorFrame
{
    std::uint8_t characterClass = 0;
    std::uint16_t armor = 0;
    std::uint16_t wings = 0xffff;
    std::uint16_t monster = 0xffff;
    std::uint32_t objectFlags = 0;
    std::uint16_t type = 0;
    glm::vec3 angles{};
    float scale = 1.0f;
    glm::vec3 origin{};
    // objeto + 0x120, ya en el BoneTransform de MuRet.
    std::vector<Render::Models::BoneTransform> bones;
    std::uint8_t map = 0;
    float worldTime = 0.0f;
    std::optional<float> windBase;
    std::int32_t counter = 0;
    std::uint8_t openAccesses = 0;
    std::int32_t windWalks = 0;
    std::optional<glm::vec3> color;
    bool wears = true;
    std::optional<float> dt;
    // 0x7245fc: la z del viento, que en el original no escribe nadie (vale 0); el caso F la prueba.
    std::optional<float> windZ;
    float windBaseAfterTheWalks = 0.0f;
    std::vector<ClothVectorEvent> events;
    std::int32_t clothCount = -1;
    std::int32_t randoms = 0;
};

struct ClothVectors
{
    std::uint32_t randomState = 0;
    std::vector<ClothVectorShape> direct;
    std::vector<ClothVectorFrame> frames;
};

// La carpeta de los vectores, en el repo (MURET_CLIENT_CLOTH_VECTORS_DIR).
[[nodiscard]] std::filesystem::path ClothVectorsDir();

// Lee un archivo de vectores. Devuelve el error del primer renglón que no entiende.
[[nodiscard]] std::optional<std::string> ReadClothVectors(const std::filesystem::path& path, ClothVectors& out);
} // namespace Client::Tests
