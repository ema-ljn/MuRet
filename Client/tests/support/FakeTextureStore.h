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

// The fake texture table of the tests without GL: stores what is uploaded by slot and records each Load and each
// Release, in order. With `failNextLoad`, the next Load fails without uploading anything.
namespace Client::Tests
{
// A call to the table, with its slot: to see what happened first, an upload or a release.
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

    [[nodiscard]] std::optional<Data::Textures::LoadedTexture> Find(Render::TextureSlot slot) const
    {
        const auto found = m_textures.find(slot.value);
        if (found == m_textures.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    [[nodiscard]] std::size_t Size() const
    {
        return m_textures.size();
    }

    int loadCalls = 0;
    bool failNextLoad = false;
    // The uploaded and the released slots, in order.
    std::vector<std::uint16_t> loaded;
    std::vector<std::uint16_t> released;
    // Both, together and in order.
    std::vector<StoreCall> calls;

private:
    std::map<std::uint16_t, Data::Textures::LoadedTexture> m_textures;
};
} // namespace Client::Tests
