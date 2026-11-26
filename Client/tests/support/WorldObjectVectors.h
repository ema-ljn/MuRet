#pragma once

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The map 55 object vectors (tests/vectors/world55/objects.txt, from
// docs/research/scripts/login/make_world55_vectors.py): what the main.exe's real code, emulated, leaves in each object
// when creating it (0x606660), in three steps (0x604ab0) and what it passes to RenderBody or RenderMesh (0x602e90).
// Each float comes as the bits it had in the binary.
namespace Client::Tests
{
// The fields of the 0x1d4-byte object the generator reads, in their order.
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

// A draw call: RenderBody(mode, alpha, blendMesh, light, U, V, hidden mesh, −1) or RenderMesh(mesh, flags, alpha,
// blendMesh, light, U, V, −1), with the model's color (+0x48) and its per-vertex light (+0x44; RenderMesh's blendMesh
// branch does not use it).
struct WorldObjectVectorCall
{
    std::string name;
    std::int32_t first = 0; // RenderBody's mode, or RenderMesh's mesh
    std::int32_t flags = 0; // RenderMesh's flags
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
    // The fixed terrain light the generator gave 0x5ffce0.
    glm::vec3 terrainLight{};
    std::vector<WorldObjectVector> objects;
};

// The map 55 vectors' folder (tests/vectors/world55).
[[nodiscard]] std::filesystem::path World55VectorsDir();

// Reads `path` into `out`; the error, if it could not, with the line.
[[nodiscard]] std::optional<std::string> ReadWorldObjectVectors(const std::filesystem::path& path,
                                                                WorldObjectVectors& out);
} // namespace Client::Tests
