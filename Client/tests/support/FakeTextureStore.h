#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// La tabla de texturas falsa de los tests sin GL (CL5.3; la usan también las escenas de la CL5.4):
// guarda lo que se sube por su slot y anota cada Load y cada Release, en orden. Con
// `failNextLoad`, el próximo Load falla sin subir nada.
namespace Client::Tests
{
// Una llamada a la tabla, con su slot: para ver qué pasó antes, una subida o una suelta.
struct StoreCall
{
    enum class Kind : std::uint8_t
    {
        Load,
        Release,
    };
    Kind kind = Kind::Load;
    std::uint16_t slot = 0;
};

class FakeTextureStore final : public Render::ITextureStore
{
public:
    [[nodiscard]] Core::Expected<void, std::string>
    Load(std::span<const Data::Textures::LoadedTexture> textures) override
    {
        ++loadCalls;
        if (failNextLoad)
        {
            failNextLoad = false;
            return Core::Unexpected(std::string("la tabla falsa no sube"));
        }
        for (const Data::Textures::LoadedTexture& texture : textures)
        {
            loaded.push_back(texture.slot);
            calls.push_back(StoreCall{StoreCall::Kind::Load, texture.slot});
            m_textures.insert_or_assign(texture.slot, texture);
        }
        return {};
    }

    void Release(Render::TextureSlot slot) override
    {
        released.push_back(slot.value);
        calls.push_back(StoreCall{StoreCall::Kind::Release, slot.value});
        m_textures.erase(slot.value);
    }

    // Lo que hay en `slot`, o std::nullopt.
    [[nodiscard]] std::optional<Data::Textures::LoadedTexture> Find(Render::TextureSlot slot) const
    {
        const auto found = m_textures.find(slot.value);
        if (found == m_textures.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    // Cuántas texturas tiene ahora.
    [[nodiscard]] std::size_t Size() const
    {
        return m_textures.size();
    }

    int loadCalls = 0;
    bool failNextLoad = false;
    // Los slots subidos y los soltados, en orden.
    std::vector<std::uint16_t> loaded;
    std::vector<std::uint16_t> released;
    // Los dos, juntos y en orden.
    std::vector<StoreCall> calls;

private:
    std::map<std::uint16_t, Data::Textures::LoadedTexture> m_textures;
};
} // namespace Client::Tests
