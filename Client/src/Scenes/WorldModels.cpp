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
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldSpecialModels.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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

constexpr std::uint8_t LorenciaMap = 0;
constexpr Render::TextureSlot SpecialModelsFirstSlot{0x69};

// "Object56" para el mapa 55: la carpeta Object<N> de su número (Data::WorldFolderNumber).
std::string ObjectFolder(std::uint8_t map)
{
    return "Object" + std::to_string(Data::WorldFolderNumber(map)) + "\\";
}
} // namespace

Core::Expected<void, std::string> WorldModels::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                    Render::ITextureStore& textures, Core::Log& log,
                                                    const Render::Models::ModelTextureSlots* gameSlots,
                                                    const IModelLibrary* globalModels)
{
    Release();
    const WorldSpecialModelSet specials = SpecialWorldModelsOf(map);
    if (specials.kind == WorldSpecialKind::BloodCastle && globalModels == nullptr)
    {
        // Dependencia estructural, antes de cargar o pedir una textura: estos modelos son globales,
        // cargados por 0x615700. Un archivo Gate opcional ausente no implica servicio ausente.
        return Core::Unexpected(std::string("0x6179f0: falta la biblioteca global de Gate01/02 (0x616304/0x61631a)"));
    }
    m_globalModels = globalModels;
    m_folder = ObjectFolder(map);
    m_slots.emplace(gameSlots != nullptr ? gameSlots->ContinuedAt(SpecialModelsFirstSlot)
                                         : Render::Models::ModelTextureSlots(SpecialModelsFirstSlot));
    std::size_t missing = 0;
    if (auto loaded = LoadSpecials(dataRoot, specials, log, missing); !loaded.HasValue())
        return loaded;
    LoadObjects(dataRoot, map, log, missing);
    if (auto configured = ConfigureActions(map, specials); !configured.HasValue())
        return configured;
    log.Debug("los objetos de {}: {} modelos y {} que no están", m_folder, m_models.size(), missing);
    if (m_models.empty())
        log.Warning("no hay ningún modelo de los objetos del mundo en Data\\{}: el mundo va sin ellos", m_folder);
    UploadTextures(dataRoot, textures, log);
    return {};
}

void WorldModels::LoadFile(const std::filesystem::path& dataRoot, const Data::Models::ModelFile& file, Core::Log& log,
                           std::size_t& missing)
{
    auto loaded = Data::Models::LoadModelFile(dataRoot, file);
    if (!loaded.HasValue())
    {
        if (loaded.Error().kind == Data::Models::ModelLoadErrorKind::Missing)
            ++missing;
        else
            log.Warning("un objeto del mundo no se puede leer y no se dibuja: {}", loaded.Error().path);
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
}

void WorldModels::AssignSpecialTexture(const Data::Models::ModelFile& file, const WorldSpecialModelSet& specials)
{
    const auto model = m_models.find(file.model);
    if (model == m_models.end())
        return;
    const auto folder = specials.textureFolder.empty() ? file.folder : specials.textureFolder;
    m_meshTextures[file.model] = m_slots->Assign(model->second, folder, WorldModelSampling);
}

Core::Expected<void, std::string> WorldModels::LoadSpecials(const std::filesystem::path& dataRoot,
                                                            const WorldSpecialModelSet& specials, Core::Log& log,
                                                            std::size_t& missing)
{
    if (specials.kind == WorldSpecialKind::Exile)
    {
        // 0x617633..0x6176a4: primero carga los siete; con todos ausentes, cada asignación lee cero
        // mallas y no llega a GL. Una malla real alcanza el wrap inválido 0x2600 de 0x617699.
        for (const Data::Models::ModelFile& file : specials.files)
            LoadFile(dataRoot, file, log, missing);
        for (const Data::Models::ModelFile& file : specials.files)
        {
            const auto found = m_models.find(file.model);
            if (found != m_models.end() && !found->second.meshes.empty())
                return Core::Unexpected(
                    std::string("0x617699: el wrap 0x2600 de los especiales de Object6 no está portado"));
        }
        return {};
    }
    if (specials.kind == WorldSpecialKind::Noria)
    {
        LoadFile(dataRoot, specials.files.front(), log, missing);
        AssignSpecialTexture(specials.files.front(), specials);
        const auto warps = specials.files.subspan(1);
        for (const Data::Models::ModelFile& file : warps)
            LoadFile(dataRoot, file, log, missing);
        for (const Data::Models::ModelFile& file : warps)
            AssignSpecialTexture(file, specials);
        return {};
    }
    for (const Data::Models::ModelFile& file : specials.files)
    {
        LoadFile(dataRoot, file, log, missing);
        AssignSpecialTexture(file, specials);
    }
    if (specials.kind == WorldSpecialKind::BloodCastle)
    {
        for (std::uint16_t gate = 0x10c; gate <= 0x10d; ++gate)
        {
            // 0x60d617..0x60d61b: un Gate sin mallas no pide textura ni adelanta el cursor.
            if (const Data::Models::Model* model = m_globalModels->Find(gate); model != nullptr)
                m_meshTextures[gate] = m_slots->Assign(*model, "Monster\\", WorldModelSampling);
        }
    }
    return {};
}

void WorldModels::LoadObjects(const std::filesystem::path& dataRoot, std::uint8_t map, Core::Log& log,
                              std::size_t& missing)
{
    m_slots->SetNext(WorldModelsFirstSlot);
    if (map == LorenciaMap)
    {
        for (const Data::Models::ModelFile& file : LorenciaModelFiles())
            LoadFile(dataRoot, file, log, missing);
    }
    else
    {
        for (std::size_t model = 0; model < WorldModelCount; ++model)
            LoadFile(dataRoot, {static_cast<std::uint16_t>(model), m_folder, "Object", static_cast<int>(model + 1)},
                     log, missing);
    }
    m_slots->SetNext(WorldModelsFirstSlot);
    for (const auto& [model, data] : m_models)
    {
        if (model < WorldModelCount)
            m_meshTextures[model] = m_slots->Assign(data, m_folder, WorldModelSampling);
    }
}

Core::Expected<void, std::string> WorldModels::ConfigureActions(std::uint8_t map, const WorldSpecialModelSet& specials)
{
    constexpr std::uint8_t DungeonMap = 1;
    constexpr std::uint16_t DungeonModel = 40;
    constexpr std::size_t DungeonAction = 1;
    if (map == DungeonMap)
    {
        const auto model = m_skeletons.find(DungeonModel);
        if (model == m_skeletons.end() || model->second.actions.size() <= DungeonAction)
            return Core::Unexpected(std::string("0x61a13c: falta la segunda acción del modelo 40 de Dungeon"));
        // 0x61a136: 40*0xe0+0x30 es el arreglo de acciones; +0x14 es velocidad de la acción 1.
        model->second.actions[DungeonAction].speed = 0.4f;
    }
    if (specials.kind != WorldSpecialKind::Kalima)
        return {};
    struct Setting
    {
        std::uint16_t model;
        bool repeats;
        std::optional<float> speed;
        const char* address;
    };
    constexpr std::array<Setting, 4> Settings{{{0x122, true, 0.13f, "0x618c54"},
                                               {0x123, true, 0.13f, "0x618c76"},
                                               {0x126, false, std::nullopt, "0x618c94"},
                                               {0x127, true, std::nullopt, "0x618ca3"}}};
    for (const Setting& setting : Settings)
    {
        const auto model = m_skeletons.find(setting.model);
        if (model == m_skeletons.end() || model->second.actions.empty())
            return Core::Unexpected(std::string(setting.address) + ": falta la primera acción del especial de Kalima " +
                                    std::to_string(setting.model));
        Render::Models::ActionInfo& action = model->second.actions.front();
        action.repeats = setting.repeats;
        if (setting.speed)
            action.speed = *setting.speed;
    }
    return {};
}

void WorldModels::UploadTextures(const std::filesystem::path& dataRoot, Render::ITextureStore& textures, Core::Log& log)
{
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
        if (const auto uploaded = textures.Load(loaded.Value()); !uploaded.HasValue())
        {
            ++skipped;
            log.Warning("una textura de un objeto del mundo no sube y se saltea: {}", uploaded.Error());
            continue;
        }
        m_uploaded.push_back(Render::TextureSlot{entry.slot});
        m_textureMetadata.insert_or_assign(entry.slot, TextureMetadata(loaded.Value().front().texture));
    }
    log.Debug("las texturas de {}: {} subidas y {} salteadas", m_folder, m_uploaded.size(), skipped);
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
    m_textureMetadata.clear();
    m_globalModels = nullptr;
}

const Data::Models::Model* WorldModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? &found->second
                                   : (m_globalModels != nullptr ? m_globalModels->Find(model) : nullptr);
}

const Render::Models::Skeleton* WorldModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_skeletons.find(model);
    return found != m_skeletons.end() ? &found->second
                                      : (m_globalModels != nullptr ? m_globalModels->SkeletonOf(model) : nullptr);
}

std::span<const Render::TextureSlot> WorldModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_meshTextures.find(model);
    if (found != m_meshTextures.end())
        return found->second;
    return m_globalModels != nullptr ? m_globalModels->MeshTextures(model) : std::span<const Render::TextureSlot>{};
}

std::optional<TextureMetadata> WorldModels::TextureMetadataOf(std::uint32_t selector) const
{
    const auto found = m_textureMetadata.find(selector);
    if (found != m_textureMetadata.end())
        return found->second;
    return m_globalModels != nullptr ? m_globalModels->TextureMetadataOf(selector) : std::nullopt;
}

std::optional<Render::TextureSlot> WorldModels::TextureSlotOf(std::uint32_t selector) const
{
    for (const Render::TextureSlot slot : m_uploaded)
    {
        if (slot.value == selector)
            return slot;
    }
    return m_globalModels != nullptr ? m_globalModels->TextureSlotOf(selector) : std::nullopt;
}

std::optional<glm::vec2> WorldModels::TextureSize(Render::TextureSlot slot) const
{
    const auto metadata = TextureMetadataOf(slot.value);
    return metadata ? std::optional(metadata->size) : std::nullopt;
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
