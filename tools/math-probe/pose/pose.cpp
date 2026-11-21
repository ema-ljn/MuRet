// The client's pose math (HalfLifeMath.cpp and OriginalRotation.cpp) on fixed inputs. Prints one hash per
// output set, and saves the sets when a directory is given. Built with the system math library, and with
// sin, cos, tan and acos routed to LLVM-libc (cr_redirect.h), to compare the hashes across systems.
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "../src/domains.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Client::Render::Models;

namespace
{
// One call per statement: the order of calls inside one expression is unspecified.
glm::vec3 Triple(SplitMix64& rng, float scale, float offset)
{
    glm::vec3 v;
    v.x = rng.UnitF() * scale - offset;
    v.y = rng.UnitF() * scale - offset;
    v.z = rng.UnitF() * scale - offset;
    return v;
}
float Grid(SplitMix64& rng) { return static_cast<float>(static_cast<int>(rng.Next() % 145) - 72) * 5.0f; }

std::uint64_t Fnv(const std::vector<float>& values)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
    for (std::size_t i = 0; i < values.size() * sizeof(float); ++i)
    {
        h = (h ^ bytes[i]) * 0x100000001b3ull;
    }
    return h;
}
} // namespace

int main(int argc, char** argv)
{
    const std::size_t n = 1000000;
    SplitMix64 rng{0xA11CE};
    std::vector<float> quaternions;   // AngleQuaternion: what every bone key goes through (glm, float sin and cos)
    std::vector<float> matrices;      // OriginalAngleMatrix: std::sin and std::cos on doubles, stored to float
    std::vector<float> roundMatrices; // the same with angles on a 5 degree grid, where terms cancel
    std::vector<float> slerps;        // QuaternionSlerp: acos and sin on doubles
    std::vector<float> chains;        // a vertex through a chain of 12 bones
    for (std::size_t i = 0; i < n; ++i)
    {
        const glm::vec3 radians = Triple(rng, 6.2831855f, 3.1415927f);
        const glm::quat q = AngleQuaternion(radians);
        quaternions.insert(quaternions.end(), {q.w, q.x, q.y, q.z});

        const glm::vec3 degrees = Triple(rng, 720.0f, 360.0f);
        const OriginalMatrix m = OriginalAngleMatrix(degrees);
        matrices.insert(matrices.end(), m.begin(), m.end());

        glm::vec3 grid;
        grid.x = Grid(rng);
        grid.y = Grid(rng);
        grid.z = Grid(rng);
        const OriginalMatrix g = OriginalAngleMatrix(grid);
        roundMatrices.insert(roundMatrices.end(), g.begin(), g.end());

        glm::vec3 other;
        other.x = radians.x + rng.UnitF() * 0.6f - 0.3f;
        other.y = radians.y + rng.UnitF() * 0.6f - 0.3f;
        other.z = radians.z + rng.UnitF() * 0.6f - 0.3f;
        const glm::quat target = AngleQuaternion(other);
        const float fraction = rng.UnitF();
        const glm::quat s = QuaternionSlerp(q, target, fraction);
        slerps.insert(slerps.end(), {s.w, s.x, s.y, s.z});
    }
    for (std::size_t i = 0; i < n / 10; ++i)
    {
        BoneTransform world(1.0f);
        for (int bone = 0; bone < 12; ++bone)
        {
            const glm::vec3 a = Triple(rng, 1.0f, 0.5f);
            glm::vec3 b;
            b.x = a.x + rng.UnitF() * 0.2f;
            b.y = a.y + rng.UnitF() * 0.2f;
            b.z = a.z + rng.UnitF() * 0.2f;
            const float fraction = rng.UnitF();
            BoneTransform local = QuaternionMatrix(QuaternionSlerp(AngleQuaternion(a), AngleQuaternion(b), fraction));
            glm::vec3 offset;
            offset.x = rng.UnitF() * 20.0f;
            offset.y = rng.UnitF() * 20.0f;
            offset.z = rng.UnitF() * 20.0f;
            local[3] = offset;
            world = ConcatTransforms(world, local);
        }
        glm::vec3 point;
        point.x = rng.UnitF() * 10.0f;
        point.y = rng.UnitF() * 10.0f;
        point.z = rng.UnitF() * 10.0f;
        const glm::vec3 v = VectorTransform(point, world);
        chains.insert(chains.end(), {v.x, v.y, v.z});
    }
    const std::string directory = argc > 1 ? argv[1] : "";
    const auto report = [&directory](const char* name, const std::vector<float>& values) {
        std::printf("%-20s %8zu floats  %016" PRIx64 "\n", name, values.size(), Fnv(values));
        if (!directory.empty())
        {
            std::FILE* file = std::fopen((directory + "/" + name + ".bin").c_str(), "wb");
            std::fwrite(values.data(), sizeof(float), values.size(), file);
            std::fclose(file);
        }
    };
    report("quaternions", quaternions);
    report("matrices", matrices);
    report("round-matrices", roundMatrices);
    report("slerps", slerps);
    report("chains", chains);
    return 0;
}
