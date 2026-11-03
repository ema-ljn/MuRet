#include "Render/Text/TextCache.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/OriginalQuad.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextLayout.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <tuple>
#include <utility>

namespace Client::Render::Text
{
namespace
{
// Las cajas de un texto de `width` columnas: una por cada 256, la última con lo que sobra.
std::size_t BoxesFor(int width)
{
    if (width <= 0)
    {
        return 0;
    }
    return static_cast<std::size_t>((width + TextBoxWidth - 1) / TextBoxWidth);
}

// Los slots de la caché que entran en un std::uint16_t desde `firstSlot`.
std::size_t SlotsThatFit(TextureSlot firstSlot, std::size_t slotCount)
{
    const std::size_t room = std::size_t{std::numeric_limits<std::uint16_t>::max()} + 1 - firstSlot.value;
    return std::min(slotCount, room);
}
} // namespace

TextCache::TextCache(const IGlyphRasterizer& rasterizer, ITextureStore& store, TextureSlot firstSlot,
                     std::size_t slotCount)
    : m_rasterizer(&rasterizer), m_store(&store), m_slotCount(SlotsThatFit(firstSlot, slotCount))
{
    // Una pila: el de atrás es el próximo que se da. Arranca con el primero atrás, así los slots
    // salen en orden, y uno que se suelta es el próximo que se da.
    m_freeSlots.reserve(m_slotCount);
    for (std::size_t i = m_slotCount; i > 0; --i)
    {
        m_freeSlots.push_back(TextureSlot{static_cast<std::uint16_t>(firstSlot.value + i - 1)});
    }
}

TextCache::~TextCache()
{
    Clear();
}

Core::Expected<void, std::string> TextCache::Draw(SpriteBatch& batch, int x, int y, std::string_view text,
                                                  const TextStyle& style)
{
    if (text.empty())
    {
        return {};
    }
    const Core::Expected<Entries::iterator, std::string> found = Find(Key{style.font, style.colors, text});
    if (!found.HasValue())
    {
        return Core::Unexpected(found.Error());
    }
    const Entry& entry = *found.Value();
    for (const TextPiece& piece : TextPieces(x, y, entry.extent, batch.Screen()))
    {
        const auto box = static_cast<std::size_t>(piece.firstColumn / TextBoxWidth);
        if (box >= entry.boxes.size())
        {
            break;
        }
        OriginalQuad quad = piece.quad;
        quad.slot = entry.boxes[box];
        batch.Quad(quad);
    }
    return {};
}

TextExtent TextCache::Measure(FontKind font, std::string_view text) const
{
    return m_rasterizer->Measure(font, text);
}

void TextCache::ChangeRasterizer(const IGlyphRasterizer& rasterizer)
{
    Clear();
    m_rasterizer = &rasterizer;
}

void TextCache::Clear()
{
    while (!m_entries.empty())
    {
        EvictOldest();
    }
}

std::size_t TextCache::Size() const
{
    return m_entries.size();
}

std::size_t TextCache::UsedSlots() const
{
    return m_slotCount - m_freeSlots.size();
}

bool TextCache::KeyLess::operator()(const Key& a, const Key& b) const
{
    return std::tie(a.font, a.colors.text, a.colors.background, a.text) <
           std::tie(b.font, b.colors.text, b.colors.background, b.text);
}

Core::Expected<TextCache::Entries::iterator, std::string> TextCache::Find(const Key& key)
{
    if (const auto indexed = m_index.find(key); indexed != m_index.end())
    {
        // El que se dibuja pasa adelante. splice no mueve el nodo: el índice sigue valiendo.
        m_entries.splice(m_entries.begin(), m_entries, indexed->second);
        return indexed->second;
    }

    const TextExtent extent = m_rasterizer->Measure(key.font, key.text);
    const std::size_t boxes = BoxesFor(extent.width);
    if (boxes > m_slotCount)
    {
        return Core::Unexpected(std::format("el texto pide {} cajas y la caché tiene {} slots", boxes, m_slotCount));
    }
    while (m_freeSlots.size() < boxes)
    {
        EvictOldest();
    }

    Entry entry{key.font, key.colors, std::string(key.text), extent, {}};
    entry.boxes.reserve(boxes);
    std::vector<Data::Textures::LoadedTexture> textures;
    textures.reserve(boxes);
    const TextStyle style{key.font, key.colors};
    const int rows = std::min(extent.height, TextBoxHeight);
    for (std::size_t k = 0; k < boxes; ++k)
    {
        const TextureSlot slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        entry.boxes.push_back(slot);

        const int firstColumn = static_cast<int>(k) * TextBoxWidth;
        Data::Textures::Texture texture{.width = TextBoxWidth,
                                        .height = TextBoxHeight,
                                        .textureWidth = TextBoxWidth,
                                        .textureHeight = TextBoxHeight,
                                        .format = Data::Textures::PixelFormat::Rgba,
                                        .pixels = std::vector<std::uint8_t>(TextBoxBytes)};
        PaintTextBox(*m_rasterizer, style, key.text, firstColumn, std::min(TextBoxWidth, extent.width - firstColumn),
                     rows, TextBoxPixels(texture.pixels.data(), TextBoxBytes));
        // Como 0x63bd00 sube la FontInput.tga del slot 0: GL_NEAREST y GL_CLAMP.
        textures.push_back(Data::Textures::LoadedTexture{
            slot.value, Data::Textures::Sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp},
            std::move(texture)});
    }

    if (const Core::Expected<void, std::string> loaded = m_store->Load(textures); !loaded.HasValue())
    {
        // Las que sí subió quedan en la tabla: se sueltan, y los slots vuelven a la pila.
        for (auto slot = entry.boxes.rbegin(); slot != entry.boxes.rend(); ++slot)
        {
            m_store->Release(*slot);
            m_freeSlots.push_back(*slot);
        }
        return Core::Unexpected(std::format("no se subió el texto a la caché: {}", loaded.Error()));
    }

    m_entries.push_front(std::move(entry));
    const Entry& front = m_entries.front();
    m_index.emplace(Key{front.font, front.colors, front.text}, m_entries.begin());
    return m_entries.begin();
}

void TextCache::EvictOldest()
{
    if (m_entries.empty())
    {
        return;
    }
    const Entry& oldest = m_entries.back();
    m_index.erase(Key{oldest.font, oldest.colors, oldest.text});
    // Al revés, para que la pila los vuelva a dar en el orden en que estaban.
    for (auto slot = oldest.boxes.rbegin(); slot != oldest.boxes.rend(); ++slot)
    {
        m_store->Release(*slot);
        m_freeSlots.push_back(*slot);
    }
    m_entries.pop_back();
}
} // namespace Client::Render::Text
