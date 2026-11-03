#include "support/TinyTrueTypeFont.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <utility>

// Las tablas son las de la especificación de OpenType (las de TrueType), con los campos en su
// orden y en big endian; cada una lleva su nombre en un comentario donde el número no se explica.
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

using Point = std::pair<std::int16_t, std::int16_t>;
// El .notdef, un cuadrado en el sentido de las agujas del reloj (el de afuera en TrueType), y la
// raya sin área, con los cuatro puntos en y = 0.
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

    // Las tablas y los glifos empiezan en un múltiplo de 4.
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
    head.U32(0);          // checkSumAdjustment: FreeType no lo mira
    head.U32(0x5f0f3cf5); // magicNumber
    head.U16(0x000b);     // la línea de base en y = 0, el lsb en x = 0 y el ppem entero
    head.U16(UnitsPerEm);
    for (int i = 0; i < 4; ++i)
    {
        head.U32(0); // created y modified
    }
    head.I16(InkLeft); // la caja de todos los glifos: xMin, yMin, xMax e yMax
    head.I16(0);
    head.I16(InkRight);
    head.I16(InkTop);
    head.U16(0); // macStyle
    head.U16(8); // lowestRecPPEM
    head.I16(2); // fontDirectionHint
    head.I16(0); // indexToLocFormat: la loca de uint16, la mitad de cada offset
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
        hhea.I16(0); // caretSlopeRun, caretOffset y cuatro reservados
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
        maxp.U16(0); // de maxTwilightPoints a maxComponentDepth: sin hinting ni compuestos
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

// Un cmap de Unicode (plataforma 3, codificación 1) con un subtable de formato 4: el segmento de
// FlatGlyphLetter, que va al glifo 1, y el 0xffff que pide el formato.
std::vector<std::uint8_t> Cmap()
{
    constexpr auto Letter = static_cast<std::uint16_t>(FlatGlyphLetter);
    BigEndian cmap;
    cmap.U16(0);  // version
    cmap.U16(1);  // numTables
    cmap.U16(3);  // platformID
    cmap.U16(1);  // encodingID
    cmap.U32(12); // offset: el subtable va después de este encabezado
    cmap.U16(4);  // format
    cmap.U16(32); // length
    cmap.U16(0);  // language
    cmap.U16(4);  // segCountX2: dos segmentos
    cmap.U16(4);  // searchRange
    cmap.U16(1);  // entrySelector
    cmap.U16(0);  // rangeShift
    cmap.U16(Letter);
    cmap.U16(0xffff); // endCode
    cmap.U16(0);      // reservedPad
    cmap.U16(Letter);
    cmap.U16(0xffff); // startCode
    cmap.U16(static_cast<std::uint16_t>(FlatGlyph - Letter));
    cmap.U16(1); // idDelta: la letra al glifo 1, y 0xffff al 0
    cmap.U16(0);
    cmap.U16(0); // idRangeOffset
    return cmap.Take();
}

// Un glifo simple de un contorno: la caja, el último punto, sin instrucciones, y cada punto sobre
// la curva con sus deltas de 16 bits.
void AppendGlyph(BigEndian& glyf, const std::array<Point, 4>& points)
{
    std::int16_t xMin = points[0].first;
    std::int16_t xMax = points[0].first;
    std::int16_t yMin = points[0].second;
    std::int16_t yMax = points[0].second;
    for (const Point& point : points)
    {
        xMin = std::min(xMin, point.first);
        xMax = std::max(xMax, point.first);
        yMin = std::min(yMin, point.second);
        yMax = std::max(yMax, point.second);
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
        glyf.U8(0x01); // ON_CURVE_POINT, x e y de 16 bits
    }
    Point previous{0, 0};
    for (const Point& point : points)
    {
        glyf.I16(static_cast<std::int16_t>(point.first - previous.first));
        previous.first = point.first;
    }
    for (const Point& point : points)
    {
        glyf.I16(static_cast<std::int16_t>(point.second - previous.second));
        previous.second = point.second;
    }
    glyf.PadTo4();
}

std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> GlyfAndLoca()
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

std::uint32_t Tag(std::string_view name)
{
    std::uint32_t tag = 0;
    for (const char letter : name)
    {
        tag = (tag << 8) | static_cast<std::uint8_t>(letter);
    }
    return tag;
}

// La suma de la tabla en uint32 de big endian, con ceros hasta el múltiplo de 4.
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

std::vector<std::uint8_t> TinyTrueTypeFont()
{
    // searchRange es 16 por la potencia de 2 más grande que no pasa de TableCount (4), y
    // entrySelector, su logaritmo.
    constexpr std::uint16_t TableCount = 7;
    constexpr std::uint16_t SearchRange = 16 * 4;
    constexpr std::uint16_t EntrySelector = 2;
    auto [glyf, loca] = GlyfAndLoca();
    // En el orden de sus etiquetas, como pide el directorio.
    const std::array<std::pair<std::string_view, std::vector<std::uint8_t>>, TableCount> tables = {{
        {"cmap", Cmap()},
        {"glyf", std::move(glyf)},
        {"head", Head()},
        {"hhea", Hhea()},
        {"hmtx", Hmtx()},
        {"loca", std::move(loca)},
        {"maxp", Maxp()},
    }};

    BigEndian font;
    font.U32(Version1);
    font.U16(TableCount);
    font.U16(SearchRange);
    font.U16(EntrySelector);
    font.U16(static_cast<std::uint16_t>(TableCount * 16 - SearchRange));
    std::size_t offset = 12 + 16 * tables.size();
    for (const auto& [name, bytes] : tables)
    {
        font.U32(Tag(name));
        font.U32(Checksum(bytes));
        font.U32(static_cast<std::uint32_t>(offset));
        font.U32(static_cast<std::uint32_t>(bytes.size()));
        offset += (bytes.size() + 3) / 4 * 4;
    }
    for (const auto& table : tables)
    {
        font.Append(table.second);
        font.PadTo4();
    }
    return font.Take();
}
} // namespace Client::Tests
