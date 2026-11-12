#include "Scenes/GameModels.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextureCalls.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/TextureSets.h"
#include "Scenes/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
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
constexpr std::array<Data::Models::ModelTextureCall, 1> InitialGateTextures{{{0x10d, "Monster\\"}}};
// 0x6161b5: Data\Skill\RidingSpear01.bmd; el 0x10f del efecto necesita su modelo real. 0x6166fe:
// Data\Skill\MuSign.bmd, el 0x12a de la capa de los GM (0x414370).
// 0x615d6a: 0x60da30(0xc8 + k, "Data\\Skill\\", "Stone", k + 1), las piedritas que salta el chorro de Lost
// Tower (Stone01 y Stone02). 0x61641d y 0x616442: Data\Skill\groundStone.bmd y groundStone2.bmd, las piedras 0x118
// y 0x119 del pisotón del caballo oscuro (0x4453e0); después de cada una, 0x615700 escribe `**(modelo + 0x30) = 0`,
// el byte +0 de la acción 0 (el "no repite"), que el lector 0x4f13d0 ya dejó en 0 (ActionInfo::repeats), así que no
// cambia nada. 0x616880: Data\Skill\TowerGatePlane.bmd, el plano 0x135 que crea el objeto 79 del mapa 30 (0x42c6b0).
constexpr std::array<Data::Models::ModelFile, 7> TailModelFiles{{{0x10f, "Skill\\", "RidingSpear", 1},
                                                                 {0x12a, "Skill\\", "MuSign", -1},
                                                                 {0xc8, "Skill\\", "Stone", 1},
                                                                 {0xc9, "Skill\\", "Stone", 2},
                                                                 {0x118, "Skill\\", "groundStone", -1},
                                                                 {0x119, "Skill\\", "groundStone2", -1},
                                                                 {0x135, "Skill\\", "TowerGatePlane", -1}}};
// 0x61619f: 0x60d5f0(0x10f, "Skill\\", GL_REPEAT, GL_NEAREST, 1); 0x616c5c, el mismo con el 0x12a; el lazo
// 0x616e20 de 0xbe a 0x150 con "Skill\\": el Uniria (0xc6, que no tiene otra), el 0xc8, el 0xc9, el 0x118 y el 0x119
// (que 0x616bd2 y 0x616bed ya habían pedido) y el 0x135 (que 0x616d8e ya había pedido); los pedidos dos veces se quedan
// con los de esta segunda vez. Van después de las de 0x615700 que MuRet carga,
// así que sus slots no son los del original (que pasa antes por los modelos de las habilidades que MuRet todavía no
// lee); ningún slot fijo los pisa.
constexpr std::array<Data::Models::ModelTextureCall, 8> TailTextureCalls{{{0x10f, "Skill\\"},
                                                                          {0x12a, "Skill\\"},
                                                                          {0xc6, "Skill\\"},
                                                                          {0xc8, "Skill\\"},
                                                                          {0xc9, "Skill\\"},
                                                                          {0x118, "Skill\\"},
                                                                          {0x119, "Skill\\"},
                                                                          {0x135, "Skill\\"}}};

// Sube las de `entries` de a una: la que no carga deja su línea en el log, y las demás siguen.
void UploadEach(const std::filesystem::path& dataRoot, std::span<const Data::Textures::TextureSetEntry> entries,
                Render::ITextureStore& textures, Core::Log& log,
                std::map<std::uint32_t, TextureMetadata>* metadata = nullptr)
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
            metadata->insert_or_assign(static_cast<std::uint32_t>(entry.slot), TextureMetadata(texture));
        }
    }
    log.Info("las texturas de los modelos: {} subidas y {} salteadas", entries.size() - skipped, skipped);
}
} // namespace

Core::Expected<void, std::string> GameModels::Load(const std::filesystem::path& dataRoot,
                                                   Render::ITextureStore& textures, Core::Log& log)
{
    for (const std::span<const Data::Models::ModelFile> table :
         {Data::Models::PlayerModelFiles(), Data::Models::ItemModelFiles(), Data::Models::SkillModelFiles(),
          std::span<const Data::Models::ModelFile>(TailModelFiles)})
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
            auto skeleton = file.model == Data::Models::PlayerSkeletonModel ? Render::Models::MakePlayerSkeleton(model)
                                                                            : Render::Models::MakeSkeleton(model);
            if (skeleton.HasValue())
            {
                m_skeletons.insert_or_assign(file.model, std::move(skeleton).Value());
            }
            m_models.insert_or_assign(file.model, std::move(model));
        }
    }
    m_slots.SetNext(PlayerTexturesStart);
    AssignTextures(Data::Models::PlayerTextureCalls());
    m_slots.SetNext(ItemTexturesStart);
    AssignTextures(Data::Models::ItemTextureCalls());
    m_slots.SetNext(FenrirTexturesStart);
    AssignTextures(Data::Models::FenrirTextureCalls());
    m_slots.SetNext(GateTexturesStart);
    AssignTextures(InitialGateTextures);
    m_slots.SetNext(SkillTexturesStart);
    AssignTextures(Data::Models::SkillTextureCalls());
    AssignTextures(TailTextureCalls);
    UploadEach(dataRoot, DamageTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, GlowTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, ClothTextureSet, textures, log);
    UploadEach(dataRoot, EffectTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, FaunaJointTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, GameTailTextureSet, textures, log, &m_textureMetadata);
    UploadEach(dataRoot, Data::Models::PlayerFixedTextures(), textures, log);
    UploadEach(dataRoot, m_slots.ToLoad(), textures, log, &m_textureMetadata);
    log.Info("se leyeron {} modelos del personaje, de los ítems y del caballo", m_models.size());
    return {};
}

// 0x60d5f0 por cada llamada: el modelo que no se leyó no tiene mallas, y no pide nada. El que se pide
// dos veces se queda con los slots de la segunda.
void GameModels::AssignTextures(std::span<const Data::Models::ModelTextureCall> calls)
{
    for (const Data::Models::ModelTextureCall& call : calls)
    {
        const auto found = m_models.find(call.model);
        if (found != m_models.end())
        {
            m_meshTextures.insert_or_assign(
                call.model, m_slots.Assign(found->second, call.folder, Data::Models::ModelTextureSampling));
        }
    }
}

const Data::Models::Model* GameModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found == m_models.end() ? nullptr : &found->second;
}

const Render::Models::Skeleton* GameModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_skeletons.find(model);
    return found == m_skeletons.end() ? nullptr : &found->second;
}

std::span<const Render::TextureSlot> GameModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_meshTextures.find(model);
    return found == m_meshTextures.end() ? std::span<const Render::TextureSlot>{} : found->second;
}

std::optional<TextureMetadata> GameModels::TextureMetadataOf(std::uint32_t selector) const
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

std::size_t GameModels::Count() const
{
    return m_models.size();
}

const Render::Models::ModelTextureSlots& GameModels::TextureSlots() const
{
    return m_slots;
}
} // namespace Client::Scenes
