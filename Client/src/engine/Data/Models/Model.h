#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A .bmd model as 0x4f13d0 leaves it (Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd"). Every float keeps
// the file's bits, even a NaN. Indices come unchecked: the goldens check that they fit in the lab's files.
namespace Client::Data::Models
{
struct Vertex
{
    // Rigid. In the character's parts it indexes Player.bmd's bones, not the part's (05).
    std::int16_t bone = 0;
    // In that bone's space.
    std::array<float, 3> position{};
};

struct Normal
{
    std::int16_t bone = 0;
    std::array<float, 3> normal{};
    // The int16 at +16: an index into the mesh's vertices, the vertex that uses it (*inferred*: in the lab all 301,367
    // fit in their mesh).
    std::int16_t vertex = 0;
};

struct TexCoord
{
    float u = 0.0f;
    float v = 0.0f;
};

// What is read from the 36 bytes 0x4f13d0 copies of each 64-byte triangle. The 6 at +0x1a are overwritten by the
// adjacency that 0x4f0870 and 0x4f0910 compute, and nothing in CL6 uses the 4 at +0x20.
struct Triangle
{
    // 3 in every lab mesh. The fourth index of each set is used only with 4.
    std::uint8_t polygon = 3;
    std::array<std::int16_t, 4> vertices{};
    std::array<std::int16_t, 4> normals{};
    std::array<std::int16_t, 4> texCoords{};
};

struct Mesh
{
    // The mesh header's int16, as it comes. The texture slot comes from the original's allocator (0x60d5f0), by name.
    std::int16_t texture = 0;
    std::vector<Vertex> vertices;
    std::vector<Normal> normals;
    std::vector<TexCoord> texCoords;
    std::vector<Triangle> triangles;
    // The mesh's last 32 bytes, up to the first 0.
    std::string textureName;
};

struct Action
{
    std::int16_t keyCount = 0;
    bool lockPositions = false;
    // With lockPositions, one position per key. The original stores them and nothing reads them (05).
    std::vector<std::array<float, 3>> lockedPositions;
};

// keyCount positions, then keyCount rotations (Euler angles in radians), both relative to the parent.
struct BoneKeys
{
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> rotations;
};

struct Bone
{
    // A dummy bone has no name, parent or keys, and the pose does not touch it (05).
    bool dummy = false;
    std::string name;
    std::int16_t parent = -1;
    // One per model action, in the order of Model::actions. Empty for a dummy.
    std::vector<BoneKeys> actions;
};

struct Model
{
    // File byte 3: 0x0C is encrypted with K16, any other is plain.
    std::uint8_t version = 0;
    // Up to the first 0.
    std::string name;
    std::vector<Mesh> meshes;
    std::vector<Action> actions;
    std::vector<Bone> bones;
    // What is left after the last bone in what the original reads: the encrypted block in version 0x0C and the rest of
    // the file in the others. It never looks at it; in the lab it is 0.
    std::size_t trailingBytes = 0;
};
} // namespace Client::Data::Models
