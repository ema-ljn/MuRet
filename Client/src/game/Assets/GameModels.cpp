#include "Assets/GameModels.h"

#include "Assets/PlayerSkeleton.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Tables/ModelLoadCalls.h"
#include "Tables/ModelTextureCalls.h"
#include "Tables/PlayerModels.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Assets/TextureSets.h"
#include "Models/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Assets
{
namespace
{
// Los slots donde empiezan las texturas de cada cargador (06 § El modelo en el binario).
constexpr Render::TextureSlot PlayerTexturesStart{0x138};
constexpr Render::TextureSlot ItemTexturesStart{0x25a};
// 0x615700 (0x61692a), antes de las del caballo.
constexpr Render::TextureSlot SkillTexturesStart{0x3b8};
// 0x615f77: los Fenrir, donde terminan las de 0x611850.
constexpr Render::TextureSlot FenrirTexturesStart{0x371};
// 0x61634e..0x616370: Gate02 antes del bloque 0x3b8; Blood Castle reasigna ambos Gate en el mapa.
constexpr Render::TextureSlot GateTexturesStart{0x69};
// Sube las de `entries` de a una: la que no carga deja su línea en el log, y las demás siguen.
std::size_t UploadEach(const std::filesystem::path& dataRoot, std::span<const Data::Textures::TextureSetEntry> entries,
                       Render::ITextureStore& textures, Core::Log& log,
                       std::map<std::uint32_t, Models::TextureMetadata>* metadata = nullptr)
{
    std::size_t skipped = 0;
    for (const Data::Textures::TextureSetEntry& entry : entries)
    {
        auto loaded = Data::Textures::LoadTextureSet(dataRoot, std::span(&entry, 1));
        if (!loaded.HasValue())
        {
            ++skipped;
            log.Warning("una textura de un modelo no carga y se saltea: {}", Data::Textures::Describe(loaded.Error()));
            continue;
        }
        const auto uploaded = textures.Load(loaded.Value());
        if (!uploaded.HasValue())
        {
            ++skipped;
            log.Warning("una textura de un modelo no sube y se saltea: {}", uploaded.Error());
            continue;
        }
        if (metadata != nullptr)
        {
            const Data::Textures::Texture& texture = loaded.Value().front().texture;
            metadata->insert_or_assign(static_cast<std::uint32_t>(entry.slot), Models::TextureMetadata(texture));
        }
    }
    const auto uploaded = entries.size() - skipped;
    log.Info("las texturas de los modelos: {} subidas y {} salteadas", uploaded, skipped);
    return uploaded;
}
} // namespace

Core::Expected<void, std::string> GameModels::Load(const std::filesystem::path& dataRoot,
                                                   Render::ITextureStore& textures, Core::Log& log)
{
    // En una recarga, los modelos de la fase de skills aún deben estar ausentes durante 0x611850.
    GameModels staged;
    staged.m_textureMetadata = m_textureMetadata;
    if (m_startupTextureStore == &textures)
        staged.m_startupTextureStore = m_startupTextureStore;
    else
        staged.m_textureMetadata.erase(StartupSelectionTextureSlot);
    auto loaded = staged.LoadResources(dataRoot, textures, log);
    if (loaded.HasValue())
        *this = std::move(staged);
    return loaded;
}

Core::Expected<void, std::string> GameModels::LoadResources(const std::filesystem::path& dataRoot,
                                                            Render::ITextureStore& textures, Core::Log& log)
{
    const auto loadTables =
        [&](std::initializer_list<std::span<const Data::Models::ModelFile>> tables) -> Core::Expected<void, std::string>
    {
        for (const auto table : tables)
        {
            for (const Data::Models::ModelFile& file : table)
            {
                auto loaded = Data::Models::LoadModelFile(dataRoot, file);
                if (!loaded.HasValue())
                {
                    if (Data::Models::IsRequiredModel(file.name))
                    {
                        return Core::Unexpected("no se pudo leer " + loaded.Error().path);
                    }
                    log.Warning("un modelo no se puede leer y no se dibuja: {}", loaded.Error().path);
                    continue;
                }
                Data::Models::Model model = std::move(loaded).Value();
                auto skeleton = file.model == Tables::PlayerSkeletonModel ? Assets::MakePlayerSkeleton(model)
                                                                          : Models::MakeSkeleton(model);
                if (skeleton.HasValue())
                {
                    m_skeletons.insert_or_assign(file.model, std::move(skeleton).Value());
                }
                m_models.insert_or_assign(file.model, std::move(model));
            }
        }
        return {};
    };
    const auto playerAndItems = loadTables({Tables::PlayerModelFiles(), Tables::ItemModelFiles()});
    if (!playerAndItems.HasValue())
        return playerAndItems;
    m_slots.SetNext(PlayerTexturesStart);
    AssignTextures(Tables::PlayerTextureCalls());
    m_slots.SetNext(ItemTexturesStart);
    AssignTextures(Tables::ItemTextureCalls());
    // 0x611850 consulta 0xdb con Item\Bow\ antes de que 0x616095 cargue su BMD.
    const auto skills = loadTables({Tables::SkillModelFiles()});
    if (!skills.HasValue())
        return skills;
    // 0x61606f/0x61607f: el cargador cambia la acción 0 de HellGate después de leer su BMD.
    if (const auto gate = m_skeletons.find(0xc5); gate != m_skeletons.end() && !gate->second.actions.empty())
    {
        auto& action = gate->second.actions.front();
        action.lockPositions = false;
        action.speed = 0.15f;
    }
    // 0x616276: la acción 0 de Ball01 conserva 0,5 keys por paso.
    if (const auto ball = m_skeletons.find(0xf1); ball != m_skeletons.end() && !ball->second.actions.empty())
        ball->second.actions.front().speed = 0.5f;
    m_slots.SetNext(FenrirTexturesStart);
    AssignTextures(Tables::FenrirTextureCalls());
    // 61613a consulta 109 y 61619f consulta 10f antes de cargar sus BMD. En el perfil sin Data2/SMD no hay mallas
    // en ese punto: no asignar ahora el BMD futuro. Su consulta efectiva llega con el barrido de 616e46.
    m_slots.SetNext(GateTexturesStart);
    AssignTextures(Tables::SiegeBeforeSkillTextureCalls());
    // 0x6164a0..0x6164bb: LaceArrow pisa los slots 0x138 y 0x139, con Item\ y GL_CLAMP (Q-C89).
    if (const auto* laceArrow = Find(0x11c); laceArrow != nullptr)
    {
        m_slots.SetNext(PlayerTexturesStart);
        constexpr Data::Textures::Sampling sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp};
        m_meshTextures.insert_or_assign(0x11c, m_slots.Assign(*laceArrow, "Item\\", sampling));
    }
    m_slots.SetNext(SkillTexturesStart);
    AssignTextures(Tables::SkillTextureCalls());
    for (std::uint16_t model = 0xbe; model < 0x151; ++model)
    {
        const std::array<Tables::ModelTextureCall, 1> call{{{model, "Skill\\"}}};
        AssignTextures(call);
    }
    UploadEach(dataRoot, DamageTextureSet, textures, log, &m_textureMetadata);
    // 0x61ccb0: las del HUD, antes de las de los efectos (CL8 hito 2).
    UploadEach(dataRoot, HudTextureSet, textures, log);
    UploadEach(dataRoot, CrywolfTextureSet, textures, log);
    UploadEach(dataRoot, GlowTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, ClothTextureSet, textures, log);
    UploadEach(dataRoot, EffectTextureSet, textures, log, &m_textureMetadata);
    if (UploadEach(dataRoot, StartupSelectionTextureSet, textures, log, &m_textureMetadata) != 0)
        m_startupTextureStore = &textures;
    UploadEach(dataRoot, FaunaJointTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, GameTailTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, Tables::PlayerFixedTextures(), textures, log);
    UploadEach(dataRoot, m_slots.ToLoad(), textures, log, &m_textureMetadata);
    log.Info("se leyeron {} modelos del personaje, de los ítems y del caballo", m_models.size());
    return {};
}

// 0x60d5f0 por cada llamada: el modelo que no se leyó no tiene mallas, y no pide nada. El que se pide
// dos veces se queda con los slots de la segunda.
void GameModels::AssignTextures(std::span<const Tables::ModelTextureCall> calls)
{
    for (const Tables::ModelTextureCall& call : calls)
    {
        const auto found = m_models.find(call.model);
        if (found != m_models.end())
        {
            m_meshTextures.insert_or_assign(
                call.model, m_slots.Assign(found->second, call.folder, {Data::Textures::Filter::Nearest, call.wrap}));
        }
    }
}

const Data::Models::Model* GameModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found == m_models.end() ? nullptr : &found->second;
}

const Models::Skeleton* GameModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_skeletons.find(model);
    return found == m_skeletons.end() ? nullptr : &found->second;
}

std::span<const Render::TextureSlot> GameModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_meshTextures.find(model);
    return found == m_meshTextures.end() ? std::span<const Render::TextureSlot>{} : found->second;
}

std::optional<Models::TextureMetadata> GameModels::TextureMetadataOf(std::uint32_t selector) const
{
    const auto found = m_textureMetadata.find(selector);
    return found == m_textureMetadata.end() ? std::nullopt : std::optional(found->second);
}

std::optional<Render::TextureSlot> GameModels::TextureSlotOf(std::uint32_t selector) const
{
    // Solo las que subieron: la clave salió del slot de 16 bits de su entrada, así que no se trunca nada.
    if (!m_textureMetadata.contains(selector))
    {
        return std::nullopt;
    }
    return Render::TextureSlot{static_cast<std::uint16_t>(selector)};
}

std::optional<glm::vec2> GameModels::TextureSize(Render::TextureSlot slot) const
{
    const auto metadata = TextureMetadataOf(slot.value);
    return metadata ? std::optional(metadata->size) : std::nullopt;
}

std::optional<Models::TextureMetadata> GameModels::TakeStartupSelectionTexture(Render::ITextureStore& store)
{
    if (m_startupTextureStore != &store)
        return std::nullopt;
    m_startupTextureStore = nullptr;
    const auto entry = m_textureMetadata.extract(StartupSelectionTextureSlot);
    return entry.empty() ? std::nullopt : std::optional(entry.mapped());
}

std::size_t GameModels::Count() const
{
    return m_models.size();
}

const Models::ModelTextureSlots& GameModels::TextureSlots() const
{
    return m_slots;
}
} // namespace Client::Assets
