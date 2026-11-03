#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
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
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkinMesh;
using Client::Render::Models::SkinnedMesh;
using Client::Tests::GlTrace;
using Client::Tests::GlTraceBlock;
using Client::Tests::ReadGlTrace;

constexpr std::string_view SelectionTrace = "character-selection-Frame_003207.txt";
constexpr std::string_view LorenciaTrace = "lorencia-Frame_016215.txt";
// La traza escribe las UV con seis decimales.
constexpr float UvTolerance = 1e-5f;
// Lo más que un vértice de MuRet puede quedar del de la traza, en unidades del mundo (una celda del
// terreno son 100). Con el frame justo da 0,001: lo que redondea un float en coordenadas de 15.000.
constexpr float MaxError = 0.005f;
// Cuánto puede estar corrido el frame de la tabla del 06, en keys, y de a cuánto se busca. Un error
// de verdad (sin el + 1 del original, la interpolación al revés, un eje cambiado) no se arregla
// corriendo el frame unas centésimas.
constexpr float SearchRange = 0.03f;
constexpr float SearchStep = 0.0005f;
constexpr int SearchSteps = 60;
static_assert(static_cast<float>(SearchSteps) * SearchStep < SearchRange + 0.0001f);

struct Part
{
    std::string_view name;
    int number = -1;
};

// Los huesos que cuelgan de la cabeza, ella incluida.
std::set<std::size_t> HeadBones(const Skeleton& skeleton)
{
    std::set<std::size_t> head{static_cast<std::size_t>(PlayerHeadBone)};
    for (std::size_t bone = 0; bone < skeleton.bones.size(); ++bone)
    {
        const std::int16_t parent = skeleton.bones[bone].parent;
        if (parent >= 0 && head.contains(static_cast<std::size_t>(parent)))
        {
            head.insert(bone);
        }
    }
    return head;
}

// Una malla en el orden en que el original la manda, triángulo por triángulo: la UV y el vértice de
// cada esquina, y si ese vértice cuelga de la cabeza.
struct MeshSequence
{
    std::vector<glm::vec2> texCoords;
    std::vector<std::size_t> vertices;
    std::vector<bool> head;
};

MeshSequence Sequence(const Mesh& mesh, const std::set<std::size_t>& head)
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
            sequence.head.push_back(head.contains(static_cast<std::size_t>(mesh.vertices[vertex].bone)));
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
};

// Cómo se compara una escena: el objeto, y si su lugar sale del promedio y si la cabeza cuenta.
struct Scene
{
    std::uint8_t action = 0;
    int keyCount = 0;
    ObjectTransform object;
    bool fitPosition = false;
    bool withoutHead = false;
};

// El error más grande de `block` contra `positions` en el orden de `sequence`. Con fitPosition, la
// posición del objeto es lo que el bloque está corrido en promedio, sin la cabeza (como el ajuste del
// 05): el lugar del guardia no está en ninguna tabla.
float BlockError(const GlTraceBlock& block, const MeshSequence& sequence, const std::vector<glm::vec3>& positions,
                 const Scene& scene)
{
    glm::vec3 offset{0.0f};
    if (scene.fitPosition)
    {
        std::size_t count = 0;
        for (std::size_t k = 0; k < sequence.vertices.size(); ++k)
        {
            if (!sequence.head[k])
            {
                offset += block.vertices[k].position - positions[sequence.vertices[k]];
                ++count;
            }
        }
        offset = count == 0 ? offset : offset / static_cast<float>(count);
    }
    float worst = 0.0f;
    for (std::size_t k = 0; k < sequence.vertices.size(); ++k)
    {
        if (!(scene.withoutHead && sequence.head[k]))
        {
            worst =
                std::max(worst, glm::distance(block.vertices[k].position, positions[sequence.vertices[k]] + offset));
        }
    }
    return worst;
}

// El peor vértice de todas las mallas en el frame `f` de la traza, con la animación en `frame` (el de
// la tabla del 06, como lo guarda el original: más 1, módulo las keys, y su parte entera como la key
// anterior; 06, "Cuánto avanza la animación").
float FrameError(const Skeleton& skeleton, const std::vector<TracedMesh>& meshes, std::size_t f, float frame,
                 const Scene& scene)
{
    const AnimationState state{scene.action, std::fmod(frame + 1.0f, static_cast<float>(scene.keyCount)), scene.action,
                               static_cast<std::int16_t>(std::floor(frame))};
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(1.0f));
    if (!PoseSkeleton(skeleton, state, scene.object, {0.0f, 0.0f}, bones))
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
            best = std::min(best, BlockError(*block, traced.sequence, skinned.positions, scene));
        }
        worst = std::max(worst, best);
    }
    return worst;
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

    // Posa Player.bmd con cada frame del 06 y compara las partes con la traza. El 06 da los frames con
    // tres decimales: el de cada swap se busca a ±SearchRange keys del de la tabla, y con él cada
    // vértice tiene que caer a menos de MaxError del de la traza.
    void ExpectTheTrace(std::string_view traceName, const std::vector<Part>& parts, const std::array<float, 5>& frames,
                        const Scene& scene)
    {
        const std::optional<GlTrace> trace = LoadTrace(traceName);
        const std::optional<Model> player = LoadPlayerModel("Player", -1);
        ASSERT_TRUE(trace && player);
        ASSERT_EQ(trace->frames.size(), frames.size());
        const auto skeleton = MakeSkeleton(*player);
        ASSERT_TRUE(skeleton.HasValue()) << "Player.bmd: el hueso " << skeleton.Error().bone << " cuelga del "
                                         << skeleton.Error().parent;
        const std::set<std::size_t> head = HeadBones(skeleton.Value());

        std::vector<Model> models;
        for (const Part& part : parts)
        {
            std::optional<Model> model = LoadPlayerModel(part.name, part.number);
            ASSERT_TRUE(model.has_value());
            models.push_back(std::move(*model));
        }
        std::vector<TracedMesh> meshes;
        for (std::size_t p = 0; p < models.size(); ++p)
        {
            for (std::size_t m = 0; m < models[p].meshes.size(); ++m)
            {
                const Mesh& mesh = models[p].meshes[m];
                // La que el original manda al slot 0x12d no se dibuja (CL6.1, SkinSlotOf).
                if (SkinSlotOf(mesh.textureName) == std::optional<std::uint16_t>{HiddenTextureSlot})
                {
                    continue;
                }
                TracedMesh traced{std::string(parts[p].name) + std::to_string(parts[p].number) + ", malla " +
                                      std::to_string(m) + " (" + mesh.textureName + ")",
                                  &mesh,
                                  Sequence(mesh, head),
                                  {}};
                for (std::size_t f = 0; f < frames.size(); ++f)
                {
                    std::vector<const GlTraceBlock*> found;
                    for (const GlTraceBlock& block : trace->frames[f].blocks)
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

        for (std::size_t f = 0; f < frames.size(); ++f)
        {
            float bestFrame = frames[f];
            float bestError = std::numeric_limits<float>::infinity();
            for (int step = -SearchSteps; step <= SearchSteps; ++step)
            {
                const float frame = frames[f] + static_cast<float>(step) * SearchStep;
                const float error = FrameError(skeleton.Value(), meshes, f, frame, scene);
                if (error < bestError)
                {
                    bestError = error;
                    bestFrame = frame;
                }
            }
            EXPECT_LT(bestError, MaxError)
                << "frame " << f << ": " << frames[f] << " en la tabla, " << bestFrame << " el que más se acerca";
            EXPECT_LT(std::fabs(bestFrame - frames[f]), SearchRange)
                << "frame " << f << ": el mejor está en el borde de la búsqueda (" << bestFrame << ")";
            RecordProperty("frame" + std::to_string(f), std::to_string(bestFrame) + " " + std::to_string(bestError));
        }
    }
};

// La selección: el personaje del lugar 0 de 0x4c20a0 en z = 163, con la escala 0,9 de su clase, en la
// acción 3 (siete keys) de Player.bmd. La cabeza sigue la animación.
TEST_F(GoldenSkinningTrace, MatchesTheFiveFramesOfTheSelection)
{
    const std::vector<Part> parts = {
        {"HelmMale", 17}, {"ArmorMale", 17}, {"PantMale", 2}, {"GloveMale", 17}, {"BootMale", 17}};
    const Scene scene{3, 7, {{12880.0f, 15046.0f, 163.0f}, {0.0f, 0.0f, 100.0f}, 0.9f, 0.0f}, false, false};

    ExpectTheTrace(SelectionTrace, parts, {1.740f, 3.320f, 4.900f, 6.740f, 1.580f}, scene);
}

// Lorencia: el guardia con partes de jugador, en la acción 1 (seis keys), girado 90°. Su lugar no está
// en ninguna tabla: sale del promedio de cada bloque. La cabeza no sigue la animación (06) y no se
// compara: el casco (HelmMale10 y MaskHelmMale10) cuelga entero de ella.
TEST_F(GoldenSkinningTrace, MatchesTheGuardOfLorencia)
{
    const std::vector<Part> parts = {{"ArmorMale", 10}, {"PantMale", 10}, {"GloveMale", 10}, {"BootMale", 10}};
    const Scene scene{1, 6, {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 90.0f}, 0.9f, 0.0f}, true, true};

    ExpectTheTrace(LorenciaTrace, parts, {0.681f, 0.281f, 5.601f, 4.921f, 4.241f}, scene);
}
} // namespace
