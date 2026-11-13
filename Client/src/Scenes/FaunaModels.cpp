#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterSounds.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ConstructedBmdPrefix.h"
#include "Scenes/TextureMetadata.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/ext/vector_float2.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t IcarusMonster = 0x1f;
constexpr std::int32_t KalimaMonster = 0x21;
constexpr std::uint16_t MonsterModelBase = 0x152;
constexpr std::array<float, 7> CommonSpeeds{.25f, .2f, .34f, .33f, .33f, .5f, .55f};
constexpr std::array<std::int16_t, 10> IcarusSounds{0x7b, 0x7b, 0x7c, 0x7c, 0x7d, -1, -1, -1, -1, -1};
constexpr std::array<std::int16_t, 10> KalimaSounds{0x82, 0x82, 0x83, 0x83, 0x82, -1, -1, -1, -1, -1};
constexpr std::array<std::int16_t, 10> NoSounds{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr Data::Textures::Sampling MonsterSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};
Core::Expected<void, std::string> Configure(Render::Models::Skeleton& skeleton, bool icarus)
{
    // 612e5a..612e9f: siete stores de velocidad antes del flag de acción6.
    for (std::size_t i = 0; i < CommonSpeeds.size(); ++i)
    {
        if (i >= skeleton.actions.size())
            return Core::Unexpected(std::string("612e5a: acciones comunes de monstruo ausentes"));
        skeleton.actions[i].speed = CommonSpeeds[i];
    }
    skeleton.actions[6].repeats = false;
    if (!icarus)
        return {};
    skeleton.actions[1].repeats = false; // 613120, tabla615324 del monstruo1f.
    return {};
}
} // namespace
FaunaModels::~FaunaModels()
{
    Release();
}
void FaunaModels::Bind(const IModelLibrary* base)
{
    m_base = base;
}
Core::Expected<void, std::string> FaunaModels::InitializePrefix(std::uint16_t constructedBeforeBase)
{
    return m_prefix.Initialize(constructedBeforeBase);
}
const Render::Models::Skeleton* FaunaModels::PrefixModel() const
{
    return m_prefix.BorrowForSetAction();
}
std::optional<std::uint16_t> FaunaModels::PrefixOffset() const
{
    return m_prefix.Offset();
}
Core::Expected<void, std::string> FaunaModels::Prepare(std::int32_t monster, FaunaModelLoadContext& context)
{
    if (monster != IcarusMonster && monster != KalimaMonster)
        return Core::Unexpected(std::string("612d90: familia de recurso de fauna no admitida"));
    const auto type = static_cast<std::uint16_t>(monster + MonsterModelBase);
    const auto found = m_models.find(type);
    // .data6c9e0c=1: reutiliza sólo si hay acciones o mallas (612dbc/612dc7).
    if (found != m_models.end() && (!found->second.model.meshes.empty() || !found->second.skeleton.actions.empty()))
        return {};
    auto loaded = Data::Models::LoadModelFile(context.dataRoot, {type, "Monster\\", "Monster", monster + 1});
    if (!loaded.HasValue())
        return Core::Unexpected("612de1: no se pudo leer " + loaded.Error().path);
    auto skeleton = Render::Models::MakeSkeleton(loaded.Value());
    if (!skeleton.HasValue())
        return Core::Unexpected(std::string("612de1: esqueleto de fauna inválido"));
    auto& entry = m_models[type];
    entry.model = std::move(loaded).Value();
    entry.skeleton = std::move(skeleton).Value();
    if (entry.model.meshes.empty())
        return {}; // 612de9: sin mallas, no pide texturas ni toca acciones.
    // 0x613127: el caso del switch de `monster` con su tabla de sonidos (0x612c80; el 0x1f y el 0x21 la escriben). Las
    // texturas, que van antes, no la cambian.
    if (context.sounds != nullptr)
        WriteMonsterSounds(monster, *context.sounds);
    const auto first = context.slots.ToLoad().size();
    entry.textures = context.slots.Assign(entry.model, "Monster\\", MonsterSampling);
    m_store = &context.textures;
    m_sharedTextures = context.sharedTextures;
    if (m_sharedTextures != nullptr)
        m_sharedTextures->Bind(context.textures);
    std::vector<Render::TextureSlot> uploadedHere;
    for (const auto& request : context.slots.ToLoad().subspan(first))
    {
        auto texture = Data::Textures::LoadTextureSet(context.dataRoot, std::span(&request, 1));
        if (!texture.HasValue())
        {
            context.log.Warning("una textura de fauna se saltea: {}", Data::Textures::Describe(texture.Error()));
            continue;
        }
        auto uploaded = m_sharedTextures != nullptr ? m_sharedTextures->Upload(texture.Value().front())
                                                    : context.textures.Load(texture.Value());
        if (!uploaded.HasValue())
        {
            context.log.Warning("una textura de fauna no sube: {}", uploaded.Error());
            continue;
        }
        m_uploaded.push_back(Render::TextureSlot{request.slot});
        uploadedHere.push_back(Render::TextureSlot{request.slot});
        m_metadata.insert_or_assign(request.slot, TextureMetadata(texture.Value().front().texture));
    }
    if (m_sharedTextures != nullptr)
        for (const auto slot : entry.textures)
        {
            const auto uploaded = std::find(uploadedHere.begin(), uploadedHere.end(), slot);
            if (uploaded != uploadedHere.end())
                uploadedHere.erase(uploaded);
            else
                m_sharedTextures->Acquire(slot);
        }
    if (auto configured = Configure(entry.skeleton, monster == IcarusMonster); !configured.HasValue())
        return configured;
    // Los pedidos de audio quedan explícitos para el consumidor lógico (el cliente aún no reproduce WAV).
    if (monster == IcarusMonster)
    {
        m_sounds.push_back({0x14d, "Sound\\mYeti1.wav"});
        m_sounds.push_back({0x14e, "Sound\\mBullAttack1.wav"});
        m_sounds.push_back({0x14f, "Sound\\mYetiDie.wav"});
        entry.sounds = IcarusSounds;
    }
    else
    {
        m_sounds.push_back({0x154, "Sound\\mBahamut1.wav"});
        m_sounds.push_back({0x155, "Sound\\mYeti1.wav"});
        entry.sounds = KalimaSounds;
    }
    if (monster == IcarusMonster)
    {
        // 6140ac..6140c9: estos stores vienen DESPUÉS de los tres pedidos de audio y 612c80.
        entry.skeleton.actions[3].speed = .5f;
        entry.skeleton.actions[4].speed = .7f;
        entry.skeleton.actions[1].speed = .8f;
        if (entry.skeleton.actions.size() <= 7)
            return Core::Unexpected(std::string("6140c9: falta acción7 de Monster32"));
        entry.skeleton.actions[7].speed = .8f;
    }
    return {};
}
void FaunaModels::Release()
{
    if (m_sharedTextures != nullptr)
    {
        for (const auto& [model, entry] : m_models)
            for (const auto slot : entry.textures)
                if (slot.value != 0x12e)
                    m_sharedTextures->Release(slot);
    }
    else if (m_store != nullptr)
        for (const auto slot : m_uploaded)
            m_store->Release(slot);
    m_models.clear();
    m_metadata.clear();
    m_uploaded.clear();
    m_sounds.clear();
    m_store = nullptr;
    m_sharedTextures = nullptr;
    m_base = nullptr;
}
ModelTextureTable& FaunaModels::TextureTable()
{
    return m_textureTable;
}
const Data::Models::Model* FaunaModels::Find(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? &found->second.model : m_base != nullptr ? m_base->Find(model) : nullptr;
}
const Render::Models::Skeleton* FaunaModels::SkeletonOf(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? &found->second.skeleton : m_base != nullptr ? m_base->SkeletonOf(model) : nullptr;
}
std::span<const Render::TextureSlot> FaunaModels::MeshTextures(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    if (found != m_models.end())
        return found->second.textures;
    return m_base != nullptr ? m_base->MeshTextures(model) : std::span<const Render::TextureSlot>{};
}
std::optional<TextureMetadata> FaunaModels::TextureMetadataOf(std::uint32_t selector) const
{
    if (m_sharedTextures != nullptr)
        return m_sharedTextures->Metadata(selector);
    const auto found = m_metadata.find(selector);
    if (found != m_metadata.end())
        return found->second;
    return m_base != nullptr ? m_base->TextureMetadataOf(selector) : std::nullopt;
}
std::optional<Render::TextureSlot> FaunaModels::TextureSlotOf(std::uint32_t selector) const
{
    if (m_sharedTextures != nullptr)
        return m_sharedTextures->Metadata(selector)
                   ? std::optional(Render::TextureSlot{static_cast<std::uint16_t>(selector)})
                   : std::nullopt;
    if (m_base != nullptr)
        if (auto slot = m_base->TextureSlotOf(selector))
            return slot;
    const auto found = m_metadata.find(selector);
    return found != m_metadata.end() ? std::optional(Render::TextureSlot{static_cast<std::uint16_t>(selector)})
                                     : std::nullopt;
}
std::optional<glm::vec2> FaunaModels::TextureSize(Render::TextureSlot slot) const
{
    const auto metadata = TextureMetadataOf(slot.value);
    return metadata ? std::optional(metadata->size) : std::nullopt;
}
std::span<const FaunaResourceSound> FaunaModels::SoundFiles() const
{
    return m_sounds;
}
std::span<const std::int16_t, 10> FaunaModels::SoundActions(std::uint16_t model) const
{
    const auto found = m_models.find(model);
    return found != m_models.end() ? std::span<const std::int16_t, 10>(found->second.sounds)
                                   : std::span<const std::int16_t, 10>(NoSounds);
}
} // namespace Client::Scenes
