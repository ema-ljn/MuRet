#include "Scenes/CharacterDrawing.h"

#include "App/WindowState.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneCameras.h"

#include "support/GlTrace.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::DrawCharacter contra la traza de la selección (MURET_CLIENT_GL_TRACE_DIR y
// MURET_CLIENT_GOLDEN_DIR): el personaje de la traza (las partes Male17 de nivel 13, el pantalón 02,
// Sword17 de nivel 13 en la mano derecha y Helper02) en los cinco swaps, con sus frames, sus relojes
// y su lugar (06). Cada bloque que el original le mandó a GL por ese personaje (cada malla y cada pase
// de brillo) tiene que estar en el lote de MuRet como un tramo de triángulos seguidos, con las mismas
// posiciones y UV, y el lote no tiene nada más: uno a uno.
namespace
{
using Client::App::PixelSize;
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::Data::Models::PlayerModelFiles;
using Client::GameLogic::CharacterAppearance;
using Client::GameLogic::WornItem;
using Client::Render::MakePass3D;
using Client::Render::ModelBatch;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::Render::Models::AnimationState;
using Client::Render::Models::MakePlayerSkeleton;
using Client::Render::Models::MakeSkeleton;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::Skeleton;
using Client::Scenes::DrawCharacter;
using Client::Scenes::DrawnCharacter;
using Client::Scenes::IModelLibrary;
using Client::Scenes::SelectionCamera;
using Client::Scenes::WorldViewport;
using Client::Tests::GlTrace;
using Client::Tests::GlTraceBlock;
using Client::Tests::ReadGlTrace;

constexpr std::string_view SelectionTrace = "character-selection-Frame_003207.txt";

// El personaje de la traza (06 § El personaje, vértice a vértice): las partes, su nivel y lo que
// lleva.
constexpr std::uint16_t HelmMale17 = 0x1012 + 16;
constexpr std::uint16_t ArmorMale17 = 0x1212 + 16;
constexpr std::uint16_t PantMale02 = 0x1412 + 1;
constexpr std::uint16_t GloveMale17 = 0x1612 + 16;
constexpr std::uint16_t BootMale17 = 0x1812 + 16;
constexpr std::uint16_t Sword17 = 0x212 + 16;
constexpr std::uint16_t Helper02 = 0x1c13;
constexpr std::uint8_t TracedLevel = 13;

// Los frames del cuerpo y de la mascota y el reloj de cada swap (06).
constexpr std::array<float, 5> BodyFrames = {1.740f, 3.320f, 4.900f, 6.740f, 1.580f};
constexpr std::array<float, 5> PetFrames = {1.5f, 2.0f, 2.5f, 3.0f, 3.5f};
constexpr std::array<std::int64_t, 5> Clocks = {26437420, 26438716, 26440046, 26441452, 26442812};

// Lo que puede diferir un vértice y una UV de los de la traza (como los otros golden).
constexpr float MaxError = 0.005f;
constexpr float MaxUvError = 0.001f;

// Los modelos del personaje, cargados del Data\ sin sus texturas.
class TracedModels final : public IModelLibrary
{
public:
    [[nodiscard]] bool Load(const std::filesystem::path& dataRoot)
    {
        for (const std::uint16_t model : {std::uint16_t{0x20a}, std::uint16_t{0x20b}, HelmMale17, ArmorMale17,
                                          PantMale02, GloveMale17, BootMale17, Sword17, Helper02})
        {
            const std::optional<ModelFile> file = FileOf(model);
            if (!file)
            {
                ADD_FAILURE() << "ninguna tabla tiene el modelo " << model;
                return false;
            }
            auto loaded = LoadModelFile(dataRoot, *file);
            if (!loaded.HasValue())
            {
                ADD_FAILURE() << "no cargó el modelo " << model;
                return false;
            }
            Model read = std::move(loaded).Value();
            auto skeleton = model == 0x20a ? MakePlayerSkeleton(read) : MakeSkeleton(read);
            if (!skeleton.HasValue())
            {
                ADD_FAILURE() << "el esqueleto del modelo " << model;
                return false;
            }
            m_skeletons.emplace(model, std::move(skeleton).Value());
            m_models.emplace(model, std::move(read));
        }
        return true;
    }

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        const auto found = m_models.find(model);
        return found == m_models.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        const auto found = m_skeletons.find(model);
        return found == m_skeletons.end() ? nullptr : &found->second;
    }

    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    static std::optional<ModelFile> FileOf(std::uint16_t model)
    {
        for (const std::span<const ModelFile> table : {PlayerModelFiles(), ItemModelFiles()})
        {
            for (const ModelFile& file : table)
            {
                if (file.model == model)
                {
                    return file;
                }
            }
        }
        return std::nullopt;
    }

    std::map<std::uint16_t, Model> m_models;
    std::map<std::uint16_t, Skeleton> m_skeletons;
};

CharacterAppearance TracedAppearance()
{
    CharacterAppearance appearance;
    appearance.bodyParts = {WornItem{HelmMale17, TracedLevel}, WornItem{ArmorMale17, TracedLevel},
                            WornItem{PantMale02, 0}, WornItem{GloveMale17, TracedLevel},
                            WornItem{BootMale17, TracedLevel}};
    appearance.rightHand = WornItem{Sword17, TracedLevel};
    appearance.pet = Helper02;
    appearance.scale = 0.9f;
    return appearance;
}

// Como la guarda el original: el frame de la tabla más 1, módulo las keys, y su parte entera como la
// anterior.
AnimationState StateAt(std::uint8_t action, float frame, int keyCount)
{
    return {action, std::fmod(frame + 1.0f, static_cast<float>(keyCount)), action,
            static_cast<std::int16_t>(std::floor(frame))};
}

// Si los vértices del lote desde `start` son los del bloque, en orden.
// Las UV se comparan solo en lo que va con textura: un bloque sin glTexCoord (la sombra, 0x4eff40)
// trae en la traza la última que quedó.
bool SameRun(std::span<const ModelVertex> vertices, const std::vector<bool>& textured, std::size_t start,
             const GlTraceBlock& block)
{
    if (start + block.vertices.size() > vertices.size())
    {
        return false;
    }
    for (std::size_t k = 0; k < block.vertices.size(); ++k)
    {
        const ModelVertex& mine = vertices[start + k];
        if (glm::distance(mine.position, block.vertices[k].position) > MaxError)
        {
            return false;
        }
        if (textured[start + k] && (std::fabs(mine.uv.x - block.vertices[k].texCoord.x) > MaxUvError ||
                                    std::fabs(mine.uv.y - block.vertices[k].texCoord.y) > MaxUvError))
        {
            return false;
        }
    }
    return true;
}

// Si cada vértice del lote va en un tramo con textura.
std::vector<bool> TexturedVertices(const ModelBatch& batch)
{
    std::vector<bool> textured(batch.Vertices().size(), false);
    for (const Client::Render::ModelDrawCommand& command : batch.Commands())
    {
        std::fill_n(textured.begin() + static_cast<std::ptrdiff_t>(command.firstVertex),
                    static_cast<std::ptrdiff_t>(command.vertexCount), command.state.texture.has_value());
    }
    return textured;
}

class GoldenCharacterDrawing : public Client::Tests::GoldenAndGlTraceTest
{
};

TEST_F(GoldenCharacterDrawing, MatchesTheCharacterOfTheSelection)
{
    const auto path = TraceFile(SelectionTrace);
    ASSERT_TRUE(path.has_value());
    auto read = ReadGlTrace(*path);
    ASSERT_TRUE(read.HasValue()) << read.Error();
    const GlTrace trace = std::move(read).Value();
    TracedModels models;
    ASSERT_TRUE(models.Load(m_dataRoot));
    const int bodyKeys = models.SkeletonOf(0x20a)->actions[3].keyCount;
    const int petKeys = models.SkeletonOf(Helper02)->actions[0].keyCount;
    ASSERT_EQ(trace.frames.size(), BodyFrames.size());
    // Lo que la traza manda por frame: cuatro bloques por malla visible de las Male17 y de la espada
    // (la base y los tres pases del nivel 13) y uno por malla del pantalón y de la mascota.
    // Y la sombra (0x20b), un bloque, antes de las partes.
    std::size_t expectedBlocks = 1;
    for (const auto& [model, perMesh] :
         {std::pair{HelmMale17, 4}, std::pair{ArmorMale17, 4}, std::pair{PantMale02, 1}, std::pair{GloveMale17, 4},
          std::pair{BootMale17, 4}, std::pair{Sword17, 4}, std::pair{Helper02, 1}})
    {
        for (const auto& mesh : models.Find(model)->meshes)
        {
            const bool hidden = Client::Data::Models::SkinSlotOf(mesh.textureName) ==
                                std::optional<std::uint16_t>{Client::Data::Models::HiddenTextureSlot};
            expectedBlocks += hidden ? 0u : static_cast<std::size_t>(perMesh);
        }
    }

    for (std::size_t f = 0; f < BodyFrames.size(); ++f)
    {
        DrawnCharacter character;
        character.appearance = TracedAppearance();
        character.body = StateAt(3, BodyFrames[f], bodyKeys);
        character.pet = StateAt(0, PetFrames[f], petKeys);
        character.object = ObjectTransform{{12880.0f, 15046.0f, 163.0f}, {0.0f, 0.0f, 100.0f}, 0.9f, 0.0f};
        ModelBatch batch;
        batch.Begin(MakePass3D(PixelSize{1024, 768}, WorldViewport, SelectionCamera()));

        DrawCharacter(batch, models, character, std::chrono::milliseconds{Clocks[f]});

        const std::span<const ModelVertex> vertices = batch.Vertices();
        ASSERT_FALSE(vertices.empty());
        const std::vector<bool> textured = TexturedVertices(batch);
        std::vector<bool> covered(vertices.size(), false);
        std::size_t blocks = 0;
        // Dónde empieza en el lote cada bloque de la traza, en el orden de la traza.
        std::vector<std::size_t> starts;
        for (const GlTraceBlock& block : trace.frames[f].blocks)
        {
            if (block.mode != "GL_TRIANGLES" || block.vertices.empty())
            {
                continue;
            }
            for (std::size_t start = 0; start + block.vertices.size() <= vertices.size(); start += 3)
            {
                if (!covered[start] && SameRun(vertices, textured, start, block))
                {
                    std::fill(covered.begin() + static_cast<std::ptrdiff_t>(start),
                              covered.begin() + static_cast<std::ptrdiff_t>(start + block.vertices.size()), true);
                    ++blocks;
                    starts.push_back(start);
                    break;
                }
            }
        }
        const auto uncovered = static_cast<std::size_t>(std::count(covered.begin(), covered.end(), false));
        EXPECT_EQ(uncovered, 0u) << "frame " << f << ": vértices del lote que no son de ningún bloque de la traza";
        EXPECT_EQ(blocks, expectedBlocks) << "frame " << f;
        // Y en el orden del original: lo aditivo no escribe la profundidad, así que el orden se ve.
        const auto unordered = std::ranges::is_sorted_until(starts);
        EXPECT_EQ(unordered, starts.end())
            << "frame " << f << ": el bloque " << (unordered - starts.begin()) << " de la traza va antes en el lote";
        RecordProperty("blocks" + std::to_string(f), static_cast<int>(blocks));
    }
}
} // namespace
