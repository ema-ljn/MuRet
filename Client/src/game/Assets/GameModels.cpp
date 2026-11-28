#include "Assets/GameModels.h"

#include "Assets/ModelTable.h"
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
#include "Models/MeshTraits.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Assets/TextureSets.h"
#include "Models/TextureMetadata.h"
#include "Models/ModelId.h"
#include "Tables/AddedEmotes.h"

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
#include <vector>

namespace Client::Assets
{
namespace
{
// The slots where each loader's textures start (06 § El modelo en el binario).
constexpr Render::TextureSlot PlayerTexturesStart{0x138};
constexpr Render::TextureSlot ItemTexturesStart{0x25a};
// 0x615700 (0x61692a), before the horse's.
constexpr Render::TextureSlot SkillTexturesStart{0x3b8};
// 0x615f77: the Fenrir ones, where those of 0x611850 end.
constexpr Render::TextureSlot FenrirTexturesStart{0x371};
// 0x61634e..0x616370: Gate02 before the 0x3b8 block; Blood Castle reassigns both Gates in the map.
constexpr Render::TextureSlot GateTexturesStart{0x69};
// Uploads those of `entries` one by one: one that does not load leaves its line in the log, and the rest go on.
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
    // On a reload, the models of the skills phase must still be absent during 0x611850.
    GameModels staged;
    staged.m_textureMetadata = m_textureMetadata;
    staged.m_addedEmotes = m_addedEmotes;
    if (m_startupTextureStore == &textures)
        staged.m_startupTextureStore = m_startupTextureStore;
    else
        staged.m_textureMetadata.erase(StartupSelectionTextureSlot);
    auto loaded = staged.LoadResources(dataRoot, textures, log);
    if (loaded.HasValue())
        *this = std::move(staged);
    return loaded;
}

void GameModels::UseAddedEmotes(std::vector<Tables::AddedEmote> emotes)
{
    m_addedEmotes = std::move(emotes);
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
                auto skeleton = file.model == Tables::PlayerSkeletonModel
                                    ? Assets::MakePlayerSkeleton(model, m_addedEmotes)
                                    : Models::MakeSkeleton(model);
                std::optional<Models::Skeleton> made;
                if (skeleton.HasValue())
                    made = std::move(skeleton).Value();
                m_models.Insert(file.model, std::move(model), std::move(made));
            }
        }
        return {};
    };
    const auto playerAndItems = loadTables({Tables::PlayerModelFiles(), Tables::ItemModelFiles()});
    if (!playerAndItems.HasValue())
        return playerAndItems;
    // 0x6117fd..0x611847: the end of 0x60ff50 takes the shine off five item meshes.
    for (const Tables::ModelMesh& marked : Tables::MeshesWithoutShine)
    {
        if (LoadedModel* item = m_models.Find(marked.model); item != nullptr && marked.mesh < item->meshes.size())
            item->meshes[marked.mesh].shines = false;
    }
    m_slots.SetNext(PlayerTexturesStart);
    AssignTextures(Tables::PlayerTextureCalls());
    m_slots.SetNext(ItemTexturesStart);
    AssignTextures(Tables::ItemTextureCalls());
    // 0x611850 queries 0xdb with Item\Bow\ before 0x616095 loads its BMD.
    const auto skills = loadTables({Tables::SkillModelFiles()});
    if (!skills.HasValue())
        return skills;
    // 0x61606f/0x61607f: the loader changes HellGate's action 0 after reading its BMD.
    if (Models::Skeleton* gate = m_models.SkeletonOf(0xc5); gate != nullptr && !gate->actions.empty())
    {
        auto& action = gate->actions.front();
        action.lockPositions = false;
        action.speed = 0.15f;
    }
    // 0x616276: Ball01's action 0 keeps 0.5 keys per step.
    if (Models::Skeleton* ball = m_models.SkeletonOf(0xf1); ball != nullptr && !ball->actions.empty())
        ball->actions.front().speed = 0.5f;
    m_slots.SetNext(FenrirTexturesStart);
    AssignTextures(Tables::FenrirTextureCalls());
    // 0x61613a queries 0x109 and 0x61619f queries 0x10f before loading their BMDs. In the profile without Data2/SMD
    // there are no meshes at that point, so the future BMD is not assigned now: its effective query comes with the
    // sweep of 0x616e46.
    m_slots.SetNext(GateTexturesStart);
    AssignTextures(Tables::SiegeBeforeSkillTextureCalls());
    // 0x6164a0..0x6164bb: LaceArrow overwrites slots 0x138 and 0x139, with Item\ and GL_CLAMP (Q-C89).
    if (LoadedModel* laceArrow = m_models.Find(0x11c); laceArrow != nullptr)
    {
        m_slots.SetNext(PlayerTexturesStart);
        constexpr Data::Textures::Sampling sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp};
        laceArrow->meshTextures = m_slots.Assign(laceArrow->model, "Item\\", sampling);
    }
    m_slots.SetNext(SkillTexturesStart);
    AssignTextures(Tables::SkillTextureCalls());
    for (std::uint16_t model = 0xbe; model < 0x151; ++model)
    {
        const std::array<Tables::ModelTextureCall, 1> call{{{model, "Skill\\"}}};
        AssignTextures(call);
    }
    UploadEach(dataRoot, DamageTextureSet, textures, log, &m_textureMetadata);
    // 0x61ccb0: the HUD's, before the effects'.
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
    log.Info("se leyeron {} modelos del personaje, de los ítems y del caballo", m_models.Count());
    return {};
}

// 0x60d5f0 for each call: a model that was not read has no meshes and requests nothing. One requested twice keeps the
// slots of the second.
void GameModels::AssignTextures(std::span<const Tables::ModelTextureCall> calls)
{
    for (const Tables::ModelTextureCall& call : calls)
    {
        if (LoadedModel* found = m_models.Find(call.model); found != nullptr)
            found->meshTextures =
                m_slots.Assign(found->model, call.folder, {Data::Textures::Filter::Nearest, call.wrap});
    }
}

const Data::Models::Model* GameModels::Find(Models::ModelId model) const
{
    return m_models.ModelOf(model.Value());
}

const Models::Skeleton* GameModels::SkeletonOf(Models::ModelId model) const
{
    return m_models.SkeletonOf(model.Value());
}

std::span<const Render::TextureSlot> GameModels::MeshTextures(Models::ModelId model) const
{
    return m_models.MeshTextures(model.Value());
}

std::span<const Models::MeshTraits> GameModels::MeshTraitsOf(Models::ModelId model) const
{
    return m_models.MeshTraitsOf(model.Value());
}

std::optional<Models::TextureMetadata> GameModels::TextureMetadataOf(std::uint32_t selector) const
{
    const auto found = m_textureMetadata.find(selector);
    return found == m_textureMetadata.end() ? std::nullopt : std::optional(found->second);
}

std::optional<Render::TextureSlot> GameModels::TextureSlotOf(std::uint32_t selector) const
{
    // Only those that were uploaded: the key came from the 16-bit slot of its entry, so nothing is truncated.
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
    return m_models.Count();
}

const Models::ModelTextureSlots& GameModels::TextureSlots() const
{
    return m_slots;
}
} // namespace Client::Assets
