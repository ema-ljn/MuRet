#include "Data/Models/ModelFiles.h"

#include "Data/DataPath.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerActions.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// Los modelos que piden los cargadores del original, contra la carpeta golden
// (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Data::ResolveDataPath;
using Client::Data::Models::Bone;
using Client::Data::Models::ItemModelFiles;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Mesh;
using Client::Data::Models::MeshesWithoutShine;
using Client::Data::Models::ModelFile;
using Client::Data::Models::ModelFileName;
using Client::Data::Models::ModelLoadErrorKind;
using Client::Data::Models::ModelMesh;
using Client::Data::Models::PlayerActionSpeeds;
using Client::Data::Models::PlayerActionsWithoutRepeat;
using Client::Data::Models::PlayerModelFiles;
using Client::Data::Models::Vertex;
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureFileName;
using Client::Data::Textures::TextureLoaderOf;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Render::TextureSlot;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::MakePlayerSkeleton;
using Client::Render::Models::ModelTextureSlots;

// Los modelos de la selección que carga 0x61ca30: Logo05 en el 0xa4 y los NewFace en el 0xa8 a 0xac
// (0x61cb92 y el bucle de 0x61cbb1).
constexpr std::array<ModelFile, 6> SelectionModels = {{
    {0xa4, "Logo\\", "Logo", 5},
    {0xa8, "Logo\\", "NewFace", 1},
    {0xa9, "Logo\\", "NewFace", 2},
    {0xaa, "Logo\\", "NewFace", 3},
    {0xab, "Logo\\", "NewFace", 4},
    {0xac, "Logo\\", "NewFace", 5},
}};
// El primer slot de las partes del personaje y el muestreo con que las pide 0x60f7d0 (06).
constexpr TextureSlot PlayerPartsFirstSlot{0x138};
constexpr Sampling PlayerPartsSampling{Filter::Linear, Wrap::Repeat};

std::string Named(const ModelFile& file)
{
    return std::string(file.folder) + ModelFileName(file.name, file.number);
}

using GoldenModelFiles = Client::Tests::GoldenDataTest;

// Los 213 que pide 0x60dbe0 están en el laboratorio y se leen.
TEST_F(GoldenModelFiles, EveryModelOfThePlayerLoaderExists)
{
    for (const ModelFile& file : PlayerModelFiles())
    {
        EXPECT_TRUE(LoadModelFile(m_dataRoot, file).HasValue()) << Named(file);
    }
}

// De los 243 de 0x60ff50, faltan los tres giftbox (09): el original los saltea sin avisar.
TEST_F(GoldenModelFiles, TheItemLoaderMissesOnlyTheGiftBoxes)
{
    std::vector<std::string> missing;
    for (const ModelFile& file : ItemModelFiles())
    {
        const auto loaded = LoadModelFile(m_dataRoot, file);
        if (!loaded.HasValue())
        {
            EXPECT_EQ(loaded.Error().kind, ModelLoadErrorKind::Missing) << Named(file);
            missing.push_back(Named(file));
        }
    }
    EXPECT_EQ(missing,
              (std::vector<std::string>{"Item\\giftbox_bp.bmd", "Item\\giftbox_br.bmd", "Item\\giftbox_bb.bmd"}));
}

// Cada malla que marca 0x60ff50 existe en el modelo del laboratorio.
TEST_F(GoldenModelFiles, TheMeshesWithoutShineExist)
{
    for (const ModelMesh& marked : MeshesWithoutShine)
    {
        const auto file = std::ranges::find(ItemModelFiles(), marked.model, &ModelFile::model);
        ASSERT_NE(file, ItemModelFiles().end()) << marked.model;
        const auto model = LoadModelFile(m_dataRoot, *file);
        ASSERT_TRUE(model.HasValue()) << Named(*file);
        EXPECT_LT(marked.mesh, model.Value().meshes.size()) << Named(*file);
    }
}

// El hueso de cada vértice de cada parte está en los 60 de Player.bmd y no es dummy: las partes se
// dibujan con la pose del cuerpo, y un dummy no se posa (CL6.2).
TEST_F(GoldenModelFiles, EveryPartVertexHangsFromAPosedBone)
{
    const auto skeleton = LoadModelFile(m_dataRoot, PlayerModelFiles().front());
    ASSERT_TRUE(skeleton.HasValue());
    const std::vector<Bone>& bones = skeleton.Value().bones;
    for (const ModelFile& part : PlayerModelFiles().subspan(1))
    {
        const auto model = LoadModelFile(m_dataRoot, part);
        ASSERT_TRUE(model.HasValue()) << Named(part);
        for (const Mesh& mesh : model.Value().meshes)
        {
            for (const Vertex& vertex : mesh.vertices)
            {
                const bool posed = vertex.bone >= 0 && static_cast<std::size_t>(vertex.bone) < bones.size() &&
                                   !bones[static_cast<std::size_t>(vertex.bone)].dummy;
                EXPECT_TRUE(posed) << Named(part) << ": el hueso " << vertex.bone;
            }
        }
    }
}

// La textura de cada malla de cada parte está en Player\, la carpeta de 0x60f7d0, con el cargador que
// elige la letra después del primer punto: lo que ModelTextureSlots manda a subir se encuentra.
TEST_F(GoldenModelFiles, EveryMeshTextureOfThePlayerResolves)
{
    ModelTextureSlots slots(PlayerPartsFirstSlot);
    for (const ModelFile& part : PlayerModelFiles().subspan(1))
    {
        const auto model = LoadModelFile(m_dataRoot, part);
        ASSERT_TRUE(model.HasValue()) << Named(part);
        (void)slots.Assign(model.Value(), "Player\\", PlayerPartsSampling);
    }
    ASSERT_FALSE(slots.ToLoad().empty());
    for (const TextureSetEntry& entry : slots.ToLoad())
    {
        const auto loader = TextureLoaderOf(entry.path);
        ASSERT_TRUE(loader.has_value()) << entry.path;
        std::error_code error;
        EXPECT_TRUE(ResolveDataPath(m_dataRoot, TextureFileName(entry.path, *loader), error).has_value()) << entry.path;
        EXPECT_FALSE(error) << entry.path;
    }
}

// Logo05 y los cinco NewFace de la selección están en Logo\.
TEST_F(GoldenModelFiles, TheSelectionModelsExist)
{
    for (const ModelFile& file : SelectionModels)
    {
        const auto loaded = LoadModelFile(m_dataRoot, file);
        ASSERT_TRUE(loaded.HasValue()) << Named(file);
        EXPECT_FALSE(loaded.Value().meshes.empty()) << Named(file);
    }
}

// Cada velocidad que escribe 0x60dbe0, y las cuatro que no repiten, son acciones de Player.bmd.
TEST_F(GoldenModelFiles, ThePlayerActionsFitTheSkeleton)
{
    const auto skeleton = LoadModelFile(m_dataRoot, PlayerModelFiles().front());
    ASSERT_TRUE(skeleton.HasValue());
    const std::size_t actions = skeleton.Value().actions.size();
    for (const auto& write : PlayerActionSpeeds())
    {
        EXPECT_LT(write.last, actions);
    }
    for (const std::uint8_t action : PlayerActionsWithoutRepeat)
    {
        EXPECT_LT(action, actions);
    }
}

// Las dos acciones de las trazas de GL, como las deja 0x60dbe0 en Player.bmd: la 3 de la selección
// (siete keys a 0,26) y la 1 de Lorencia (seis a 0,28), las dos repiten y sin lock_positions. Son las
// de AnimationTest.TheSelectionJumpsAreThoseOfTheTrace y TheLorenciaJumpsAreThoseOfTheTrace.
TEST_F(GoldenModelFiles, TheActionsOfTheTracesAreThoseOfTheJumpTests)
{
    const auto model = LoadModelFile(m_dataRoot, PlayerModelFiles().front());
    ASSERT_TRUE(model.HasValue());
    const auto skeleton = MakePlayerSkeleton(model.Value());
    ASSERT_TRUE(skeleton.HasValue());

    const ActionInfo& selection = skeleton.Value().actions.at(3);
    EXPECT_EQ(selection.keyCount, 7);
    EXPECT_EQ(selection.speed, 0.26f);
    EXPECT_TRUE(selection.repeats);
    EXPECT_FALSE(selection.lockPositions);
    const ActionInfo& lorencia = skeleton.Value().actions.at(1);
    EXPECT_EQ(lorencia.keyCount, 6);
    EXPECT_EQ(lorencia.speed, 0.28f);
    EXPECT_TRUE(lorencia.repeats);
    EXPECT_FALSE(lorencia.lockPositions);
}
} // namespace
