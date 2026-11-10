#include "Scenes/ModelTextureTable.h"
#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include <cstdint>
#include "Scenes/TextureMetadata.h"
#include <limits>
#include <optional>
#include <span>
#include <string>
namespace Client::Scenes
{
void ModelTextureTable::Bind(Render::ITextureStore& store)
{
    m_store = &store;
}
void ModelTextureTable::Seed(const Render::Models::ModelTextureSlots& slots, const IModelLibrary* models)
{
    if (m_seeded)
        return;
    m_seeded = true;
    m_slots = slots.ContinuedAt(Render::TextureSlot{0x69});
    if (models == nullptr)
        return;
    for (std::uint32_t selector = 0; selector < Render::OriginalTextureSlotCount; ++selector)
        if (const auto metadata = models->TextureMetadataOf(selector))
            m_entries.emplace(selector, Entry{0, *metadata});
    for (std::uint32_t model = 0; model <= std::numeric_limits<std::uint16_t>::max(); ++model)
        for (const auto slot : models->MeshTextures(static_cast<std::uint16_t>(model)))
            Acquire(slot);
    for (auto& [selector, entry] : m_entries)
        if (entry.references == 0)
            entry.references = 1;
}
Render::Models::ModelTextureSlots& ModelTextureTable::Slots()
{
    return m_slots;
}
const Render::Models::ModelTextureSlots& ModelTextureTable::Slots() const
{
    return m_slots;
}
Core::Expected<void, std::string> ModelTextureTable::Upload(const Data::Textures::LoadedTexture& texture)
{
    if (m_store == nullptr)
        return Core::Unexpected(std::string("63bfcf: falta el backend de texturas"));
    // La subida puede pisar un slot con referencias vivas: conserva el byte tras decrementarlo.
    const Render::TextureSlot slot{texture.slot};
    const auto name = std::string(m_slots.NameOf(slot));
    const auto old = References(slot);
    const auto loaded = m_store->Load(std::span(&texture, 1));
    if (!loaded.HasValue())
        return loaded;
    const auto remaining = old > 0 ? static_cast<std::uint8_t>(old - 1) : std::uint8_t{0};
    m_entries.insert_or_assign(texture.slot,
                               Entry{static_cast<std::uint8_t>(remaining + 1), TextureMetadata(texture.texture)});
    m_slots.SetName(slot, name);
    return {};
}
void ModelTextureTable::Acquire(Render::TextureSlot slot)
{
    const auto found = m_entries.find(slot.value);
    if (found != m_entries.end())
        ++found->second.references;
}
void ModelTextureTable::Release(Render::TextureSlot slot)
{
    const auto found = m_entries.find(slot.value);
    if (found == m_entries.end() || found->second.references == 0)
        return;
    if (--found->second.references != 0)
        return;
    if (m_store != nullptr)
        m_store->Release(slot);
    m_slots.SetName(slot, {});
    m_entries.erase(found);
}
std::uint8_t ModelTextureTable::References(Render::TextureSlot slot) const
{
    const auto found = m_entries.find(slot.value);
    return found == m_entries.end() ? std::uint8_t{0} : found->second.references;
}
std::optional<TextureMetadata> ModelTextureTable::Metadata(std::uint32_t selector) const
{
    const auto found = m_entries.find(selector);
    return found == m_entries.end() ? std::nullopt : std::optional(found->second.metadata);
}
} // namespace Client::Scenes
