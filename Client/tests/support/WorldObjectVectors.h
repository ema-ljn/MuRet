#pragma once

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Los vectores de los objetos del mapa 55 (tests/vectors/world55/objects.txt, de
// docs/research/scripts/login/make_world55_vectors.py): lo que el código real del main.exe, emulado,
// deja en cada objeto al crearlo (0x606660), en tres pasos (0x604ab0) y lo que le pasa a RenderBody o a
// RenderMesh (0x602e90). Cada float viene como los bits que tenía en el binario.
namespace Client::Tests
{
// Los campos del objeto de 0x1d4 bytes que lee el generador, en su orden.
struct WorldObjectVectorState
{
    float scale = 0.0f;          // +0xc
    glm::vec3 position{};        // +0x10
    glm::vec3 angles{};          // +0x1c
    std::int32_t hiddenMesh = 0; // +0x58
    std::int32_t blendMesh = 0;  // +0x64
    float blendMeshLight = 0.0f; // +0x68
    glm::vec2 uv{};              // +0x6c, +0x70
    float speed = 0.0f;          // +0xd8
    bool vertexLight = false;    // +0xe8
    glm::vec3 light{};           // +0xf4
    float alpha = 0.0f;          // +0x174
    float targetAlpha = 0.0f;    // +0x170
    float frame = 0.0f;          // +0x114
    std::uint8_t action = 0;     // +0x111
};

// Una llamada del dibujo: RenderBody(modo, alpha, blendMesh, luz, U, V, malla oculta, −1) o
// RenderMesh(malla, banderas, alpha, blendMesh, luz, U, V, −1), con el color del modelo (+0x48) y su
// luz por vértice (+0x44; la rama del blendMesh de RenderMesh no la usa).
struct WorldObjectVectorCall
{
    std::string name;
    std::int32_t first = 0; // el modo de RenderBody, o la malla de RenderMesh
    std::int32_t flags = 0; // las banderas de RenderMesh
    float alpha = 0.0f;
    std::int32_t blendMesh = 0;
    float blendMeshLight = 0.0f;
    glm::vec2 uv{};
    std::int32_t hiddenMesh = 0;
    glm::vec3 color{};
    bool vertexLight = false;
};

struct WorldObjectVector
{
    std::int32_t index = 0;
    std::int16_t type = 0;
    bool hasModel = false;
    WorldObjectVectorState created;
    std::array<WorldObjectVectorState, 3> steps{};
    std::vector<WorldObjectVectorCall> calls;
};

struct WorldObjectVectors
{
    // La luz del terreno fija que el generador le dio a 0x5ffce0.
    glm::vec3 terrainLight{};
    std::vector<WorldObjectVector> objects;
};

// La carpeta de los vectores del mapa 55 (tests/vectors/world55).
[[nodiscard]] std::filesystem::path World55VectorsDir();

// Lee `path` en `out`; el error, si no se pudo, con el renglón.
[[nodiscard]] std::optional<std::string> ReadWorldObjectVectors(const std::filesystem::path& path,
                                                                WorldObjectVectors& out);
} // namespace Client::Tests
