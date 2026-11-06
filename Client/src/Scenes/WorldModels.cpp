#include "Scenes/WorldModels.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Data/WorldFolder.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/LorenciaModelFiles.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x60d5f0(k, "Object<N>\\", 0x2901, 0x2600, 1): repitiendo y con GL_NEAREST.
constexpr Data::Textures::Sampling WorldModelSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};

// 0x616ec0, el caso del mapa 0x37 (0x6188ae): el dragón que vuela, 0x60da30(0xb4, "Data\\Object56\\",
// "Dragon", -1), con sus texturas desde 0x60d530(0x69) (Q-C127: en el Data\ es dragon.bmd).
constexpr std::uint8_t LoginMap = 0x37;
constexpr std::uint8_t LorenciaMap = 0;
// 0x617356/0x617387: Bird01 y Fish01 antes de los nombres de los objetos de Lorencia.
constexpr std::uint16_t LorenciaBirdModel = 0xad;
constexpr std::uint16_t LorenciaFishModel = 0xb2;
constexpr Render::TextureSlot SpecialModelsFirstSlot{0x69};

// "Object56" para el mapa 55: la carpeta Object<N> de su número (Data::WorldFolderNumber).
std::string ObjectFolder(std::uint8_t map)
{
    return "Object" + std::to_string(Data::WorldFolderNumber(map)) + "\\";
}
} // namespace

Core::Expected<void, std::string> WorldModels::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                    Render::ITextureStore& textures, Core::Log& log,
                                                    const Render::Models::ModelTextureSlots* gameSlots)
{
    Release();
    m_folder = ObjectFolder(map);
    std::size_t missing = 0;
    const auto load = [&](const Data::Models::ModelFile& file)
    {
        auto loaded = Data::Models::LoadModelFile(dataRoot, file);
        if (!loaded.HasValue())
        {
            if (loaded.Error().kind == Data::Models::ModelLoadErrorKind::Missing)
            {
                ++missing;
            }
            else
            {
                log.Warning("un objeto del mundo no se puede leer y no se dibuja: {}", loaded.Error().path);
            }
            return;
        }
        auto skeleton = Render::Models::MakeSkeleton(loaded.Value());
        if (!skeleton.HasValue())
        {
            log.Warning("el objeto {} del mundo no tiene un esqueleto que se pueda posar", file.model);
            return;
        }
        m_models.emplace(file.model, std::move(loaded).Value());
        m_skeletons.emplace(file.model, std::move(skeleton).Value());
    };
    // Los modelos especiales del mapa, con sus texturas desde 0x69.
    m_slots.emplace(gameSlots != nullptr ? gameSlots->ContinuedAt(SpecialModelsFirstSlot)
                                         : Render::Models::ModelTextureSlots(SpecialModelsFirstSlot));
    const auto loadSpecial = [&](const Data::Models::ModelFile& file)
    {
        load(file);
        if (const auto model = m_models.find(file.model); model != m_models.end())
            m_meshTextures[file.model] = m_slots->Assign(model->second, file.folder, WorldModelSampling);
    };
    if (map == LorenciaMap)
    {
        loadSpecial({LorenciaBirdModel, m_folder, "Bird", 1});
        loadSpecial({LorenciaFishModel, m_folder, "Fish", 1});
    }
    else if (map == LoginMap)
        loadSpecial({DragonModel, m_folder, "Dragon", -1});

    // 0x618e4a: el cursor de los objetos se reinicia antes de cargar sus modelos.
    m_slots->SetNext(WorldModelsFirstSlot);
    if (map == LorenciaMap)
    {
        for (const Data::Models::ModelFile& file : LorenciaModelFiles())
            load(file);
    }
    else
    {
        for (std::size_t k = 0; k < WorldModelCount; ++k)
            load(Data::Models::ModelFile{static_cast<std::uint16_t>(k), m_folder, "Object", static_cast<int>(k + 1)});
    }
    log.Debug("los objetos de {}: {} modelos y {} que no están", m_folder, m_models.size(), missing);
    if (m_models.empty())
    {
        log.Warning("no hay ningún modelo de los objetos del mundo en Data\\{}: el mundo va sin ellos", m_folder);
    }
    // 0x619eb7..0x619ed8 (Lorencia), 0x61a0e8 en los demás: texturas en el orden de k.
    if (map != LorenciaMap)
        m_slots->SetNext(WorldModelsFirstSlot);
    for (const auto& [model, data] : m_models)
    {
        if (model < WorldModelCount)
        {
            m_meshTextures[model] = m_slots->Assign(data, m_folder, WorldModelSampling);
        }
    }
    m_store = &textures;
    std::size_t skipped = 0;
    for (const Data::Textures::TextureSetEntry& entry : m_slots->ToLoad())
    {
        auto loaded = Data::Textures::LoadTextureSet(dataRoot, std::span(&entry, 1));
        if (!loaded.HasValue())
        {
            ++skipped;
            log.Warning("una textura de un objeto del mundo no carga y se saltea: {}",
                        Data::Textures::Describe(loaded.Error()));
            continue;
        }
        const auto uploaded = textures.Load(loaded.Value());
        if (!uploaded.HasValue())
        {
            ++skipped;
            log.Warning("una textura de un objeto del mundo no sube y se saltea: {}", uploaded.Error());
            continue;
        }
        m_uploaded.push_back(Render::TextureSlot{entry.slot});
    }
    log.Debug("las texturas de {}: {} subidas y {} salteadas", m_folder, m_uploaded.size(), skipped);
    return {};
}

void WorldModels::Release()
{
    if (m_store != nullptr)
    {
        for (const Render::TextureSlot slot : m_uploaded)
        {
            m_store->Release(slot);
        }
    }
    m_uploaded.clear();
    m_store = nullptr;
    m_slots.reset();
    m_models.clear();
    m_skeletons.clear();
    m_meshTextures.clear();
}

const Data::Models::Model* WorldModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found == m_models.end() ? nullptr : &found->second;
}

const Render::Models::Skeleton* WorldModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_skeletons.find(model);
    return found == m_skeletons.end() ? nullptr : &found->second;
}

std::span<const Render::TextureSlot> WorldModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_meshTextures.find(model);
    return found == m_meshTextures.end() ? std::span<const Render::TextureSlot>{} : found->second;
}

std::size_t WorldModels::Count() const
{
    return m_models.size();
}

const Render::Models::ModelTextureSlots* WorldModels::TextureSlots() const
{
    return m_slots ? &*m_slots : nullptr;
}
} // namespace Client::Scenes
