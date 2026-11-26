#include "Render/Text/TextCache.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Client::Render::Text
{
namespace
{
// One box per 256 columns, the last with the remainder.
std::size_t BoxesFor(int width)
{
    if (width <= 0)
    {
        return 0;
    }
    return static_cast<std::size_t>((width + TextBoxWidth - 1) / TextBoxWidth);
}

bool BatchUses(const SpriteBatch& batch, const std::vector<TextureSlot>& boxes)
{
    return std::ranges::any_of(batch.Commands(),
                               [&boxes](const DrawCommand& command) {
                                   return command.texture && std::ranges::find(boxes, *command.texture) != boxes.end();
                               });
}

// The cache slots from `firstSlot` that fit in a std::uint16_t.
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
    // A stack: the back is the next one given. It starts with the first at the back, so slots come out in order, and a
    // released one is the next given.
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
    const Core::Expected<Entries::iterator, std::string> found = Find(Key{style.font, style.colors, text}, batch);
    if (!found.HasValue())
    {
        return Core::Unexpected(found.Error());
    }
    DrawPieces(batch, *found.Value(), x, y);
    return {};
}

Core::Expected<void, std::string> TextCache::DrawClippedAtLeft(SpriteBatch& batch, int x, int y, std::string_view text,
                                                               const TextStyle& style)
{
    if (x >= 0)
    {
        return Draw(batch, x, y, text, style);
    }
    if (text.empty())
    {
        return {};
    }
    // 0x4670f9..0x46711c: the width is cx + x, 0 if negative, and x becomes 0.
    if (m_rasterizer->Measure(style.font, text).width + x <= 0)
    {
        return {};
    }
    const Core::Expected<Entries::iterator, std::string> found = Find(Key{style.font, style.colors, text, -x}, batch);
    if (!found.HasValue())
    {
        return Core::Unexpected(found.Error());
    }
    DrawPieces(batch, *found.Value(), 0, y);
    return {};
}

Core::Expected<void, std::string> TextCache::DrawColumns(SpriteBatch& batch, int x, int y, std::string_view text,
                                                         const TextStyle& style, int firstColumn, int columns)
{
    // 0x4671b4..0x4671cf: below 0 columns, the rep stos clearing the DIB gets 3 · columns / 4 in ecx, which unsigned is
    // huge.
    if (columns < 0)
        return Core::Unexpected(
            std::format("0x4671cf: la caja de {} columnas: el rep stos limpiaría fuera del DIB", columns));
    // 0x467289: no columns, no pieces.
    if (columns == 0)
        return {};
    const Core::Expected<Entries::iterator, std::string> found =
        Find(Key{.font = style.font, .colors = style.colors, .text = text, .skipped = firstColumn, .columns = columns},
             batch);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    DrawPieces(batch, *found.Value(), x, y);
    return {};
}

Core::Expected<void, std::string> TextCache::DrawColumnsAtPixels(SpriteBatch& batch, int pixelX, int pixelY,
                                                                 std::string_view text, const TextStyle& style,
                                                                 int firstColumn, int columns)
{
    if (columns < 0)
        return Core::Unexpected(
            std::format("0x4671cf: la caja de {} columnas: el rep stos limpiaría fuera del DIB", columns));
    if (columns == 0)
        return {};

    const Core::Expected<Entries::iterator, std::string> found =
        Find(Key{.font = style.font, .colors = style.colors, .text = text, .skipped = firstColumn, .columns = columns},
             batch);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    DrawPiecesAtPixels(batch, *found.Value(), pixelX, pixelY);
    return {};
}

Core::Expected<void, std::string> TextCache::DrawBoxed(SpriteBatch& batch, int x, int y, std::string_view text,
                                                       const TextStyle& style, const BoxShape& box, bool clampY)
{
    if (box.width <= 0 || box.rows <= 0)
        return {};
    const Core::Expected<Entries::iterator, std::string> found =
        Find(Key{.font = style.font, .colors = style.colors, .text = text, .skipped = 0, .box = box}, batch);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    OriginalQuad quad = BoxedQuad(x, y, box.width, box.rows, clampY, batch.Screen());
    quad.slot = found.Value()->boxes.front();
    batch.Quad(quad);
    return {};
}

void TextCache::DrawPieces(SpriteBatch& batch, const Entry& entry, int x, int y)
{
    for (const TextPiece& piece : TextPieces(x, y, entry.extent, batch.Screen()))
    {
        const auto box = static_cast<std::size_t>(piece.firstColumn / TextBoxWidth);
        if (box >= entry.boxes.size())
            break;
        OriginalQuad quad = piece.quad;
        quad.slot = entry.boxes[box];
        batch.Quad(quad);
    }
}

void TextCache::DrawPiecesAtPixels(SpriteBatch& batch, const Entry& entry, int pixelX, int pixelY)
{
    for (const TextPiece& piece : TextPiecesAtPixels(pixelX, pixelY, entry.extent, batch.Screen()))
    {
        const auto box = static_cast<std::size_t>(piece.firstColumn / TextBoxWidth);
        if (box >= entry.boxes.size())
            break;
        OriginalQuad quad = piece.quad;
        quad.slot = entry.boxes[box];
        batch.Quad(quad);
    }
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
    return std::tie(a.font, a.colors.text, a.colors.background, a.text, a.skipped, a.box.width, a.box.rows,
                    a.box.prefixColumns, a.box.prefixColor,
                    a.columns) < std::tie(b.font, b.colors.text, b.colors.background, b.text, b.skipped, b.box.width,
                                          b.box.rows, b.box.prefixColumns, b.box.prefixColor, b.columns);
}

Core::Expected<TextCache::Entries::iterator, std::string> TextCache::Find(const Key& key, const SpriteBatch& batch)
{
    if (const auto indexed = m_index.find(key); indexed != m_index.end())
    {
        // The drawn one moves to the front. splice does not move the node: the index stays valid.
        m_entries.splice(m_entries.begin(), m_entries, indexed->second);
        return indexed->second;
    }

    const bool boxed = key.box.width > 0;
    TextExtent extent = m_rasterizer->Measure(key.font, key.text);
    extent.width = std::max(extent.width - key.skipped, 0);
    // 0x4670ba..0x4670d0: an empty text's height is that of "0".
    if (key.columns > 0)
        extent = TextExtent{.width = key.columns,
                            .height = m_rasterizer->Measure(key.font, key.text.empty() ? "0" : key.text).height};
    if (boxed)
        extent = TextExtent{.width = key.box.width, .height = key.box.rows};
    // A bubble's box is a single one: the quad shows more than 256 columns by repeating the last.
    const std::size_t boxes = boxed ? 1 : BoxesFor(extent.width);
    if (boxes > m_slotCount)
    {
        return Core::Unexpected(std::format("el texto pide {} cajas y la caché tiene {} slots", boxes, m_slotCount));
    }
    // With fewer free slots than boxes some slot is taken, so there is an entry at the back. If it is already in the
    // batch, the ones ahead were drawn later in the same frame: stop looking.
    while (m_freeSlots.size() < boxes)
    {
        if (BatchUses(batch, m_entries.back().boxes))
        {
            return Core::Unexpected(std::format(
                "la caché tiene sus {} slots en textos de este lote, que todavía no se dibujó", m_slotCount));
        }
        EvictOldest();
    }

    Entry entry{key.font, key.colors, std::string(key.text), key.skipped, key.box, key.columns, extent, {}};
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
        const int drawnColumn = key.skipped + firstColumn;
        Data::Textures::Texture texture{.width = TextBoxWidth,
                                        .height = TextBoxHeight,
                                        .textureWidth = TextBoxWidth,
                                        .textureHeight = TextBoxHeight,
                                        .format = Data::Textures::PixelFormat::Rgba,
                                        .pixels = std::vector<std::uint8_t>(TextBoxBytes)};
        if (boxed)
            PaintBoxedText(*m_rasterizer, style, key.text, key.box, TextBoxPixels(texture.pixels.data(), TextBoxBytes));
        else
            PaintTextBox(*m_rasterizer, style, key.text, drawnColumn,
                         std::min(TextBoxWidth, extent.width - firstColumn), rows,
                         TextBoxPixels(texture.pixels.data(), TextBoxBytes));
        // Like 0x63bd00 uploads FontInput.tga in slot 0: GL_NEAREST and GL_CLAMP.
        textures.push_back(Data::Textures::LoadedTexture{
            slot.value, Data::Textures::Sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp},
            std::move(texture)});
    }

    if (const Core::Expected<void, std::string> loaded = m_store->Load(textures); !loaded.HasValue())
    {
        // The ones it did upload stay in the table: release them, and their slots go back to the stack.
        for (auto slot = entry.boxes.rbegin(); slot != entry.boxes.rend(); ++slot)
        {
            m_store->Release(*slot);
            m_freeSlots.push_back(*slot);
        }
        return Core::Unexpected(std::format("no se subió el texto a la caché: {}", loaded.Error()));
    }

    m_entries.push_front(std::move(entry));
    const Entry& front = m_entries.front();
    m_index.emplace(Key{front.font, front.colors, front.text, front.skipped, front.box, front.columns},
                    m_entries.begin());
    return m_entries.begin();
}

void TextCache::EvictOldest()
{
    if (m_entries.empty())
    {
        return;
    }
    const Entry& oldest = m_entries.back();
    m_index.erase(Key{oldest.font, oldest.colors, oldest.text, oldest.skipped, oldest.box, oldest.columns});
    // Reversed, so the stack gives them back in their order.
    for (auto slot = oldest.boxes.rbegin(); slot != oldest.boxes.rend(); ++slot)
    {
        m_store->Release(*slot);
        m_freeSlots.push_back(*slot);
    }
    m_entries.pop_back();
}
} // namespace Client::Render::Text
