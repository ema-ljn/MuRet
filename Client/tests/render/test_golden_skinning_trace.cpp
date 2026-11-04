#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Shine.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
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

// La pose y el skinning de MuRet contra lo que el original le mandó a GL en las trazas del
// laboratorio (Client/docs/research/06-render-escenas-y-ui.md, "El personaje, vértice a vértice").
// Cada triángulo de una malla manda sus tres UV en orden, así que la secuencia de UV de un bloque
// identifica la malla; los números de cada escena (la acción, los frames, el lugar, el giro y la
// escala) son los del 06.
namespace
{
using Client::Data::Models::HiddenTextureSlot;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::Data::Models::PlayerHeadBone;
using Client::Data::Models::SkinSlotOf;
using Client::Render::Models::AnimationState;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::MakeSkeleton;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::PoseSkeleton;
using Client::Render::Models::ShinePass;
using Client::Render::Models::ShineTexCoords;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkinMesh;
using Client::Render::Models::SkinnedMesh;
using Client::Tests::GlTrace;
using Client::Tests::GlTraceBlock;
using Client::Tests::GlTraceFrame;
using Client::Tests::ReadGlTrace;

constexpr std::string_view SelectionTrace = "character-selection-Frame_003207.txt";
constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";
// El reloj T de cada swap de Lorencia, en ms (06): lo usan los pases de brillo de sus dos personajes.
constexpr std::array<std::int64_t, 5> LorenciaClocks = {30530748, 30531530, 30532328, 30533092, 30533858};
// La traza escribe las UV con seis decimales.
constexpr float UvTolerance = 1e-5f;
// Lo más que un vértice de MuRet puede quedar del de la traza, en unidades del mundo (una celda del
// terreno son 100). Con el frame justo da 0,001: lo que redondea un float en coordenadas de 15.000.
constexpr float MaxError = 0.005f;
// Cuánto puede estar corrido el frame de la tabla del 06, en keys (SearchSteps pasos de SearchStep
// para cada lado), y de a cuánto se busca. La búsqueda absorbe un error que equivale a correr el
// frame, como una t corrida pareja en todos los huesos: ese lo atrapan los PoseTest. Lo que no se
// arregla corriendo el frame unas centésimas (sin el + 1 del original, la interpolación al revés, un
// eje cambiado) lo atrapa este.
constexpr float SearchStep = 0.0005f;
constexpr int SearchSteps = 60;

struct Part
{
    std::string_view name;
    int number = -1;
};

// Una malla en el orden en que el original la manda, triángulo por triángulo: la UV, el vértice y la
// normal de cada esquina.
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

// Una malla que el original dibuja, con los bloques de cada frame de la traza que tienen sus UV: puede
// haber más de uno (una sombra, otro personaje con la misma parte), y vale el que más se acerca.
struct TracedMesh
{
    std::string label;
    const Mesh* mesh = nullptr;
    MeshSequence sequence;
    std::vector<std::vector<const GlTraceBlock*>> blocks; // [frame]
    int partNumber = -1;
};

// Un personaje de una traza, con los números del 06: sus partes, su acción, el frame de cada swap, el
// objeto y los ángulos de la cabeza en cada swap (los de `+0x28` y `+0x2c` del objeto, en grados).
struct Scene
{
    std::vector<Part> parts;
    std::uint8_t action = 0;
    int keyCount = 0;
    std::array<float, 5> frames{};
    ObjectTransform object;
    std::array<glm::vec2, 5> headAngles{};
};

// El error más grande de `block` contra `positions` en el orden de `sequence`.
float BlockError(const GlTraceBlock& block, const MeshSequence& sequence, const std::vector<glm::vec3>& positions)
{
    float worst = 0.0f;
    for (std::size_t k = 0; k < sequence.vertices.size(); ++k)
    {
        worst = std::max(worst, glm::distance(block.vertices[k].position, positions[sequence.vertices[k]]));
    }
    return worst;
}

// La animación en `frame` (uno de la tabla del 06), como la guarda el original: más 1, módulo las keys,
// y su parte entera como la key anterior (06, "Cuánto avanza la animación").
AnimationState StateAt(const Scene& scene, float frame)
{
    return {scene.action, std::fmod(frame + 1.0f, static_cast<float>(scene.keyCount)), scene.action,
            static_cast<std::int16_t>(std::floor(frame))};
}

// El peor vértice de todas las mallas en el swap `f` de la traza, con la animación en `frame`.
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

// Cada malla que se dibuja de cada parte, con los bloques de cada frame de la traza que tienen sus UV.
// La que el original manda al slot 0x12d no se dibuja (CL6.1, SkinSlotOf).
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

// El frame que más se acerca al swap `f`: cuántos pasos de SearchStep está del de la tabla, y su error.
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

    // Player.bmd con su cabeza (el hueso 20, como lo deja 0x60dbe0): en Lorencia gira con los ángulos
    // de cada swap, y en la selección, con 0, sigue la animación.
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

    // Posa Player.bmd con cada frame del 06 y compara las partes con la traza. El 06 da los frames con
    // tres decimales: el de cada swap se busca cerca del de la tabla, y con él cada vértice tiene que
    // caer a menos de MaxError del de la traza, sin que el mejor quede en el borde de la búsqueda.
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

// El personaje de las dos trazas: HelmMale17, ArmorMale17, PantMale02, GloveMale17 y BootMale17.
std::vector<Part> TracedPlayerParts()
{
    return {{"HelmMale", 17}, {"ArmorMale", 17}, {"PantMale", 2}, {"GloveMale", 17}, {"BootMale", 17}};
}

// La selección: el personaje del lugar 0 de 0x4c20a0 en z = 163, con la escala 0,9 de su clase, en la
// acción 3 (siete keys) de Player.bmd. La cabeza sigue la animación: sus ángulos, en 0.
Scene SelectionPlayer()
{
    return {TracedPlayerParts(),
            3,
            7,
            {1.740f, 3.320f, 4.900f, 6.740f, 1.580f},
            {{12880.0f, 15046.0f, 163.0f}, {0.0f, 0.0f, 100.0f}, 0.9f, 0.0f},
            {}};
}

// La cabeza de un personaje de Lorencia como la gira 0x505de0 (06, "La cabeza gira aparte"): en el
// swap 0 está en `first`, medida en la traza, y en cada paso de la animación se acerca un 20 % a
// `target`, con los pasos que da el cuerpo entre swaps (20, 19, 19 y 19).
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

// Lorencia: el personaje en el centro de la celda (143, 120), a z = 165, en la acción 1 (seis keys),
// girado 116°. La cabeza no sigue la animación: mira a (0, −0,65)°, y en el swap 0 le falta llegar
// 0,0065° en x.
Scene LorenciaPlayer()
{
    return {TracedPlayerParts(),
            1,
            6,
            {1.801f, 1.401f, 0.721f, 0.041f, 5.361f},
            {{14350.0f, 12050.0f, 165.0f}, {0.0f, 0.0f, 116.0f}, 0.9f, 0.0f},
            TurningHead({-0.0065f, -0.65f}, {0.0f, -0.65f})};
}

// Lorencia: el guardia con partes de jugador, en el centro de la celda (143, 128), a z = 165, en la
// acción 1, girado 90°. Su cabeza va hacia (−18, 4)°, un objetivo al azar de los de 0x505de0, y en el
// swap 0 está en (−22,399, 0,0205)°. MaskHelmMale10 no va: de sus cuatro mallas solo guard_hair tiene
// un bloque en la traza, y es el de la malla igual de HelmMale10 (06).
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

// Lo que cuelga en la selección (CL6.3): Sword17 de la mano derecha (el hueso 33, sin desplazamiento)
// y Helper02 del 34 con (20, 0, 0) girado por el hueso, cada uno posado desde esa raíz (0x509340)
// y llevado al mundo con el objeto del personaje. La espada tiene una key; la mascota avanza 0,5 por
// frame (Q-C20), y en los cinco swaps está en 1,5 · 2 · 2,5 · 3 · 3,5 (06).
struct HangingModel
{
    ModelFile file;
    Client::Render::Models::AttachmentSlot slot;
    std::array<float, 5> frames;
};

TEST_F(GoldenSkinningTrace, TheSwordAndThePetHangFromTheirBones)
{
    using Client::Render::Models::AttachmentOf;
    using Client::Render::Models::AttachmentRoot;
    using Client::Render::Models::AttachmentSlot;
    using Client::Render::Models::PoseAttached;
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

// Los pases de brillo de la selección por su textura en la traza (06): 115, 120 y 141.
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
// Un pase de brillo repite las posiciones de su malla.
constexpr float SamePosition = 0.01f;
// Lo más que una UV de MuRet puede quedar de la de la traza: da 0,00003.
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

// Cuántos pases de brillo de una malla hay en el frame, y la UV que más se aleja de la de MuRet: un pase
// repite las posiciones del bloque `base` de la malla, con la textura de su pase.
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

// Cuántos pases de brillo muestran las trazas en cada juego de partes: las Male17 del personaje, los
// tres (un nivel 13, 06); las Male10 del guardia, uno (el 120); el pantalón 02, ninguno.
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
    // Los pases de brillo de las mallas de `scene`: con las normales que da SkinMesh en los frames del
    // 06 (giradas por los huesos, sin la escala del objeto) y el reloj T de cada swap (también del 06),
    // las UV de MuRet caen sobre las de la traza.
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
