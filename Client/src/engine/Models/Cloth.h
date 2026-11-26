#pragma once

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "EngineApi.h"
#include "Math/HalfLifeMath.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <vector>

// The original's cloth class (vtable 0x6b8cd8; the Dark Lord's cape and class 3's):
// Client/docs/research/06-render-escenas-y-ui.md, "La tela de la clase 4". Each computation follows the original's
// x87 order at 53-bit precision: what the original stores in a float is rounded to float here, and what it leaves on
// the stack stays double. The tests compare each step, bit by bit, with the emulated main.exe (tests/vectors/cloth).
namespace Client::Models
{
// What the cloth reads from the original's globals, and its rand(): the base wind (0x7245f0), this step's wind
// (0x7245f4..0x7245fc; nobody writes z and it stays 0) and the protected logic-step counter (0x7d3a0f0).
struct ClothWorld
{
    Crt::CrtRandom random;
    float windBase = 0.0f;
    glm::vec3 wind{};
    std::int32_t logicSteps = 0;
    // Open counter accesses (the table at 0x57630c8): with 0, each cloth step encrypts it again and draws 4 unused
    // rand(). No other access is open while drawing.
    std::int32_t openCounterAccesses = 0;
    // DAT_07d2d360 is in .bss: 0. 637d63/637d7c advance the 32-cycle after the wind, before the logic counter. The
    // owner only publishes values 0..31 and survives maps and scenes.
    std::uint32_t waterTextureFrame = 0;
};

enum class ClothWindSource
{
    Base,
    Randomized,
};

// 0x456b10: the base wind walk, b = f(b + (rand() % 200 − 100) · 0.001), between −0.2 and 1.
ENGINE_API void WalkTheWind(ClothWorld& world);

// What 0x637700 does for the cloth on each logic step of scenes 2, 4 and 5: the wind walk five times (0x637cec) and the
// counter plus 1 (0x637f8c), which draws another 4 rand() when its access closes.
// MuRet does not draw the other rand() calls of 0x637700 here: they are for other things.
ENGINE_API void StepTheClothWorld(ClothWorld& world);

// A particle (0x3c bytes, vtable 0x6b8cbc). The position is in world space.
struct ClothParticle
{
    glm::vec3 force{};
    glm::vec3 velocity{};
    glm::vec3 position{};
    bool anchored = false;
    std::int32_t corrections = 0;
    glm::vec3 correction{};
};

// A spring (0x10 bytes, 0x455140): the two particles, the minimum length f(L · 0.8), the rest length L and its flags:
// 1 relaxes, 2 pulls and 4 restrains.
struct ClothSpring
{
    std::int16_t a = 0;
    std::int16_t b = 0;
    float shortest = 0.0f;
    float rest = 0.0f;
    std::uint8_t flags = 0;
};

// A sphere (0x24 bytes, vtable 0x6b8ccc): its place on the bone, the radius and the world center from the last step.
struct ClothSphere
{
    glm::vec3 local{};
    float radius = 0.0f;
    std::int32_t bone = 0;
    glm::vec3 world{};
};

// The build arguments (0x454a50(object, bone, x, y, z, columns, rows, width, height, front, back, flags)).
struct ClothShape
{
    std::int32_t bone = 0;
    glm::vec3 anchor{};
    std::int32_t columns = 2;
    std::int32_t rows = 2;
    float width = 1.0f;
    float height = 1.0f;
    Render::TextureSlot front{0};
    Render::TextureSlot back{0};
    std::uint32_t flags = 0;
};

// The object it hangs from: its bones (object + 0x120, as PoseSkeleton poses them: no scale or position, root
// rotated), its angles (+0x1c) and the model's scale and position (+0x68 and +0x6c..+0x74, which 0x6001b0 copies from
// the object).
struct ClothBody
{
    std::span<const Math::BoneTransform> bones;
    glm::vec3 angles{};
    float scale = 1.0f;
    glm::vec3 position{};
    // Mesh cloth only: its mesh's world vertices as the last 0x4ebc80 left them in 0x5786f9c (mesh × 15000 vertices).
    // Read by its anchoring (0x4564c0).
    std::span<const glm::vec3> meshVertices{};
};

// The cloth flags the drawing looks at (06 § La tela de la clase 4).
inline constexpr std::uint32_t ClothBlendMask = 0x3000;
inline constexpr std::uint32_t ClothAlphaBlend = 0x1000;
inline constexpr std::uint32_t ClothAdditiveBlend = 0x2000;
inline constexpr std::uint32_t ClothKeepsTheBlend = 0x3000;
inline constexpr std::uint32_t ClothGlows = 0x40000000;

// The blend the faces are drawn with (0x455930): by `& 0x3000`, 0x620980 (opaque), 0x620a00(1) (normal, with alpha
// test) or 0x620a90 (additive), and with 0x3000 the one already set; with the glow, the additive one as well.
enum class ClothBlend : std::uint8_t
{
    Opaque,
    Alpha,
    Additive,
    Previous,
};
[[nodiscard]] ENGINE_API ClothBlend FaceBlendOf(std::uint32_t flags);

// A face vertex as 0x455c40 sends it: the particle and its UV.
struct ClothCorner
{
    std::int32_t particle = 0;
    glm::vec2 uv{};
};

// One of the glow sprites (0x5491e0(0x5ac, …)): Effect\flare01.jpg at a particle's position, with its scale and light.
// The effect manager (0x549440) draws them later.
struct ClothFlare
{
    glm::vec3 position{};
    float scale = 0.0f;
    float light = 0.0f;
};

class ENGINE_API Cloth
{
public:
    // 0x454a00: everything at 0, width, height, dx and dy at 1 and the wind bytes at 1.
    Cloth() = default;

    // 0x454a50: the particles in a provisional grid (with the object's angles and the bone's translation), the springs
    // measured in it and row 0 anchored to that same matrix. The flags are ORed into the existing ones and the spheres
    // stay: that is how 0x50ba30 builds the fourth cloth again.
    void Build(const ClothShape& shape, const ClothBody& body);
    // 0x455ea0 (vtable 0x6b8d04 + 0x24), the mesh cloth (08 P-C22 f): one particle per vertex of `mesh` in `vertices`
    // (0x5786f9c, world space), anchored if its bone is `bone`; for each edge (a, b) of each triangle, in order, a
    // spring with L = f(|a − b|), minimum f(L · 0.5) and flags 3 if |a.x − b.x| exceeds 10 (in double) or 6 otherwise;
    // and, if the neighbouring triangle that walks it backwards (0x4563c0) has its third vertex d (from 1: 0 does not
    // count, Q-C861) and f(|c − d|) < L · 1.2f, with c this triangle's third, another from c to d with the same flags
    // unless one from c to d already exists (0x456470, in that direction). `meshIndex` is the +0x60 the drawing reads.
    // Without the model, columns at 0: the constraint looks at every spring.
    void BuildOnTheMesh(const Data::Models::Mesh& mesh, std::int32_t meshIndex, std::int32_t bone, std::uint32_t flags,
                        std::span<const glm::vec3> vertices);
    // 0x455cd0.
    void AddSphere(const glm::vec3& local, float radius, std::int32_t bone);
    // Bytes +0x49 and +0x48: the wind minimum and range of the capes with 0x200.
    void SetWindBytes(std::uint8_t minimum, std::uint8_t range);
    // 0x454f80: releases the spheres, the springs and the particles, and goes back to 0x454a00.
    void Release();

    // 0x455190(dt, steps): the steps of 0x4551d0 until the first that returns false (the cloth tore).
    [[nodiscard]] bool Simulate(float dt, int steps, const ClothBody& body, ClothWorld& world,
                                ClothWindSource fallbackWind);
    // 0x4551d0: the wind, the forces, the pulling springs, the anchoring, the integration, the spheres, the relaxation
    // and the constraint. false if a spring exceeded 20 times its length.
    [[nodiscard]] bool Step(float dt, const ClothBody& body, ClothWorld& world, ClothWindSource fallbackWind);

    // Draws the mesh cloth (0x4568f0, vtable 0x6b8d04 + 0x18): each particle to its vertex in 0x5786f9c, which the
    // model's RenderBody draws afterwards. Nothing if `vertices` is shorter.
    void WriteTheMesh(std::span<glm::vec3> vertices) const;
    // The mesh of the mesh cloth (+0x60), or −1 for the grid cloth.
    [[nodiscard]] std::int32_t Mesh() const
    {
        return m_mesh;
    }

    // The drawing (0x455930): the four corners of each square, front (i, j), (i+1, j), (i+1, j+1), (i, j+1) and back
    // reversed, with u = f(i / (columns − 1)) and v = j / (rows − 1) up to 0.99.
    [[nodiscard]] std::vector<ClothCorner> Corners(bool front) const;
    // The glow of 0x40000000 with the clock T in ms (DAT_05767b88): t = f(sin(T · 0.001) · 0.1 + 0.4).
    [[nodiscard]] static float GlowAt(float worldTime);
    // The 30 glow sprites: k walks the particles by rows, and the scale and the light change every `rows` sprites, like
    // the original.
    [[nodiscard]] std::vector<ClothFlare> Flares(float glow) const;
    // Not with the additive blend and the glow together.
    [[nodiscard]] bool DrawsTheBack() const;

    [[nodiscard]] std::span<const ClothParticle> Particles() const
    {
        return m_particles;
    }
    [[nodiscard]] std::span<const ClothSpring> Springs() const
    {
        return m_springs;
    }
    [[nodiscard]] std::span<const ClothSphere> Spheres() const
    {
        return m_spheres;
    }
    [[nodiscard]] std::uint32_t Flags() const
    {
        return m_flags;
    }
    [[nodiscard]] Render::TextureSlot Front() const
    {
        return m_front;
    }
    [[nodiscard]] Render::TextureSlot Back() const
    {
        return m_back;
    }
    [[nodiscard]] float Wind() const
    {
        return m_wind;
    }
    [[nodiscard]] std::int32_t Columns() const
    {
        return m_columns;
    }
    // The anchored row's bone, the `bone` of its ClothShape.
    [[nodiscard]] std::int32_t Bone() const
    {
        return m_bone;
    }
    [[nodiscard]] std::int32_t Rows() const
    {
        return m_rows;
    }

private:
    void Reset();
    void AnchorTheTopRow(const Math::BoneTransform& matrix, const ClothBody& body);
    void AddForces(ClothWorld& world);
    // 0x454240: a particle's force from its Manhattan distance to the wind center.
    void PushTheParticle(ClothParticle& particle, std::int32_t distance, const ClothWorld& world) const;
    // 0x4565f0 and 0x4564c0, the mesh cloth without 0x10000000.
    void AddForcesOnTheMesh(ClothWorld& world);
    void AnchorTheMesh(const ClothBody& body);
    void PullTheSprings();
    [[nodiscard]] bool Constrain(const ClothBody& body);

    std::int32_t m_bone = 0;
    Render::TextureSlot m_front{0};
    Render::TextureSlot m_back{0};
    std::uint32_t m_flags = 0;
    glm::vec3 m_anchor{};
    float m_width = 1.0f;
    float m_height = 1.0f;
    std::int32_t m_columns = 0;
    std::int32_t m_rows = 0;
    std::vector<ClothParticle> m_particles;
    std::vector<ClothSpring> m_springs;
    float m_wind = 0.0f;
    std::uint8_t m_windRange = 1;
    std::uint8_t m_windMinimum = 1;
    float m_dx = 1.0f;
    float m_dy = 1.0f;
    std::vector<ClothSphere> m_spheres;
    // Mesh cloth: its mesh (+0x60) and each vertex's bone, read by its anchoring.
    std::int32_t m_mesh = -1;
    std::vector<std::int16_t> m_vertexBones;
};
} // namespace Client::Models
