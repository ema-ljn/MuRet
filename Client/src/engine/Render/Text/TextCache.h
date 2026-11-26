#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Render/ITextureStore.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Rasterized texts. The original rasterizes each text with TextOutA (0x467060), converts it to the 256 × 32 box
// (0x465a00) and uploads it to slot 0 (0x554000) before each quad, every frame (06 § Texto); MuRet does it once per
// text, font and colors, and keeps the boxes in text slots. A box does not depend on where the text goes: TextPieces
// puts the edge cut and the position in the quad.
//
// Bounded by its slots: when a new text does not fit, the least recently drawn are evicted, except one already in the
// batch being drawn: Gl::GlSpriteRenderer looks each quad's slot up only when drawing, so a box released or overwritten
// earlier would change what is shown. Not 0x466000's cache (a hash tree with 0x466560's slots, which does not run in
// the measured scenes). Lives as long as the table it uploads to; not thread-safe.
namespace Client::Render::Text
{
class ENGINE_API TextCache
{
public:
    // The rasterizer and the store must outlive the cache.
    TextCache(const IGlyphRasterizer& rasterizer, ITextureStore& store, TextureSlot firstSlot = FirstTextTextureSlot,
              std::size_t slotCount = TextTextureSlotCount);
    // Releases the slots it uses.
    ~TextCache();

    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;
    TextCache(TextCache&&) = delete;
    TextCache& operator=(TextCache&&) = delete;

    // 0x554180(x, y, text, 0, 0, 0) with TextOut other than 1, the login's: the text at (x, y) of 640 × 480 as
    // TextPieces quads in `batch` (with its screen, color and blend). A missing text is rasterized and uploaded; an
    // empty one draws nothing, like 0x467060. Error if the store does not upload a box, the text needs more boxes than
    // the cache's slots, or making room would evict a text already in `batch`. One batch per frame: another batch's
    // undrawn text may leave.
    [[nodiscard]] Core::Expected<void, std::string> Draw(SpriteBatch& batch, int x, int y, std::string_view text,
                                                         const TextStyle& style);

    // 0x5542f0(x, y, text, 0, 6, 0), 0x467060's mode 6 (the Slide.bmd banner): with negative x, TextOutA at DIB (x, 0),
    // so the text's columns from −x show (640 × 480 ones taken as pixels, Q-C519), cx + x wide, from x 0; with none,
    // nothing. Each cut is another cache entry, like the original, which rasterizes it every frame. With x ≥ 0, Draw.
    [[nodiscard]] Core::Expected<void, std::string> DrawClippedAtLeft(SpriteBatch& batch, int x, int y,
                                                                      std::string_view text, const TextStyle& style);

    // 0x467060 with a width (modes 1 and 2 of 0x554180): a box of `columns` columns at (x, y) of 640 × 480, with the
    // whole background and, at its column 0, text column `firstColumn`: with more, the left is cut (mode 1 with a width
    // under cx); below 0, the box starts with background (mode 2, centered). Past the text is background, even if
    // empty. Beyond 256 columns, the 256 boxes and the right-edge cut are TextPieces' with the box width. Each box is
    // another cache entry. No columns, nothing. Error below 0 columns (a negative box in mode 2: 0x4671cf's rep stos
    // would clear outside the DIB, Q-C711), plus Draw's. CenterInBox gives mode 2's columns.
    [[nodiscard]] Core::Expected<void, std::string> DrawColumns(SpriteBatch& batch, int x, int y, std::string_view text,
                                                                const TextStyle& style, int firstColumn, int columns);

    // Like DrawColumns, with a pixel origin for callers that already computed the EDIT's position.
    [[nodiscard]] Core::Expected<void, std::string> DrawColumnsAtPixels(SpriteBatch& batch, int pixelX, int pixelY,
                                                                        std::string_view text, const TextStyle& style,
                                                                        int firstColumn, int columns);

    // 0x553e50 and 0x554000 for a bubble line (0x555b00): `box`'s box with the text at (0, 0), even if empty
    // (background only), at screen pixel (x, y), shifted as BoxedQuad does. Each box is another one-slot cache entry.
    // Errors as Draw's.
    [[nodiscard]] Core::Expected<void, std::string> DrawBoxed(SpriteBatch& batch, int x, int y, std::string_view text,
                                                              const TextStyle& style, const BoxShape& box, bool clampY);

    // GetTextExtentPointA, to place the text before drawing it (RightAlignedAt).
    [[nodiscard]] TextExtent Measure(FontKind font, std::string_view text) const;

    // For another screen width (fonts change height, FontSet.h): empties the cache and releases its slots. Between
    // frames, like Clear.
    void ChangeRasterizer(const IGlyphRasterizer& rasterizer);

    // Empties the cache and releases its slots, even those in a batch: between frames.
    void Clear();

    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] std::size_t UsedSlots() const;

private:
    // The font, the colors, the text (in the index it points to its entry's), and the columns mode 6 leaves out on the
    // left (negative: those mode 2 leaves before the text).
    struct Key
    {
        FontKind font = FontKind::Normal;
        TextColors colors;
        std::string_view text;
        int skipped = 0;
        // A bubble's box; with width 0, 0x467060's.
        BoxShape box{};
        // DrawColumns' box columns; with 0, the text's from `skipped`.
        int columns = 0;
    };
    struct KeyLess
    {
        [[nodiscard]] bool operator()(const Key& a, const Key& b) const;
    };
    struct Entry
    {
        FontKind font = FontKind::Normal;
        TextColors colors;
        std::string text;
        int skipped = 0;
        BoxShape box{};
        int columns = 0;
        // The visible width: the text's minus `skipped`, DrawColumns' box's or the bubble box's.
        TextExtent extent;
        // One per 256 text columns, in order.
        std::vector<TextureSlot> boxes;
    };
    using Entries = std::list<Entry>;

    // `key`'s entry, rasterized and uploaded if missing, moved to the front. Never evicts an entry already in `batch`
    // to make room.
    [[nodiscard]] Core::Expected<Entries::iterator, std::string> Find(const Key& key, const SpriteBatch& batch);
    // `entry`'s quads at (x, y), like TextPieces.
    static void DrawPieces(SpriteBatch& batch, const Entry& entry, int x, int y);
    static void DrawPiecesAtPixels(SpriteBatch& batch, const Entry& entry, int pixelX, int pixelY);
    // Evicts the back of the list, the least recently drawn.
    void EvictOldest();

    const IGlyphRasterizer* m_rasterizer;
    ITextureStore* m_store;
    std::size_t m_slotCount;
    // Most recent first. List nodes do not move: the index points to their texts.
    Entries m_entries;
    std::map<Key, Entries::iterator, KeyLess> m_index;
    std::vector<TextureSlot> m_freeSlots;
};
} // namespace Client::Render::Text
