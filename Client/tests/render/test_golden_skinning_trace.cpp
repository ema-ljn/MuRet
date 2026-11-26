#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "Tables/PlayerModels.h"
#include "Models/Attachment.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Shine.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "support/GlTrace.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// MuRet's pose and skinning against what the original sent to GL in the lab traces
// (Client/docs/research/06-render-escenas-y-ui.md, "El personaje, vértice a vértice"). Each triangle of a mesh sends
// its three UVs in order, so a block's UV sequence identifies the mesh; each scene's numbers (the action, the frames,
// the place, the rotation and the scale) are those of 06.
namespace
{
using Client::Data::Models::HiddenTextureSlot;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::Data::Models::PlayerHeadBone;
using Client::Data::Models::SkinSlotOf;
using Client::Math::BoneTransform;
using Client::Models::AnimationState;
using Client::Models::MakeSkeleton;
using Client::Models::ObjectTransform;
using Client::Models::PoseSkeleton;
using Client::Models::ShinePass;
using Client::Models::ShineTexCoords;
using Client::Models::Skeleton;
using Client::Models::SkinMesh;
using Client::Models::SkinnedMesh;
using Client::Tests::GlTrace;
using Client::Tests::GlTraceBlock;
using Client::Tests::GlTraceFrame;
using Client::Tests::ReadGlTrace;

constexpr std::string_view SelectionTrace = "character-selection-Frame_003207.txt";
constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";
// The clock T of each Lorencia swap, in ms (06): the glow passes of its two characters use it.
constexpr std::array<std::int64_t, 5> LorenciaClocks = {30530748, 30531530, 30532328, 30533092, 30533858};
// The trace writes the UVs with six decimals.
constexpr float UvTolerance = 1e-5f;
// The most a MuRet vertex may be off the trace's, in world units (a terrain cell is 100). With the exact frame it gives
// 0.001: what a float rounds at coordinates of 15000.
constexpr float MaxError = 0.005f;
// How far the frame of 06's table may be off, in keys (SearchSteps steps of SearchStep each way), and the search step.
// The search absorbs an error equivalent to shifting the frame, like a t shifted evenly in all bones: the PoseTests
// catch that one. This test catches what shifting the frame a few hundredths does not fix (without the original's
// + 1, the interpolation reversed, a swapped axis).
constexpr float SearchStep = 0.0005f;
constexpr int SearchSteps = 60;

struct Part
{
    std::string_view name;
    int number = -1;
};

// A mesh in the order the original sends it, triangle by triangle: the UV, the vertex and the normal of each corner.
struct MeshSequence
{
    std::vector<glm::vec2> texCoords;
    std::vector<std::size_t> vertices;
    std::vector<std::size_t> normals;
};

MeshSequence Sequence(const Mesh& mesh)
{
    MeshSequence sequence;
    for (const auto& triangle : mesh.triangles)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto vertex = static_cast<std::size_t>(triangle.vertices[corner]);
            const auto& uv = mesh.texCoords[static_cast<std::size_t>(triangle.texCoords[corner])];
            sequence.texCoords.emplace_back(uv.u, uv.v);
            sequence.vertices.push_back(vertex);
            sequence.normals.push_back(static_cast<std::size_t>(triangle.normals[corner]));
        }
    }
    return sequence;
}

bool SameTexCoords(const GlTraceBlock& block, const MeshSequence& sequence)
{
    if (block.mode != "GL_TRIANGLES" || block.vertices.size() != sequence.texCoords.size())
    {
        return false;
    }
    for (std::size_t k = 0; k < sequence.texCoords.size(); ++k)
    {
        const glm::vec2 difference = block.vertices[k].texCoord - sequence.texCoords[k];
        if (std::fabs(difference.x) > UvTolerance || std::fabs(difference.y) > UvTolerance)
        {
            return false;
        }
    }
    return true;
}

// A mesh the original draws, with the blocks of each trace frame that have its UVs: there can be more than one (a
// shadow, another character with the same part), and the closest one counts.
struct TracedMesh
{
    std::string label;
    const Mesh* mesh = nullptr;
    MeshSequence sequence;
    std::vector<std::vector<const GlTraceBlock*>> blocks; // [frame]
    int partNumber = -1;
};

// A character of a trace, with 06's numbers: its parts, its action, each swap's frame, the object and the head angles
// at each swap (`+0x28` and `+0x2c` of the object, in degrees).
struct Scene
{
    std::vector<Part> parts;
    std::uint8_t action = 0;
    int keyCount = 0;
    std::array<float, 5> frames{};
    ObjectTransform object;
    std::array<glm::vec2, 5> headAngles{};
};

float BlockError(const GlTraceBlock& block, const MeshSequence& sequence, const std::vector<glm::vec3>& positions)
{
    float worst = 0.0f;
    for (std::size_t k = 0; k < sequence.vertices.size(); ++k)
    {
        worst = std::max(worst, glm::distance(block.vertices[k].position, positions[sequence.vertices[k]]));
    }
    return worst;
}

// The animation at `frame` (one of 06's table), as the original stores it: plus 1, modulo the keys, and its integer
// part as the previous key (06, "Cuánto avanza la animación").
AnimationState StateAt(const Scene& scene, float frame)
{
    return {scene.action, std::fmod(frame + 1.0f, static_cast<float>(scene.keyCount)), scene.action,
            static_cast<std::int16_t>(std::floor(frame))};
}

float FrameError(const Skeleton& skeleton, const std::vector<TracedMesh>& meshes, std::size_t f, float frame,
                 const Scene& scene)
{
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(1.0f));
    if (!PoseSkeleton(skeleton, StateAt(scene, frame), scene.object, scene.headAngles[f], bones))
    {
        return std::numeric_limits<float>::infinity();
    }
    float worst = 0.0f;
    for (const TracedMesh& traced : meshes)
    {
        SkinnedMesh skinned;
        if (!SkinMesh(*traced.mesh, bones, scene.object, skinned))
        {
            return std::numeric_limits<float>::infinity();
        }
        float best = std::numeric_limits<float>::infinity();
        for (const GlTraceBlock* block : traced.blocks[f])
        {
            best = std::min(best, BlockError(*block, traced.sequence, skinned.positions));
        }
        worst = std::max(worst, best);
    }
    return worst;
}

// Each drawn mesh of each part, with the blocks of each trace frame that have its UVs. The one the original sends to
// slot 0x12d is not drawn (CL6.1, SkinSlotOf).
std::vector<TracedMesh> TraceMeshes(const GlTrace& trace, const std::vector<Part>& parts,
                                    const std::vector<Model>& models)
{
    std::vector<TracedMesh> meshes;
    for (std::size_t p = 0; p < models.size(); ++p)
    {
        for (std::size_t m = 0; m < models[p].meshes.size(); ++m)
        {
            const Mesh& mesh = models[p].meshes[m];
            if (SkinSlotOf(mesh.textureName) == std::optional<std::uint16_t>{HiddenTextureSlot})
            {
                continue;
            }
            TracedMesh traced{std::string(parts[p].name) + std::to_string(parts[p].number) + ", malla " +
                                  std::to_string(m) + " (" + mesh.textureName + ")",
                              &mesh,
                              Sequence(mesh),
                              {},
                              parts[p].number};
            for (std::size_t f = 0; f < trace.frames.size(); ++f)
            {
                std::vector<const GlTraceBlock*> found;
                for (const GlTraceBlock& block : trace.frames[f].blocks)
                {
                    if (SameTexCoords(block, traced.sequence))
                    {
                        found.push_back(&block);
                    }
                }
                EXPECT_FALSE(found.empty()) << traced.label << ": ningún bloque del frame " << f << " tiene sus UV";
                traced.blocks.push_back(std::move(found));
            }
            meshes.push_back(std::move(traced));
        }
    }
    return meshes;
}

// The frame closest to swap `f`: how many SearchStep steps it is from the table's, and its error.
struct FrameFit
{
    int step = 0;
    float error = std::numeric_limits<float>::infinity();
};

FrameFit FitFrame(const Skeleton& skeleton, const std::vector<TracedMesh>& meshes, std::size_t f, float tableFrame,
                  const Scene& scene)
{
    FrameFit best;
    for (int step = -SearchSteps; step <= SearchSteps; ++step)
    {
        const float error = FrameError(skeleton, meshes, f, tableFrame + static_cast<float>(step) * SearchStep, scene);
        if (error < best.error)
        {
            best = FrameFit{step, error};
        }
    }
    return best;
}

class GoldenSkinningTrace : public Client::Tests::GoldenAndGlTraceTest
{
protected:
    std::optional<Model> LoadPlayerModel(std::string_view name, int number) const
    {
        auto loaded = LoadModelFile(m_dataRoot, ModelFile{0, "Player\\", name, number});
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << "no cargó " << loaded.Error().path;
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }

    std::optional<GlTrace> LoadTrace(std::string_view name) const
    {
        const auto path = TraceFile(name);
        if (!path)
        {
            return std::nullopt;
        }
        auto trace = ReadGlTrace(*path);
        if (!trace.HasValue())
        {
            ADD_FAILURE() << name << ": " << trace.Error();
            return std::nullopt;
        }
        return std::move(trace).Value();
    }

    // Player.bmd with its head (bone 20, as 0x60dbe0 leaves it): in Lorencia it turns with each swap's angles, and in
    // the selection, with 0, it follows the animation.
    std::optional<Skeleton> LoadPlayerSkeleton() const
    {
        const std::optional<Model> player = LoadPlayerModel("Player", -1);
        if (!player)
        {
            return std::nullopt;
        }
        auto skeleton = MakeSkeleton(*player);
        if (!skeleton.HasValue())
        {
            ADD_FAILURE() << "Player.bmd: el hueso " << skeleton.Error().bone << " cuelga del "
                          << skeleton.Error().parent;
            return std::nullopt;
        }
        Skeleton made = std::move(skeleton).Value();
        made.headBone = PlayerHeadBone;
        return made;
    }

    std::optional<std::vector<Model>> LoadParts(const std::vector<Part>& parts) const
    {
        std::vector<Model> models;
        for (const Part& part : parts)
        {
            std::optional<Model> model = LoadPlayerModel(part.name, part.number);
            if (!model)
            {
                return std::nullopt;
            }
            models.push_back(std::move(*model));
        }
        return models;
    }

    // 06 gives the frames with three decimals: each swap's frame is searched near the table's, and with it every vertex
    // must land within MaxError of the trace's, without the best one at the edge of the search.
    void ExpectTheTrace(std::string_view traceName, const Scene& scene)
    {
        const std::optional<GlTrace> trace = LoadTrace(traceName);
        const std::optional<Skeleton> skeleton = LoadPlayerSkeleton();
        const std::optional<std::vector<Model>> models = LoadParts(scene.parts);
        ASSERT_TRUE(trace && skeleton && models);
        ASSERT_EQ(trace->frames.size(), scene.frames.size());
        const std::vector<TracedMesh> meshes = TraceMeshes(*trace, scene.parts, *models);

        for (std::size_t f = 0; f < scene.frames.size(); ++f)
        {
            const float tableFrame = scene.frames[f];
            const FrameFit fit = FitFrame(*skeleton, meshes, f, tableFrame, scene);
            const float bestFrame = tableFrame + static_cast<float>(fit.step) * SearchStep;
            EXPECT_LT(fit.error, MaxError)
                << "frame " << f << ": " << tableFrame << " en la tabla, " << bestFrame << " el que más se acerca";
            EXPECT_LT(std::abs(fit.step), SearchSteps)
                << "frame " << f << ": el mejor está en el borde de la búsqueda (" << bestFrame << ")";
            RecordProperty("frame" + std::to_string(f), std::to_string(bestFrame) + " " + std::to_string(fit.error));
        }
    }
};

std::vector<Part> TracedPlayerParts()
{
    return {{"HelmMale", 17}, {"ArmorMale", 17}, {"PantMale", 2}, {"GloveMale", 17}, {"BootMale", 17}};
}

// The selection: the character of place 0 of 0x4c20a0 at z = 163, with its class's 0.9 scale, in action 3 (seven
// keys) of Player.bmd. The head follows the animation: its angles, at 0.
Scene SelectionPlayer()
{
    return {TracedPlayerParts(),
            3,
            7,
            {1.740f, 3.320f, 4.900f, 6.740f, 1.580f},
            {{12880.0f, 15046.0f, 163.0f}, {0.0f, 0.0f, 100.0f}, 0.9f, 0.0f},
            {}};
}

// The head of a Lorencia character as 0x505de0 turns it (06, "La cabeza gira aparte"): at swap 0 it is at `first`,
// measured in the trace, and at each animation step it gets 20 % closer to `target`, with the steps the body takes
// between swaps (20, 19, 19 and 19).
std::array<glm::vec2, 5> TurningHead(glm::vec2 first, glm::vec2 target)
{
    constexpr float TurnPerStep = 0.2f;
    constexpr std::array<int, 4> StepsBetweenSwaps = {20, 19, 19, 19};
    std::array<glm::vec2, 5> angles{};
    angles[0] = first;
    for (std::size_t f = 1; f < angles.size(); ++f)
    {
        angles[f] = angles[f - 1];
        for (int step = 0; step < StepsBetweenSwaps[f - 1]; ++step)
        {
            angles[f] += (target - angles[f]) * TurnPerStep;
        }
    }
    return angles;
}

// Lorencia: the character at the center of cell (143, 120), at z = 165, in action 1 (six keys), rotated 116°. The head
// does not follow the animation: it looks at (0, −0.65)°, and at swap 0 it is still 0.0065° short in x.
Scene LorenciaPlayer()
{
    return {TracedPlayerParts(),
            1,
            6,
            {1.801f, 1.401f, 0.721f, 0.041f, 5.361f},
            {{14350.0f, 12050.0f, 165.0f}, {0.0f, 0.0f, 116.0f}, 0.9f, 0.0f},
            TurningHead({-0.0065f, -0.65f}, {0.0f, -0.65f})};
}

// Lorencia: the guard with player parts, at the center of cell (143, 128), at z = 165, in action 1, rotated 90°. Its
// head goes towards (−18, 4)°, one of the random targets of 0x505de0, and at swap 0 it is at (−22.399, 0.0205)°.
// MaskHelmMale10 is left out: of its four meshes only guard_hair has a block in the trace, and it is the one of the
// identical HelmMale10 mesh (06).
Scene LorenciaGuard()
{
    return {{{"HelmMale", 10}, {"ArmorMale", 10}, {"PantMale", 10}, {"GloveMale", 10}, {"BootMale", 10}},
            1,
            6,
            {0.681f, 0.281f, 5.601f, 4.921f, 4.241f},
            {{14350.0f, 12850.0f, 165.0f}, {0.0f, 0.0f, 90.0f}, 0.9f, 0.0f},
            TurningHead({-22.399f, 0.0205f}, {-18.0f, 4.0f})};
}

TEST_F(GoldenSkinningTrace, MatchesTheFiveFramesOfTheSelection)
{
    ExpectTheTrace(SelectionTrace, SelectionPlayer());
}

TEST_F(GoldenSkinningTrace, MatchesThePlayerOfLorencia)
{
    ExpectTheTrace(LorenciaTrace, LorenciaPlayer());
}

TEST_F(GoldenSkinningTrace, MatchesTheGuardOfLorencia)
{
    ExpectTheTrace(LorenciaTrace, LorenciaGuard());
}

// What hangs in the selection (CL6.3): Sword17 from the right hand (bone 33, no offset) and Helper02 from 34 with
// (20, 0, 0) rotated by the bone, each posed from that root (0x509340) and taken to the world with the character's
// object. The sword has one key; the pet advances 0.5 per frame (Q-C20), and over the five swaps it is at 1.5, 2, 2.5,
// 3 and 3.5 (06).
struct HangingModel
{
    ModelFile file;
    Client::Models::AttachmentSlot slot;
    std::array<float, 5> frames;
};

TEST_F(GoldenSkinningTrace, TheSwordAndThePetHangFromTheirBones)
{
    using Client::Models::AttachmentOf;
    using Client::Models::AttachmentRoot;
    using Client::Models::AttachmentSlot;
    using Client::Models::PoseAttached;
    const Scene scene = SelectionPlayer();
    const std::optional<GlTrace> trace = LoadTrace(SelectionTrace);
    const std::optional<Skeleton> skeleton = LoadPlayerSkeleton();
    ASSERT_TRUE(trace && skeleton);
    const std::array<HangingModel, 2> hanging = {{
        {ModelFile{0x222, "Item\\", "Sword", 17}, AttachmentSlot::RightHand, {0, 0, 0, 0, 0}},
        {ModelFile{0x1c13, "Player\\", "Helper", 2}, AttachmentSlot::Pet, {1.5f, 2.0f, 2.5f, 3.0f, 3.5f}},
    }};
    for (const HangingModel& model : hanging)
    {
        auto loaded = LoadModelFile(m_dataRoot, model.file);
        ASSERT_TRUE(loaded.HasValue()) << model.file.name;
        const std::vector<Model> models = {std::move(loaded).Value()};
        auto made = MakeSkeleton(models[0]);
        ASSERT_TRUE(made.HasValue()) << model.file.name;
        const Skeleton hangingSkeleton = std::move(made).Value();
        const int keys = hangingSkeleton.actions[0].keyCount;
        const std::vector<TracedMesh> meshes = TraceMeshes(*trace, {{model.file.name, model.file.number}}, models);
        for (std::size_t f = 0; f < scene.frames.size(); ++f)
        {
            std::vector<BoneTransform> body(skeleton->bones.size(), BoneTransform(1.0f));
            ASSERT_TRUE(
                PoseSkeleton(*skeleton, StateAt(scene, scene.frames[f]), scene.object, scene.headAngles[f], body));
            const auto attachment = AttachmentOf(model.slot);
            const BoneTransform root =
                AttachmentRoot(body[static_cast<std::size_t>(attachment.bone)], attachment.offset);
            const AnimationState state{0, std::fmod(model.frames[f] + 1.0f, static_cast<float>(keys)), 0,
                                       static_cast<std::int16_t>(std::floor(model.frames[f]))};
            std::vector<BoneTransform> bones(hangingSkeleton.bones.size(), BoneTransform(1.0f));
            ASSERT_TRUE(PoseAttached(hangingSkeleton, state, root, bones));
            for (const TracedMesh& traced : meshes)
            {
                SkinnedMesh skinned;
                ASSERT_TRUE(SkinMesh(*traced.mesh, bones, scene.object, skinned));
                float best = std::numeric_limits<float>::infinity();
                for (const GlTraceBlock* block : traced.blocks[f])
                {
                    best = std::min(best, BlockError(*block, traced.sequence, skinned.positions));
                }
                EXPECT_LT(best, MaxError) << traced.label << ", frame " << f;
            }
        }
    }
}

// The selection's glow passes by their texture in the trace (06): 115, 120 and 141.
struct TracedShinePass
{
    std::uint32_t texture = 0;
    ShinePass pass = ShinePass::Shiny;
};
constexpr std::array<TracedShinePass, 3> ShinePasses = {{
    {115, ShinePass::Shiny},
    {120, ShinePass::Chrome},
    {141, ShinePass::Chrome4},
}};
// A glow pass repeats its mesh's positions.
constexpr float SamePosition = 0.01f;
// The most a MuRet UV may be off the trace's: it gives 0.00003.
constexpr float MaxUvError = 0.001f;

const TracedShinePass* ShinePassOf(std::uint32_t texture)
{
    for (const TracedShinePass& traced : ShinePasses)
    {
        if (traced.texture == texture)
        {
            return &traced;
        }
    }
    return nullptr;
}

bool SamePositions(const GlTraceBlock& a, const GlTraceBlock& b)
{
    if (a.vertices.size() != b.vertices.size())
    {
        return false;
    }
    for (std::size_t k = 0; k < a.vertices.size(); ++k)
    {
        if (glm::distance(a.vertices[k].position, b.vertices[k].position) > SamePosition)
        {
            return false;
        }
    }
    return true;
}

// How many glow passes of a mesh are in the frame, and the UV furthest from MuRet's: a pass repeats the positions of
// the mesh's `base` block, with its pass's texture.
struct ShineResult
{
    std::size_t passes = 0;
    float worst = 0.0f;
};

ShineResult CompareShine(const GlTraceFrame& frame, const GlTraceBlock& base, const MeshSequence& sequence,
                         const SkinnedMesh& skinned, std::int64_t clockMs)
{
    ShineResult result;
    for (const GlTraceBlock& block : frame.blocks)
    {
        const TracedShinePass* traced = ShinePassOf(block.texture);
        if (traced == nullptr || !SamePositions(block, base))
        {
            continue;
        }
        ++result.passes;
        for (std::size_t k = 0; k < sequence.normals.size(); ++k)
        {
            const glm::vec2 expected =
                ShineTexCoords(traced->pass, skinned.normals[sequence.normals[k]], std::chrono::milliseconds{clockMs});
            const glm::vec2 difference = block.vertices[k].texCoord - expected;
            result.worst = std::max({result.worst, std::fabs(difference.x), std::fabs(difference.y)});
        }
    }
    return result;
}

// How many glow passes the traces show for each part set: the character's Male17, three (a level 13, 06); the guard's
// Male10, one (the 120); pants 02, none.
std::size_t TracedShinePassesOf(int partNumber)
{
    switch (partNumber)
    {
    case 17:
        return ShinePasses.size();
    case 10:
        return 1;
    default:
        return 0;
    }
}

class GoldenShineTrace : public GoldenSkinningTrace
{
protected:
    // The glow passes of the meshes of `scene`: with the normals SkinMesh gives at 06's frames (rotated by the bones,
    // without the object's scale) and each swap's clock T (also from 06), MuRet's UVs land on the trace's.
    void ExpectTheShine(std::string_view traceName, const Scene& scene, const std::array<std::int64_t, 5>& clocks)
    {
        const std::optional<GlTrace> trace = LoadTrace(traceName);
        const std::optional<Skeleton> skeleton = LoadPlayerSkeleton();
        const std::optional<std::vector<Model>> models = LoadParts(scene.parts);
        ASSERT_TRUE(trace && skeleton && models);
        ASSERT_EQ(trace->frames.size(), scene.frames.size());
        const std::vector<TracedMesh> meshes = TraceMeshes(*trace, scene.parts, *models);

        float worst = 0.0f;
        for (std::size_t f = 0; f < scene.frames.size(); ++f)
        {
            std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
            ASSERT_TRUE(
                PoseSkeleton(*skeleton, StateAt(scene, scene.frames[f]), scene.object, scene.headAngles[f], bones));
            for (const TracedMesh& traced : meshes)
            {
                SkinnedMesh skinned;
                ASSERT_TRUE(!traced.blocks[f].empty() && SkinMesh(*traced.mesh, bones, scene.object, skinned))
                    << traced.label;
                const ShineResult result =
                    CompareShine(trace->frames[f], *traced.blocks[f].front(), traced.sequence, skinned, clocks[f]);
                EXPECT_EQ(result.passes, TracedShinePassesOf(traced.partNumber))
                    << "frame " << f << ", " << traced.label;
                worst = std::max(worst, result.worst);
            }
        }
        EXPECT_LT(worst, MaxUvError) << "la UV que más se aleja";
        RecordProperty("worst", std::to_string(worst));
    }
};

TEST_F(GoldenShineTrace, MatchesThePassesOfTheSelection)
{
    ExpectTheShine(SelectionTrace, SelectionPlayer(), {26437420, 26438716, 26440046, 26441452, 26442812});
}

TEST_F(GoldenShineTrace, MatchesThePassesOfThePlayerOfLorencia)
{
    ExpectTheShine(LorenciaTrace, LorenciaPlayer(), LorenciaClocks);
}

TEST_F(GoldenShineTrace, MatchesThePassOfTheGuardOfLorencia)
{
    ExpectTheShine(LorenciaTrace, LorenciaGuard(), LorenciaClocks);
}
} // namespace
