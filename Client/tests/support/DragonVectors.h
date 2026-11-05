#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Los vectores del dragón del login (tests/vectors/world55/dragon.txt, de
// docs/research/scripts/login/make_dragon_vectors.py): lo que el código real del main.exe, emulado,
// deja en el efecto 0xb4 al crearlo (0x518cf0) y después de cada paso (0x5208a0). Cada float viene
// como los bits que tenía en el binario.
namespace Client::Tests
{
// Los campos del efecto de 0x1d4 bytes que lee el generador, en su orden.
struct DragonVectorState
{
    bool alive = false;          // +0x0
    std::int16_t type = 0;       // +0x2
    float scale = 0.0f;          // +0xc
    glm::vec3 position{};        // +0x10
    glm::vec3 angles{};          // +0x1c
    std::int32_t life = 0;       // +0x60
    glm::vec3 velocity{};        // +0xcc
    float speed = 0.0f;          // +0xd8
    float turnPending = 0.0f;    // +0xdc
    float turnDirection = 0.0f;  // +0xe4
    float flagEc = 0.0f;         // +0xec
    std::uint8_t state = 0;      // +0x88
    float reference = 0.0f;      // +0x84
    float frame = 0.0f;          // +0x114
    float previousFrame = 0.0f;  // +0x118
    std::uint8_t action = 0;     // +0x111
    glm::vec3 light{};           // +0xf4
    float alpha = 0.0f;          // +0x174
    std::int32_t hiddenMesh = 0; // +0x58
    std::int32_t blendMesh = 0;  // +0x64
    float blendMeshLight = 0.0f; // +0x68
    bool vertexLight = false;    // +0xe8
    glm::vec3 start{};           // +0x17c
};

struct DragonVectors
{
    DragonVectorState created;
    std::vector<DragonVectorState> steps;
    // Las llamadas a rand() de todos los pasos (una por paso: una luz que el 0xb4 no usa).
    std::int32_t randomCalls = 0;
};

// La carpeta de los vectores del mapa 55 es la de WorldObjectVectors.h (World55VectorsDir).
// Lee `path` en `out`; el error, si no se pudo, con el renglón.
[[nodiscard]] std::optional<std::string> ReadDragonVectors(const std::filesystem::path& path, DragonVectors& out);
} // namespace Client::Tests
