#include "Models/Cloth.h"

#include "Data/Models/Model.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

// Contract: Client/docs/research/06-render-escenas-y-ui.md, "La tela de la clase 4". F() is the original's float
// FSTP; everything else is double, like the x87 stack at 53-bit precision (control word 0x27F). The constants are the
// file's floats, with their addresses.
namespace Client::Models
{
namespace
{
// The file's floats, by their bits: 0x6b8cf4 is 0x3ecccccc, not the compiler's 0.4f (0x3ecccccd), and gives a
// different grid.
constexpr float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}
constexpr float WidthGrowth = Bits(0x3ecccccc);        // 0x6b8cf4: 0.4 (the row width grows)
constexpr float WidthStart = Bits(0x3f19999a);         // 0x6b8a20: 0.6
using Math::Half;                                      // 0x6b882c: 0.5
constexpr float ArchDepth = Bits(0x41200000);          // 0x6b87a8: 10
constexpr float Twenty = Bits(0x41a00000);             // 0x6b86d0 and the grid's immediate
constexpr float ShortFactor = Bits(0x3f4ccccd);        // 0x6b8a38: 0.8
using Math::Hundredth;                                 // 0x6b88d4 and 0x6b86c0: 0.01
using Math::Tenth;                                     // 0x6b87b8: 0.1
constexpr float GravityScale = Bits(0x42c80000);       // 0x6b86b4: 100
constexpr float OneEighty = Bits(0x43340000);          // 0x6b86bc: 180
constexpr float RandomizedWindStep = Bits(0x3ca3d70a); // 0x6b89e4: 0.02
constexpr float Degree = Math::DegreesToRadians;       // 0x6b8cf8 and 0x6ba360: π/180
using Math::Thousandth;                                // 0x6b89c8: 0.001
constexpr float ThirtySecond = Bits(0x3d000000);       // 0x6b8880: 1/32
constexpr float Three = Bits(0x40400000);              // 0x6b86b8: 3
constexpr float Gravity = Bits(0x411ccccd);            // 0x6c7a40, in .data: 9.8
constexpr float GravityFactor = Bits(0x3b23d70a);      // 0x6c7a44: 0.0025
constexpr float InverseMass = Bits(0x43c80000);        // 0x6c7a48: 400
constexpr float GlowBase = Bits(0x3ecccccd);           // 0x6b87b4: 0.4
using Math::Fifth;                                     // 0x6b8854 and 0x6b8cfc: 0.2
constexpr float LastRow = Bits(0x3f7d70a4);            // 0x6b8d00: 0.99
constexpr float WindFloor = Bits(0xbe4ccccd);          // 0x6b8d3c: −0.2
constexpr float WindCeiling = Bits(0x3f800000);        // 0x6b87d4: 1
constexpr double MeshSpringSpan = 10.0;                // 0x6b8d30, a double
constexpr float MeshNeighbourReach = Bits(0x3f99999a); // 0x6b8bb0: 1.2

constexpr std::uint32_t WindModeMask = 0x300;
constexpr std::uint32_t WindRises = 0x100;
constexpr std::uint32_t WindFromBytes = 0x200;
constexpr std::uint32_t GravityMask = 0xc00;
constexpr std::uint32_t StrongGravity = 0x400;
constexpr std::uint32_t TaperMask = 0xc;
constexpr std::uint32_t Tapers = 4;
constexpr std::uint32_t ArchMask = 3;
constexpr std::uint32_t SpringScales = 0x20000000;
constexpr std::uint8_t Relaxes = 1;
constexpr std::uint8_t Pulls = 2;
constexpr std::uint8_t Restrains = 4;
constexpr int ProtectionRandoms = 4;
constexpr int WindWalks = 5;

using Math::D;

constexpr float F(double value)
{
    return static_cast<float>(value);
}

// HL m[row][column] is glm m[column][row] (HalfLifeMath.h).
float At(const Math::BoneTransform& m, int row, int column)
{
    return m[column][row];
}

// 0x5fc8f0, angles in degrees: the cosine of angle 2 stays in double.
Math::BoneTransform OriginalAngleMatrix(const glm::vec3& angles)
{
    const double a2 = D(angles.z) * D(Degree);
    const float sy = F(Math::RoundedSin(a2));
    const double cy = Math::RoundedCos(a2);
    const double a1 = D(angles.y) * D(Degree);
    const float sp = F(Math::RoundedSin(a1));
    const float cp = F(Math::RoundedCos(a1));
    const double a0 = D(angles.x) * D(Degree);
    const float sr = F(Math::RoundedSin(a0));
    const float cr = F(Math::RoundedCos(a0));
    Math::BoneTransform m(0.0f);
    m[0][0] = F(D(cp) * cy);
    m[0][1] = F(D(cp) * D(sy));
    m[0][2] = F(-D(sp));
    const double t1 = D(sr) * D(sp);
    m[1][0] = F(t1 * cy - D(cr) * D(sy));
    m[1][1] = F(t1 * D(sy) + D(cr) * cy);
    m[1][2] = F(D(sr) * D(cp));
    const double t2 = D(cr) * D(sp);
    m[2][0] = F(t2 * cy + D(sr) * D(sy));
    m[2][1] = F(t2 * D(sy) - D(sr) * cy);
    m[2][2] = F(D(cr) * D(cp));
    return m;
}

// 0x5fccc0, with each row's sum order.
glm::vec3 OriginalVectorTransform(const glm::vec3& v, const Math::BoneTransform& m)
{
    const float x = F(((D(At(m, 0, 1)) * D(v.y) + D(At(m, 0, 2)) * D(v.z)) + D(v.x) * D(At(m, 0, 0))) + D(At(m, 0, 3)));
    const float y = F(((D(At(m, 1, 0)) * D(v.x) + D(At(m, 1, 1)) * D(v.y)) + D(At(m, 1, 2)) * D(v.z)) + D(At(m, 1, 3)));
    const float z = F(((D(At(m, 2, 0)) * D(v.x) + D(At(m, 2, 1)) * D(v.y)) + D(At(m, 2, 2)) * D(v.z)) + D(At(m, 2, 3)));
    return {x, y, z};
}

// 0x4ec110 with its fourth argument at 1: the model scale after the bone translation, without rounding s · tx before
// adding the origin (the other two are rounded).
glm::vec3 TransformPosition(const Math::BoneTransform& m, const glm::vec3& v, const ClothBody& body)
{
    const glm::vec3 t = OriginalVectorTransform(v, m);
    return {F(D(body.scale) * D(t.x) + D(body.position.x)), F(D(F(D(body.scale) * D(t.y))) + D(body.position.y)),
            F(D(F(D(body.scale) * D(t.z))) + D(body.position.z))};
}

// 0x4543c0: the difference in floats and the root of (dx² + dy²) + dz² in double, with the 0.001 minimum its callers
// use.
struct Separation
{
    glm::vec3 delta{};
    double length = 0.0;
};

Separation Distance(const ClothParticle& a, const ClothParticle& b)
{
    const glm::vec3 d{F(D(a.position.x) - D(b.position.x)), F(D(a.position.y) - D(b.position.y)),
                      F(D(a.position.z) - D(b.position.z))};
    return {d, std::sqrt((D(d.x) * D(d.x) + D(d.y) * D(d.y)) + D(d.z) * D(d.z))};
}

Separation ClampedDistance(const ClothParticle& a, const ClothParticle& b)
{
    Separation separation = Distance(a, b);
    if (separation.length < D(Thousandth))
    {
        separation.length = D(Thousandth);
    }
    return separation;
}

// 0x454570: the average of the corrections; an anchored particle only clears them.
void ApplyTheCorrections(ClothParticle& particle)
{
    if (particle.anchored)
    {
        particle.correction = glm::vec3(0.0f);
        return;
    }
    if (particle.corrections > 0)
    {
        const double count = static_cast<double>(particle.corrections);
        particle.correction = {F(D(particle.correction.x) / count), F(D(particle.correction.y) / count),
                               F(D(particle.correction.z) / count)};
        particle.position = {F(D(particle.correction.x) + D(particle.position.x)),
                             F(D(particle.correction.y) + D(particle.position.y)),
                             F(D(particle.position.z) + D(particle.correction.z))};
        particle.correction = glm::vec3(0.0f);
        particle.corrections = 0;
    }
}

// t = |i / (columns − 1) − 0.5| · 2, from the arch of flag 1.
double ArchOf(std::int32_t column, std::int32_t columns)
{
    return std::abs(static_cast<double>(column) / static_cast<double>(columns - 1) - D(Half)) * 2.0;
}

// 0x4563c0(mesh, a, b, c): the first mesh triangle that walks the edge (b, a) with a third vertex other than `c` gives
// that third vertex; none, −1. Each triangle, edge by edge: (v0, v1), (v1, v2), (v2, v0).
std::int32_t NeighbourAcross(const Data::Models::Mesh& mesh, std::int32_t a, std::int32_t b, std::int32_t c)
{
    for (const Data::Models::Triangle& triangle : mesh.triangles)
    {
        for (std::size_t side = 0; side < 3; ++side)
        {
            const std::int32_t from = triangle.vertices[side];
            const std::int32_t to = triangle.vertices[(side + 1) % 3];
            const std::int32_t third = triangle.vertices[(side + 2) % 3];
            if (a == to && b == from && c != third)
                return third;
        }
    }
    return -1;
}
} // namespace

void WalkTheWind(ClothWorld& world)
{
    const int step = world.random.Next() % 200 - 100;
    float base = F(D(world.windBase) + static_cast<double>(step) * D(Thousandth));
    if (!(WindFloor <= base))
    {
        base = WindFloor;
    }
    else if (base >= WindCeiling)
    {
        base = WindCeiling;
    }
    world.windBase = base;
}

void StepTheClothWorld(ClothWorld& world)
{
    for (int walk = 0; walk < WindWalks; ++walk)
    {
        WalkTheWind(world);
    }
    // 637d5a..637d7c, no CRT, after the five walks and before the counter.
    constexpr std::uint32_t WaterTextureFrames = 32;
    world.waterTextureFrame = (world.waterTextureFrame + 1u) % WaterTextureFrames;
    // The ADD at 0x637f91 stores the 32 bits before the counter is read back as signed.
    const std::uint32_t nextLogicStepsBits = static_cast<std::uint32_t>(world.logicSteps) + 1u;
    world.logicSteps = std::bit_cast<std::int32_t>(nextLogicStepsBits);
    if (world.openCounterAccesses == 0)
    {
        for (int i = 0; i < ProtectionRandoms; ++i)
        {
            static_cast<void>(world.random.Next());
        }
    }
}

void Cloth::Reset()
{
    m_bone = 0;
    m_front = Render::TextureSlot{0};
    m_back = Render::TextureSlot{0};
    m_flags = 0;
    m_anchor = glm::vec3(0.0f);
    m_width = 1.0f;
    m_height = 1.0f;
    m_columns = 0;
    m_rows = 0;
    m_particles.clear();
    m_springs.clear();
    m_dx = 1.0f;
    m_dy = 1.0f;
    m_windRange = 1;
    m_windMinimum = 1;
}

void Cloth::Release()
{
    m_spheres.clear();
    Reset();
}

void Cloth::SetWindBytes(std::uint8_t minimum, std::uint8_t range)
{
    m_windMinimum = minimum;
    m_windRange = range;
}

void Cloth::AddSphere(const glm::vec3& local, float radius, std::int32_t bone)
{
    m_spheres.push_back(ClothSphere{.local = local, .radius = radius, .bone = bone, .world = glm::vec3(0.0f)});
}

void Cloth::BuildOnTheMesh(const Data::Models::Mesh& mesh, std::int32_t meshIndex, std::int32_t bone,
                           std::uint32_t flags, std::span<const glm::vec3> vertices)
{
    m_bone = bone;
    m_mesh = meshIndex;
    m_flags |= flags;
    m_columns = 0;
    m_rows = 0;
    // 0x455f06..0x455ff2: one particle per vertex, at its position in 0x5786f9c, anchored with the cloth's bone.
    m_particles.assign(mesh.vertices.size(), ClothParticle{});
    m_vertexBones.resize(mesh.vertices.size());
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex)
    {
        ClothParticle& particle = m_particles[vertex];
        particle.position = vertex < vertices.size() ? vertices[vertex] : glm::vec3(0.0f);
        m_vertexBones[vertex] = mesh.vertices[vertex].bone;
        particle.anchored = mesh.vertices[vertex].bone == bone;
    }
    // 0x455ff6..0x4561aa: the springs of the edges and of the neighbours; reserves six per triangle.
    m_springs.clear();
    const auto count = static_cast<std::int32_t>(m_particles.size());
    const auto within = [count](std::int32_t vertex) { return vertex >= 0 && vertex < count; };
    const auto add = [this](std::int32_t a, std::int32_t b, float rest, std::uint8_t springFlags)
    {
        m_springs.push_back(ClothSpring{.a = static_cast<std::int16_t>(a),
                                        .b = static_cast<std::int16_t>(b),
                                        .shortest = F(D(rest) * D(Half)),
                                        .rest = rest,
                                        .flags = springFlags});
    };
    for (const Data::Models::Triangle& triangle : mesh.triangles)
    {
        for (std::size_t side = 0; side < 3; ++side)
        {
            const std::int32_t a = triangle.vertices[side];
            const std::int32_t b = triangle.vertices[(side + 1) % 3];
            const std::int32_t c = triangle.vertices[(side + 2) % 3];
            if (!within(a) || !within(b) || !within(c))
                continue;
            const ClothParticle& first = m_particles[static_cast<std::size_t>(a)];
            const ClothParticle& second = m_particles[static_cast<std::size_t>(b)];
            const float rest = F(Distance(first, second).length);
            // 0x456094..0x4560b1: FSUB of the two floats on the x87, unrounded, against the double 10.
            const bool wide = std::abs(D(first.position.x) - D(second.position.x)) > MeshSpringSpan;
            const auto springFlags = static_cast<std::uint8_t>(wide ? (Relaxes | Pulls) : (Restrains | Pulls));
            add(a, b, rest, springFlags);
            // 0x4560f8..0x456170: the neighbour sharing the edge, with the unrounded length against L · 1.2f.
            const std::int32_t d = NeighbourAcross(mesh, a, b, c);
            if (d <= 0 || !within(d))
                continue;
            const double across =
                Distance(m_particles[static_cast<std::size_t>(c)], m_particles[static_cast<std::size_t>(d)]).length;
            if (!(across < D(rest) * D(MeshNeighbourReach)))
                continue;
            const bool linked = std::ranges::any_of(m_springs, [c, d](const ClothSpring& spring)
                                                    { return spring.a == c && spring.b == d; });
            if (!linked)
                add(c, d, F(across), springFlags);
        }
    }
}

void Cloth::Build(const ClothShape& shape, const ClothBody& body)
{
    m_bone = shape.bone;
    m_front = shape.front;
    m_back = shape.back;
    m_flags |= shape.flags;
    m_anchor = shape.anchor;
    m_width = shape.width;
    m_height = shape.height;
    m_columns = shape.columns;
    m_rows = shape.rows;
    const auto count = static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows);
    m_particles.assign(count, ClothParticle{});
    m_springs.clear();
    m_dx = F(D(m_width) / static_cast<double>(m_columns - 1));
    m_dy = F(D(m_height) / static_cast<double>(m_rows - 1));

    // The provisional grid, with the object's angles and the bone's translation.
    Math::BoneTransform m = OriginalAngleMatrix(body.angles);
    const bool hasTheBone = static_cast<std::size_t>(m_bone) < body.bones.size();
    if (hasTheBone)
    {
        m[3] = body.bones[static_cast<std::size_t>(m_bone)][3];
    }
    const bool tapers = (m_flags & TaperMask) == Tapers;
    const std::uint32_t arch = m_flags & ArchMask;
    for (std::int32_t j = 0; j < m_rows; ++j)
    {
        float dx = m_dx;
        double span = D(m_width);
        if (tapers)
        {
            span = D(m_width) *
                   ((static_cast<double>(j) * D(WidthGrowth)) / static_cast<double>(m_rows - 1) + D(WidthStart));
            dx = F(span / static_cast<double>(m_columns - 1));
        }
        const float half = F(span * D(Half));
        const float z = F(-(static_cast<double>(j) * D(m_dy)));
        for (std::int32_t i = 0; i < m_columns; ++i)
        {
            const float x = F(static_cast<double>(i) * D(dx) - D(half));
            float y = Twenty;
            if (arch == 1)
            {
                const double t = ArchOf(i, m_columns);
                y = F(D(Twenty) - t * t * D(ArchDepth));
            }
            else if (arch == 2)
            {
                y = 0.0f;
            }
            ClothParticle& particle = m_particles[static_cast<std::size_t>(j * m_columns + i)];
            particle.position = hasTheBone ? TransformPosition(m, {x, y, z}, body) : glm::vec3{x, y, z};
        }
    }

    // The springs, measured in the provisional grid.
    const bool rises = (m_flags & WindModeMask) == WindRises;
    const std::uint8_t pull = rises ? 0 : Restrains;
    const std::uint8_t diagonal = rises ? 0 : Relaxes;
    const auto add = [this](std::int32_t a, std::int32_t b, std::uint8_t flags)
    {
        const float rest =
            F(Distance(m_particles[static_cast<std::size_t>(a)], m_particles[static_cast<std::size_t>(b)]).length);
        m_springs.push_back(ClothSpring{.a = static_cast<std::int16_t>(a),
                                        .b = static_cast<std::int16_t>(b),
                                        .shortest = F(D(rest) * D(ShortFactor)),
                                        .rest = rest,
                                        .flags = flags});
    };
    for (std::int32_t j = 0; j < m_rows; ++j)
    {
        for (std::int32_t i = 0; i < m_columns; ++i)
        {
            const std::int32_t k = m_columns * j + i;
            if (j < m_rows - 1)
            {
                add(k, k + m_columns, static_cast<std::uint8_t>(pull | Pulls));
            }
            if (i < m_columns - 1)
            {
                add(k, k + 1, static_cast<std::uint8_t>(Relaxes | Pulls));
                if (j < m_rows - 1)
                {
                    add(k, k + m_columns + 1, diagonal);
                }
                // No such diagonal between rows 0 and 1 (j > 1, not j > 0).
                if (j > 1)
                {
                    add(k, k - m_columns + 1, diagonal);
                }
            }
        }
    }
    // Row 0, anchored to this same matrix and not to the bone: it jumps to the bone on the first step.
    if (hasTheBone)
    {
        AnchorTheTopRow(m, body);
    }
}

// 0x455020: each particle of row 0 to TransformPosition(m, (z, −y', xᵢ)), anchored.
void Cloth::AnchorTheTopRow(const Math::BoneTransform& matrix, const ClothBody& body)
{
    const bool tapers = (m_flags & TaperMask) == Tapers;
    for (std::int32_t i = 0; i < m_columns; ++i)
    {
        double span = D(m_width);
        double dx = D(m_dx);
        if (tapers)
        {
            span = D(m_width) * D(WidthStart);
            dx = span / static_cast<double>(m_columns - 1);
        }
        const float x = F((static_cast<double>(i) * dx - span * D(Half)) + D(m_anchor.x));
        float y = m_anchor.y;
        if ((m_flags & ArchMask) == 1)
        {
            const double t = ArchOf(i, m_columns);
            y = F(D(y) - t * t * D(ArchDepth));
        }
        ClothParticle& particle = m_particles[static_cast<std::size_t>(i)];
        particle.position = TransformPosition(matrix, {m_anchor.z, -y, x}, body);
        particle.anchored = true;
    }
}

bool Cloth::Simulate(float dt, int steps, const ClothBody& body, ClothWorld& world, ClothWindSource fallbackWind)
{
    for (int step = 0; step < steps; ++step)
    {
        if (!Step(dt, body, world, fallbackWind))
        {
            return false;
        }
    }
    return true;
}

bool Cloth::Step(float dt, const ClothBody& body, ClothWorld& world, ClothWindSource fallbackWind)
{
    const bool onTheMesh = m_mesh >= 0;
    if (!onTheMesh && static_cast<std::size_t>(m_bone) >= body.bones.size())
    {
        return true;
    }
    // The wind (0x4551d0).
    if ((m_flags & WindModeMask) == WindFromBytes)
    {
        // The original divides by the byte without checking it: 0x50ba30 never leaves it at 0.
        const int range = m_windRange == 0 ? 1 : m_windRange;
        m_wind = F(static_cast<double>(world.random.Next() % range + m_windMinimum) * D(Hundredth));
    }
    else if (fallbackWind == ClothWindSource::Randomized)
    {
        m_wind = F(static_cast<double>(world.random.Next() % 40 + 10) * D(RandomizedWindStep));
    }
    else
    {
        m_wind = world.windBase;
    }
    const double angle = (D(body.angles.z) + D(OneEighty)) * D(Degree);
    world.wind.x = F(Math::RoundedSin(angle) * D(m_wind));
    world.wind.y = F(-(Math::RoundedCos(angle) * D(m_wind)));

    // 0x4555d0: the forces (+0x14), the springs, the anchoring (+0xc) and the integration; the ones of vtable 0x6b8d04,
    // on the mesh.
    if (onTheMesh)
    {
        AddForcesOnTheMesh(world);
        PullTheSprings();
        AnchorTheMesh(body);
    }
    else
    {
        AddForces(world);
        PullTheSprings();
        AnchorTheTopRow(body.bones[static_cast<std::size_t>(m_bone)], body);
    }

    // 0x454350: semi-implicit Euler with mass 1/400; the position uses the unrounded velocity.
    for (ClothParticle& particle : m_particles)
    {
        if (particle.anchored)
        {
            continue;
        }
        for (int k = 0; k < 3; ++k)
        {
            const double v = (D(InverseMass) * D(particle.force[k])) * D(dt) + D(particle.velocity[k]);
            particle.velocity[k] = F(v);
            particle.position[k] = F(v * D(dt) + D(particle.position[k]));
        }
    }
    return Constrain(body);
}

// 0x4552a0 and 0x454240 per particle.
void Cloth::AddForces(ClothWorld& world)
{
    // The protection of 0x7d3a0f0: closing the access encrypts the counter again and overwrites it with rand().
    if (world.openCounterAccesses == 0)
    {
        for (int i = 0; i < ProtectionRandoms; ++i)
        {
            static_cast<void>(world.random.Next());
        }
    }
    const auto count = static_cast<std::int32_t>(m_particles.size());
    if (count == 0)
    {
        return;
    }
    // The wind center: ((V / 10) · 101) mod N, with the x86 divisions and the product in 32 bits.
    const auto product = static_cast<std::uint32_t>(world.logicSteps / 10) * 101u;
    const std::int32_t center = static_cast<std::int32_t>(product) % count;
    for (std::int32_t i = 0; i < count; ++i)
    {
        ClothParticle& particle = m_particles[static_cast<std::size_t>(i)];
        const std::int32_t distance =
            std::abs(center % m_columns - i % m_columns) + std::abs(center / m_columns - i / m_columns);
        PushTheParticle(particle, distance, world);
    }
}

void Cloth::PushTheParticle(ClothParticle& particle, std::int32_t distance, const ClothWorld& world) const
{
    if (particle.anchored)
    {
        particle.force = glm::vec3(0.0f);
        return;
    }
    const double gravity = D(GravityFactor) * D(Gravity);
    const std::uint32_t windMode = m_flags & WindModeMask;
    // u = 5 − d unsigned: d ≤ 1 → 6, 2 → 5, 3 → 4, 4 → 3, 5 → 0, and d ≥ 6 wraps around back to 6.
    const auto u = static_cast<std::uint32_t>(5 - distance);
    const int weight = u >= 4 ? 6 : (u == 0 ? 0 : static_cast<int>(u) + 2);
    for (int k = 0; k < 3; ++k)
    {
        particle.force[k] = F(static_cast<double>(weight) * D(world.wind[k]) - D(particle.velocity[k]) * D(Hundredth));
    }
    if (windMode == WindRises)
    {
        particle.force.z = F((D(m_wind) + D(Tenth)) * static_cast<double>(weight) + D(particle.force.z));
    }
    else if (windMode == WindFromBytes)
    {
        particle.force.z = F(static_cast<double>(weight) * D(m_wind) + D(particle.force.z));
    }
    const double pull = (m_flags & GravityMask) == StrongGravity ? D(OneEighty) : D(GravityScale);
    particle.force.z = F(D(particle.force.z) - gravity * pull);
}

// 0x4565f0 without 0x10000000: the same counter access (4 rand() on close), and the same distance for all,
// |c % 10| with c = ((V / 10) · 101) mod N (0x4568af..0x4568c0).
void Cloth::AddForcesOnTheMesh(ClothWorld& world)
{
    if (world.openCounterAccesses == 0)
    {
        for (int i = 0; i < ProtectionRandoms; ++i)
        {
            static_cast<void>(world.random.Next());
        }
    }
    const auto count = static_cast<std::int32_t>(m_particles.size());
    if (count == 0)
    {
        return;
    }
    const auto product = static_cast<std::uint32_t>(world.logicSteps / 10) * 101u;
    const std::int32_t center = static_cast<std::int32_t>(product) % count;
    const std::int32_t distance = std::abs(center % 10);
    for (ClothParticle& particle : m_particles)
    {
        PushTheParticle(particle, distance, world);
    }
}

// 0x4564c0 without 0x10000000: each vertex of the cloth's bone to its position in 0x5786f9c, anchored (0x454210).
void Cloth::AnchorTheMesh(const ClothBody& body)
{
    for (std::size_t vertex = 0; vertex < m_particles.size() && vertex < m_vertexBones.size(); ++vertex)
    {
        if (m_vertexBones[vertex] != m_bone || vertex >= body.meshVertices.size())
        {
            continue;
        }
        m_particles[vertex].position = body.meshVertices[vertex];
        m_particles[vertex].anchored = true;
    }
}

void Cloth::WriteTheMesh(std::span<glm::vec3> vertices) const
{
    if (vertices.size() < m_particles.size())
    {
        return;
    }
    for (std::size_t vertex = 0; vertex < m_particles.size(); ++vertex)
    {
        vertices[vertex] = m_particles[vertex].position;
    }
}

// 0x4555d0: springs with flag 2 only pull, when they exceed L + 0.01.
void Cloth::PullTheSprings()
{
    for (const ClothSpring& spring : m_springs)
    {
        if ((spring.flags & Pulls) == 0)
        {
            continue;
        }
        ClothParticle& a = m_particles[static_cast<std::size_t>(spring.a)];
        ClothParticle& b = m_particles[static_cast<std::size_t>(spring.b)];
        const Separation separation = ClampedDistance(a, b);
        if (!(separation.length > D(spring.rest) + D(Hundredth)))
        {
            continue;
        }
        const double stretch = separation.length - D(spring.rest);
        std::array<float, 3> pull{};
        for (int k = 0; k < 3; ++k)
        {
            const double value = stretch * D(separation.delta[k]) / separation.length;
            float stored = F(value);
            if ((m_flags & SpringScales) != 0)
            {
                stored = F((D(spring.rest) * D(ThirtySecond)) * value);
            }
            if ((m_flags & WindModeMask) == WindRises)
            {
                stored = F(D(stored) * D(Three));
            }
            pull[static_cast<std::size_t>(k)] = stored;
        }
        for (int k = 0; k < 3; ++k)
        {
            a.force[k] = F(-D(pull[static_cast<std::size_t>(k)]) + D(a.force[k]));
        }
        for (int k = 0; k < 3; ++k)
        {
            b.force[k] = F(D(pull[static_cast<std::size_t>(k)]) + D(b.force[k]));
        }
    }
}

// 0x455770: the spheres, the relaxation (Jacobi) and the constraint on the particle below (Gauss-Seidel), which tears
// the cloth if a spring exceeds 20 times its length.
bool Cloth::Constrain(const ClothBody& body)
{
    for (ClothSphere& sphere : m_spheres)
    {
        if (static_cast<std::size_t>(sphere.bone) < body.bones.size())
        {
            sphere.world = TransformPosition(body.bones[static_cast<std::size_t>(sphere.bone)],
                                             {sphere.local.z, -sphere.local.y, sphere.local.x}, body);
        }
    }
    if (!m_spheres.empty())
    {
        for (const ClothSphere& sphere : m_spheres)
        {
            for (ClothParticle& particle : m_particles)
            {
                // 0x4547f0: z with an unrounded factor.
                float dx = F(D(particle.position.x) - D(sphere.world.x));
                float dy = F(D(particle.position.y) - D(sphere.world.y));
                const double exactZ = D(particle.position.z) - D(sphere.world.z);
                float dz = F(exactZ);
                double distance = std::sqrt((exactZ * D(dz) + D(dy) * D(dy)) + D(dx) * D(dx));
                if (distance < D(Hundredth))
                {
                    distance = D(Hundredth);
                    dx = Hundredth;
                    dy = 0.0f;
                    dz = 0.0f;
                }
                if (distance < D(sphere.radius))
                {
                    const double k = (D(sphere.radius) - distance) / distance;
                    const glm::vec3 push{F(k * D(dx)), F(k * D(dy)), F(k * D(dz))};
                    for (int m = 0; m < 3; ++m)
                    {
                        particle.correction[m] = F(D(push[m]) + D(particle.correction[m]));
                    }
                    ++particle.corrections;
                }
            }
        }
        for (ClothParticle& particle : m_particles)
        {
            ApplyTheCorrections(particle);
        }
    }
    for (const ClothSpring& spring : m_springs)
    {
        if ((spring.flags & Relaxes) == 0)
        {
            continue;
        }
        ClothParticle& a = m_particles[static_cast<std::size_t>(spring.a)];
        ClothParticle& b = m_particles[static_cast<std::size_t>(spring.b)];
        const Separation separation = ClampedDistance(a, b);
        const double k = ((separation.length - D(spring.rest)) * D(Half)) / separation.length;
        const float dx = F(k * D(separation.delta.x));
        const float dy = F(k * D(separation.delta.y));
        const double dz = k * D(separation.delta.z);
        a.correction = {F(D(a.correction.x) - D(dx)), F(D(a.correction.y) - D(dy)), F(D(a.correction.z) - dz)};
        b.correction = {F(D(dx) + D(b.correction.x)), F(D(dy) + D(b.correction.y)), F(dz + D(b.correction.z))};
        ++a.corrections;
        ++b.corrections;
    }
    for (ClothParticle& particle : m_particles)
    {
        ApplyTheCorrections(particle);
    }
    for (const ClothSpring& spring : m_springs)
    {
        if (spring.b < m_columns || (spring.flags & Restrains) == 0)
        {
            continue;
        }
        ClothParticle& below = m_particles[static_cast<std::size_t>(spring.b)];
        const ClothParticle& above = m_particles[static_cast<std::size_t>(spring.a)];
        if (below.anchored)
        {
            continue;
        }
        const Separation separation = ClampedDistance(below, above);
        if (separation.length > D(spring.rest) * D(Twenty))
        {
            return false;
        }
        double k = 0.0;
        if (separation.length > D(spring.rest))
        {
            k = (separation.length - D(spring.rest)) / separation.length;
        }
        else if (separation.length < D(spring.shortest))
        {
            k = (separation.length - D(spring.shortest)) / separation.length;
        }
        else
        {
            continue;
        }
        const float dx = F(k * D(separation.delta.x));
        const float dy = F(k * D(separation.delta.y));
        const double dz = k * D(separation.delta.z);
        below.position = {F(D(below.position.x) - D(dx)), F(D(below.position.y) - D(dy)), F(D(below.position.z) - dz)};
    }
    return true;
}

std::vector<ClothCorner> Cloth::Corners(bool front) const
{
    std::vector<ClothCorner> corners;
    if (m_columns < 2 || m_rows < 2)
    {
        return corners;
    }
    corners.reserve(static_cast<std::size_t>((m_columns - 1) * (m_rows - 1) * 4));
    for (std::int32_t j = 0; j < m_rows - 1; ++j)
    {
        for (std::int32_t i = 0; i < m_columns - 1; ++i)
        {
            const std::array<std::array<std::int32_t, 2>, 4> square =
                front ? std::array<std::array<std::int32_t, 2>, 4>{{{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}}
                      : std::array<std::array<std::int32_t, 2>, 4>{{{i, j}, {i, j + 1}, {i + 1, j + 1}, {i + 1, j}}};
            for (const auto& [column, row] : square)
            {
                const double v = static_cast<double>(row) / static_cast<double>(m_rows - 1);
                corners.push_back(
                    ClothCorner{.particle = m_columns * row + column,
                                .uv = {F(static_cast<double>(column) / static_cast<double>(m_columns - 1)),
                                       D(LastRow) < v ? LastRow : F(v)}});
            }
        }
    }
    return corners;
}

float Cloth::GlowAt(float worldTime)
{
    return F(Math::RoundedSin(D(worldTime) * D(Thousandth)) * D(Tenth) + D(GlowBase));
}

std::vector<ClothFlare> Cloth::Flares(float glow) const
{
    std::vector<ClothFlare> flares;
    std::size_t k = 0;
    for (std::int32_t i = 0; i < m_columns; ++i)
    {
        for (std::int32_t j = 0; j < m_rows && k < m_particles.size(); ++j)
        {
            flares.push_back(ClothFlare{
                .position = m_particles[k].position,
                .scale = F(static_cast<double>(i) * D(Fifth) + D(GlowBase)),
                .light = F(static_cast<double>(m_columns - i) * D(glow) * D(Fifth)),
            });
            ++k;
        }
    }
    return flares;
}

ClothBlend FaceBlendOf(std::uint32_t flags)
{
    if ((flags & ClothGlows) != 0)
    {
        return ClothBlend::Additive;
    }
    switch (flags & ClothBlendMask)
    {
    case 0:
        return ClothBlend::Opaque;
    case ClothAlphaBlend:
        return ClothBlend::Alpha;
    case ClothAdditiveBlend:
        return ClothBlend::Additive;
    default:
        return ClothBlend::Previous;
    }
}

bool Cloth::DrawsTheBack() const
{
    return !((m_flags & ClothBlendMask) == ClothAdditiveBlend && (m_flags & ClothGlows) != 0);
}
} // namespace Client::Models
