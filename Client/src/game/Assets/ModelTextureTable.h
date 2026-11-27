#pragma once
#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
namespace Client::Render
{
class ITextureStore;
}
namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Assets
{
// Registro físico durable de 60d550/63c150. El cursor puede retroceder; los nombres siguen vivos.
class ModelTextureTable final
{
public:
    void Bind(Render::ITextureStore& store);
    // El backend que ligó Bind; nullptr antes.
    [[nodiscard]] Render::ITextureStore* Store() const;
    void Seed(const Models::ModelTextureSlots& slots, const Models::IModelLibrary* models);
    [[nodiscard]] Models::ModelTextureSlots& Slots();
    [[nodiscard]] const Models::ModelTextureSlots& Slots() const;
    [[nodiscard]] Core::Expected<void, std::string> Upload(const Data::Textures::LoadedTexture& texture);
    void Acquire(Render::TextureSlot slot);
    void Release(Render::TextureSlot slot);
    [[nodiscard]] std::uint8_t References(Render::TextureSlot slot) const;
    [[nodiscard]] std::optional<Models::TextureMetadata> Metadata(std::uint32_t selector) const;

private:
    Render::ITextureStore* m_store = nullptr;
    bool m_seeded = false;
    Models::ModelTextureSlots m_slots{Render::TextureSlot{0x69}};
    struct Entry
    {
        std::uint8_t references;
        Models::TextureMetadata metadata;
    };
    std::map<std::uint32_t, Entry> m_entries;
};
} // namespace Client::Assets
