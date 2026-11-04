#include "Render/Models/ModelTextureSlots.h"

#include "Data/Models/Model.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Render::Models
{
namespace
{
using Data::Textures::Filter;
using Data::Textures::Sampling;
using Data::Textures::TextureLoader;

// La ruta que se pide para una textura de modelo: la carpeta, el nombre hasta el primer punto y la
// extensión que Data::Textures::TextureLoaderOf lee como el cargador que eligió el original.
std::string RequestPath(std::string_view folder, std::string_view name, TextureLoader loader)
{
    std::string path(folder);
    path += name.substr(0, name.find('.'));
    path += loader == TextureLoader::Ozt ? ".tga" : ".jpg";
    return path;
}
} // namespace

ModelTextureSlots::ModelTextureSlots(TextureSlot next) : m_names(OriginalTextureSlotCount), m_next(next.value) {}

ModelTextureSlots ModelTextureSlots::ContinuedAt(TextureSlot next) const
{
    ModelTextureSlots continued(next);
    continued.m_names = m_names;
    return continued;
}

void ModelTextureSlots::SetNext(TextureSlot next)
{
    m_next = next.value;
}

std::vector<TextureSlot> ModelTextureSlots::Assign(const Data::Models::Model& model, std::string_view folder,
                                                   Sampling sampling)
{
    std::vector<TextureSlot> slots;
    slots.reserve(model.meshes.size());
    for (const Data::Models::Mesh& mesh : model.meshes)
    {
        const std::optional<TextureSlot> found = Find(mesh.textureName);
        TextureSlot slot = found.has_value() ? *found : Request(folder, mesh.textureName, sampling);
        const std::optional<std::uint16_t> skin = Data::Models::SkinSlotOf(mesh.textureName);
        if (skin.has_value())
        {
            slot = TextureSlot{*skin};
        }
        slots.push_back(slot);
    }
    return slots;
}

std::span<const Data::Textures::TextureSetEntry> ModelTextureSlots::ToLoad() const
{
    return m_toLoad;
}

TextureSlot ModelTextureSlots::Next() const
{
    return TextureSlot{static_cast<std::uint16_t>(m_next)};
}

std::string_view ModelTextureSlots::NameOf(TextureSlot slot) const
{
    return slot.value < m_names.size() ? std::string_view(m_names[slot.value]) : std::string_view{};
}

// 0x60d550: del slot 0 al próximo libre, sin contar este.
std::optional<TextureSlot> ModelTextureSlots::Find(std::string_view name) const
{
    for (std::size_t slot = 0; slot < m_next && slot < m_names.size(); ++slot)
    {
        if (m_names[slot] == name)
        {
            return TextureSlot{static_cast<std::uint16_t>(slot)};
        }
    }
    return std::nullopt;
}

TextureSlot ModelTextureSlots::Request(std::string_view folder, std::string_view name, Sampling sampling)
{
    const TextureSlot slot{static_cast<std::uint16_t>(m_next)};
    if (m_next < m_names.size())
    {
        m_names[m_next] = name;
    }
    ++m_next;
    const TextureLoader loader = Data::Models::ModelTextureLoaderOf(name);
    if (loader == TextureLoader::Ozt)
    {
        sampling.filter = Filter::Nearest;
    }
    const std::string& path = m_paths.emplace_back(RequestPath(folder, name, loader));
    m_toLoad.push_back({path, slot.value, sampling});
    return slot;
}
} // namespace Client::Render::Models
