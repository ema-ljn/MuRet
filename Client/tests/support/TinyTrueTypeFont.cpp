#include "support/TinyTrueTypeFont.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

// The tables are those of the OpenType specification (TrueType's), with the fields in order and big-endian; each
// carries its name in a comment where the number is not explained.
namespace Client::Tests
{
namespace
{
constexpr std::uint16_t UnitsPerEm = 1000;
constexpr std::int16_t Ascender = 800;
constexpr std::int16_t Descender = -200;
constexpr std::uint16_t Advance = 600;
constexpr std::int16_t InkLeft = 100;
constexpr std::int16_t InkRight = 500;
constexpr std::int16_t InkTop = 700;
constexpr std::uint16_t GlyphCount = 2;
constexpr std::uint16_t FlatGlyph = 1;
constexpr std::uint32_t Version1 = 0x00010000;

// An aggregate and not a std::pair: pair's constructor would convert each int literal to std::int16_t inside
// <utility>, and MSVC warns (C4242) with /W4 /WX.
struct Point
{
    std::int16_t x = 0;
    std::int16_t y = 0;
};
// The .notdef, a clockwise square (the outer one in TrueType), and the line without area, with its four points at
// y = 0.
constexpr std::array<Point, 4> Square = {Point{InkLeft, 0}, Point{InkLeft, InkTop}, Point{InkRight, InkTop},
                                         Point{InkRight, 0}};
constexpr std::array<Point, 4> Flat = {Point{InkLeft, 0}, Point{300, 0}, Point{InkRight, 0}, Point{300, 0}};

class BigEndian
{
public:
    void U8(std::uint8_t value)
    {
        m_bytes.push_back(value);
    }

    void U16(std::uint16_t value)
    {
        U8(static_cast<std::uint8_t>(value >> 8));
        U8(static_cast<std::uint8_t>(value & 0xff));
    }

    void I16(std::int16_t value)
    {
        U16(static_cast<std::uint16_t>(value));
    }

    void U32(std::uint32_t value)
    {
        U16(static_cast<std::uint16_t>(value >> 16));
        U16(static_cast<std::uint16_t>(value & 0xffff));
    }

    void Append(const std::vector<std::uint8_t>& bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    // Tables and glyphs start at a multiple of 4.
    void PadTo4()
    {
        while (m_bytes.size() % 4 != 0)
        {
            U8(0);
        }
    }

    [[nodiscard]] std::size_t Size() const
    {
        return m_bytes.size();
    }

    [[nodiscard]] std::vector<std::uint8_t> Take()
    {
        return std::move(m_bytes);
    }

private:
    std::vector<std::uint8_t> m_bytes;
};

std::vector<std::uint8_t> Head()
{
    BigEndian head;
    head.U32(Version1);
    head.U32(Version1);   // fontRevision
    head.U32(0);          // checkSumAdjustment: FreeType does not check it
    head.U32(0x5f0f3cf5); // magicNumber
    head.U16(0x000b);     // the baseline at y = 0, the lsb at x = 0 and the ppem an integer
    head.U16(UnitsPerEm);
    for (int i = 0; i < 4; ++i)
    {
        head.U32(0); // created and modified
    }
    head.I16(InkLeft); // the box of all glyphs: xMin, yMin, xMax and yMax
    head.I16(0);
    head.I16(InkRight);
    head.I16(InkTop);
    head.U16(0); // macStyle
    head.U16(8); // lowestRecPPEM
    head.I16(2); // fontDirectionHint
    head.I16(0); // indexToLocFormat: uint16 loca, half of each offset
    head.I16(0); // glyphDataFormat
    return head.Take();
}

std::vector<std::uint8_t> Hhea()
{
    BigEndian hhea;
    hhea.U32(Version1);
    hhea.I16(Ascender);
    hhea.I16(Descender);
    hhea.I16(0); // lineGap
    hhea.U16(Advance);
    hhea.I16(InkLeft);                                       // minLeftSideBearing
    hhea.I16(static_cast<std::int16_t>(Advance - InkRight)); // minRightSideBearing
    hhea.I16(InkRight);                                      // xMaxExtent
    hhea.I16(1);                                             // caretSlopeRise
    for (int i = 0; i < 6; ++i)
    {
        hhea.I16(0); // caretSlopeRun, caretOffset and four reserved
    }
    hhea.I16(0); // metricDataFormat
    hhea.U16(GlyphCount);
    return hhea.Take();
}

std::vector<std::uint8_t> Maxp()
{
    BigEndian maxp;
    maxp.U32(Version1);
    maxp.U16(GlyphCount);
    maxp.U16(static_cast<std::uint16_t>(Square.size())); // maxPoints
    maxp.U16(1);                                         // maxContours
    maxp.U16(0);                                         // maxCompositePoints
    maxp.U16(0);                                         // maxCompositeContours
    maxp.U16(2);                                         // maxZones
    for (int i = 0; i < 8; ++i)
    {
        maxp.U16(0); // maxTwilightPoints to maxComponentDepth: no hinting or composites
    }
    return maxp.Take();
}

std::vector<std::uint8_t> Hmtx()
{
    BigEndian hmtx;
    for (std::uint16_t glyph = 0; glyph < GlyphCount; ++glyph)
    {
        hmtx.U16(Advance);
        hmtx.I16(InkLeft);
    }
    return hmtx.Take();
}

// A Unicode cmap with format 4 segments for FlatGlyphLetter, the extra codepoint and 0xffff.
std::vector<std::uint8_t> Cmap(std::uint16_t extraFlatCodePoint)
{
    constexpr std::uint16_t Letter = static_cast<std::uint16_t>(FlatGlyphLetter);
    std::array<std::uint16_t, 2> mapped = {Letter, extraFlatCodePoint};
    const std::uint16_t mappedCount = extraFlatCodePoint == Letter ? 1 : 2;
    if (mapped[1] < mapped[0])
    {
        std::swap(mapped[0], mapped[1]);
    }
    const std::uint16_t segmentCount = static_cast<std::uint16_t>(mappedCount + 1);
    constexpr std::uint16_t SearchRange = 4;
    constexpr std::uint16_t EntrySelector = 1;
    const std::uint16_t rangeShift = static_cast<std::uint16_t>(segmentCount * 2 - SearchRange);
    const std::uint16_t length = static_cast<std::uint16_t>(16 + 8 * segmentCount);
    BigEndian cmap;
    cmap.U16(0);  // version
    cmap.U16(1);  // numTables
    cmap.U16(3);  // platformID
    cmap.U16(1);  // encodingID
    cmap.U32(12); // offset: the subtable follows this header
    cmap.U16(4);  // format
    cmap.U16(length);
    cmap.U16(0); // language
    cmap.U16(static_cast<std::uint16_t>(segmentCount * 2));
    cmap.U16(SearchRange);
    cmap.U16(EntrySelector);
    cmap.U16(rangeShift);
    for (std::uint16_t index = 0; index < mappedCount; ++index)
    {
        cmap.U16(mapped[index]);
    }
    cmap.U16(0xffff); // endCode
    cmap.U16(0);      // reservedPad
    for (std::uint16_t index = 0; index < mappedCount; ++index)
    {
        cmap.U16(mapped[index]);
    }
    cmap.U16(0xffff); // startCode
    for (std::uint16_t index = 0; index < mappedCount; ++index)
    {
        cmap.U16(static_cast<std::uint16_t>(FlatGlyph - mapped[index]));
    }
    cmap.U16(1); // idDelta: 0xffff to glyph 0
    for (std::uint16_t index = 0; index < segmentCount; ++index)
    {
        cmap.U16(0); // idRangeOffset
    }
    return cmap.Take();
}

// A simple one-contour glyph: the box, the last point, no instructions, and each point on the curve with its 16-bit
// deltas.
void AppendGlyph(BigEndian& glyf, const std::array<Point, 4>& points)
{
    std::int16_t xMin = points[0].x;
    std::int16_t xMax = points[0].x;
    std::int16_t yMin = points[0].y;
    std::int16_t yMax = points[0].y;
    for (const Point& point : points)
    {
        xMin = std::min(xMin, point.x);
        xMax = std::max(xMax, point.x);
        yMin = std::min(yMin, point.y);
        yMax = std::max(yMax, point.y);
    }
    glyf.I16(1); // numberOfContours
    glyf.I16(xMin);
    glyf.I16(yMin);
    glyf.I16(xMax);
    glyf.I16(yMax);
    glyf.U16(static_cast<std::uint16_t>(points.size() - 1)); // endPtsOfContours
    glyf.U16(0);                                             // instructionLength
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        glyf.U8(0x01); // ON_CURVE_POINT, with x and y as 16-bit values
    }
    Point previous;
    for (const Point& point : points)
    {
        glyf.I16(static_cast<std::int16_t>(point.x - previous.x));
        previous.x = point.x;
    }
    for (const Point& point : points)
    {
        glyf.I16(static_cast<std::int16_t>(point.y - previous.y));
        previous.y = point.y;
    }
    glyf.PadTo4();
}

struct GlyphTables
{
    std::vector<std::uint8_t> glyf;
    std::vector<std::uint8_t> loca;
};

GlyphTables GlyfAndLoca()
{
    BigEndian glyf;
    BigEndian loca;
    for (const std::array<Point, 4>& points : {Square, Flat})
    {
        loca.U16(static_cast<std::uint16_t>(glyf.Size() / 2));
        AppendGlyph(glyf, points);
    }
    loca.U16(static_cast<std::uint16_t>(glyf.Size() / 2));
    return {glyf.Take(), loca.Take()};
}

struct Table
{
    std::string_view tag;
    std::vector<std::uint8_t> bytes;
};

std::uint32_t Tag(std::string_view name)
{
    std::uint32_t tag = 0;
    for (const char letter : name)
    {
        tag = (tag << 8) | static_cast<std::uint8_t>(letter);
    }
    return tag;
}

// The table's sum in big-endian uint32, with zeros up to the multiple of 4.
std::uint32_t Checksum(const std::vector<std::uint8_t>& table)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < table.size(); i += 4)
    {
        std::uint32_t word = 0;
        for (std::size_t j = 0; j < 4; ++j)
        {
            word = (word << 8) | (i + j < table.size() ? table[i + j] : 0u);
        }
        sum += word;
    }
    return sum;
}
} // namespace

std::vector<std::uint8_t> TinyTrueTypeFont(std::uint16_t extraFlatCodePoint)
{
    // searchRange is 16 times the largest power of 2 not above TableCount (4), and entrySelector, its logarithm.
    constexpr std::uint16_t TableCount = 7;
    constexpr std::uint16_t SearchRange = 16 * 4;
    constexpr std::uint16_t EntrySelector = 2;
    GlyphTables glyphs = GlyfAndLoca();
    // In their tags' order, as the directory requires.
    const std::array<Table, TableCount> tables = {{
        {"cmap", Cmap(extraFlatCodePoint)},
        {"glyf", std::move(glyphs.glyf)},
        {"head", Head()},
        {"hhea", Hhea()},
        {"hmtx", Hmtx()},
        {"loca", std::move(glyphs.loca)},
        {"maxp", Maxp()},
    }};

    BigEndian font;
    font.U32(Version1);
    font.U16(TableCount);
    font.U16(SearchRange);
    font.U16(EntrySelector);
    font.U16(static_cast<std::uint16_t>(TableCount * 16 - SearchRange));
    std::size_t offset = 12 + 16 * tables.size();
    for (const Table& table : tables)
    {
        font.U32(Tag(table.tag));
        font.U32(Checksum(table.bytes));
        font.U32(static_cast<std::uint32_t>(offset));
        font.U32(static_cast<std::uint32_t>(table.bytes.size()));
        offset += (table.bytes.size() + 3) / 4 * 4;
    }
    for (const Table& table : tables)
    {
        font.Append(table.bytes);
        font.PadTo4();
    }
    return font.Take();
}
} // namespace Client::Tests
