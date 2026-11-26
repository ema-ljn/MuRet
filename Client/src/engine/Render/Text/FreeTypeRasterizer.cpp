#include "Render/Text/FreeTypeRasterizer.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Core/Windows1252.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

// Defines the FT_*_H macros below: include-cleaner does not see that use.
#include <ft2build.h> // IWYU pragma: keep

#include FT_FREETYPE_H
#include FT_DRIVER_H
#include FT_IMAGE_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TYPES_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Render::Text
{
namespace
{
using Core::Unexpected;

// The largest font file read. Not from the original: a Latin-1 TTF weighs hundreds of KiB and a CJK collection a few
// tens of MiB; the cap keeps a device from being read whole.
constexpr std::uintmax_t MaxFontFileBytes = 64 * 1024 * 1024;

// One-bit hinting, without the font's bitmaps (FreeTypeRasterizer.h).
constexpr FT_Int32 LoadFlags = static_cast<FT_Int32>(FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP);

// One pixel in 26.6, the synthetic bold's widening.
constexpr FT_Pos OnePixel = 64;

struct LibraryDeleter
{
    void operator()(FT_Library library) const
    {
        FT_Done_FreeType(library);
    }
};

struct FaceDeleter
{
    void operator()(FT_Face face) const
    {
        FT_Done_Face(face);
    }
};

using LibraryHandle = std::unique_ptr<FT_LibraryRec_, LibraryDeleter>;
using FaceHandle = std::unique_ptr<FT_FaceRec_, FaceDeleter>;

std::string FreeTypeError(FT_Error error)
{
    // vcpkg's port lacks the error texts (the error-strings feature): the number.
    return std::format("error {:#04x} de FreeType", static_cast<unsigned>(error));
}

// The file's bytes, which FreeType reads from memory: so a path with non-ASCII letters opens on Windows too, where
// FT_New_Face wants the path in the ANSI code page.
Core::Expected<std::vector<FT_Byte>, std::string> ReadFontFile(const std::filesystem::path& path)
{
    const std::string name = Core::PathText(path);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return Unexpected(std::format("no existe la fuente {}", name));
    }
    if (error)
    {
        return Unexpected(std::format("no se pudo leer la fuente {}: {}", name, error.message()));
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return Unexpected(std::format("la fuente {} no es un archivo", name));
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return Unexpected(std::format("no se pudo leer la fuente {}: {}", name, error.message()));
    }
    if (size > MaxFontFileBytes)
    {
        return Unexpected(std::format("la fuente {} pasa de {} bytes", name, MaxFontFileBytes));
    }
    std::vector<FT_Byte> bytes(static_cast<std::size_t>(size));
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.is_open() || file.bad() || static_cast<std::uintmax_t>(file.gcount()) != size)
    {
        return Unexpected(std::format("no se pudo leer la fuente {}", name));
    }
    return bytes;
}

// The GDI cell with a positive height: the Windows ascent and descent of the OS/2 table (usWinAscent and
// usWinDescent), which in the runners' fonts match the hhea ones (*inferred*: from the LOGFONT and OS/2 docs). Without
// OS/2, the hhea ones.
struct CellMetrics
{
    FT_Long ascent = 0;
    FT_Long descent = 0;
};

CellMetrics CellOf(FT_Face face)
{
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
    if (os2 != nullptr && os2->usWinAscent + os2->usWinDescent > 0)
    {
        return {static_cast<FT_Long>(os2->usWinAscent), static_cast<FT_Long>(os2->usWinDescent)};
    }
    return {static_cast<FT_Long>(face->ascender), -static_cast<FT_Long>(face->descender)};
}

struct Font
{
    FaceHandle face;
    // In pixels; the baseline is measured from the top.
    int height = 0;
    int baseline = 0;
    bool syntheticBold = false;
    // Each byte's hinted advance in pixels: what GetTextExtentPointA adds up.
    std::array<int, 256> advances{};
};
} // namespace

struct FreeTypeRasterizer::State
{
    // In this order: the bytes outlive the faces, and the faces close before the library (members are destroyed in
    // reverse).
    std::vector<FT_Byte> regularBytes;
    std::vector<FT_Byte> boldBytes;
    LibraryHandle library;
    std::array<Font, 5> fonts;
};

namespace
{
// The glyph for the Windows-1252 byte, with the synthetic bold if needed. Returns FreeType's error, or 0.
FT_Error LoadGlyph(const Font& font, unsigned char byte)
{
    const FT_ULong codePoint = static_cast<FT_ULong>(Core::Windows1252CodePoint(byte));
    const FT_Error error = FT_Load_Char(font.face.get(), codePoint, LoadFlags);
    if (error != 0)
    {
        return error;
    }
    FT_GlyphSlot slot = font.face->glyph;
    if (font.syntheticBold && slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        // Widens the outline one pixel to the right (the left edge stays). An outline without area (some fonts'
        // underscore, which hinting flattens to a line) has no orientation and FT_Outline_EmboldenXY rejects it: the
        // letter stays as is, with its advance pixel.
        if (FT_Outline_Get_Orientation(&slot->outline) != FT_ORIENTATION_NONE)
        {
            const FT_Error embolden = FT_Outline_EmboldenXY(&slot->outline, OnePixel, 0);
            if (embolden != 0)
            {
                return embolden;
            }
        }
        slot->advance.x += OnePixel;
    }
    return 0;
}

int AdvanceInPixels(FT_GlyphSlot slot)
{
    return static_cast<int>((slot->advance.x + OnePixel / 2) / OnePixel);
}

// The font's face at its cell height, with the advances of the 256 bytes. Error if a glyph does not load or the font
// gives no one-bit letters.
Core::Expected<void, std::string> SetUp(Font& font, int height, bool syntheticBold, const std::string& name)
{
    font.height = height;
    font.syntheticBold = syntheticBold;

    FT_Error error = FT_Select_Charmap(font.face.get(), FT_ENCODING_UNICODE);
    if (error != 0)
    {
        return Unexpected(std::format("la fuente {} no tiene un mapa Unicode: {}", name, FreeTypeError(error)));
    }

    const CellMetrics cell = CellOf(font.face.get());
    const FT_Long cellUnits = cell.ascent + cell.descent;
    if (cellUnits <= 0)
    {
        return Unexpected(std::format("la fuente {} no dice el alto de su celda", name));
    }
    // FT_SIZE_REQUEST_TYPE_SCALES: font units to 26.6 pixels, in 16.16. The cell (ascent plus descent) is `height`
    // pixels, as in CreateFontA with a positive height.
    const FT_Fixed scale = FT_DivFix(static_cast<FT_Long>(height) * OnePixel, cellUnits);
    FT_Size_RequestRec request{};
    request.type = FT_SIZE_REQUEST_TYPE_SCALES;
    request.width = scale;
    request.height = scale;
    error = FT_Request_Size(font.face.get(), &request);
    if (error != 0)
    {
        return Unexpected(std::format("la fuente {} no toma el alto {}: {}", name, height, FreeTypeError(error)));
    }
    font.baseline = static_cast<int>(
        std::lround(static_cast<double>(height) * static_cast<double>(cell.ascent) / static_cast<double>(cellUnits)));

    for (std::size_t byte = 0; byte < font.advances.size(); ++byte)
    {
        error = LoadGlyph(font, static_cast<unsigned char>(byte));
        if (error != 0)
        {
            return Unexpected(
                std::format("la fuente {} no carga la letra {:#04x}: {}", name, byte, FreeTypeError(error)));
        }
        font.advances[byte] = AdvanceInPixels(font.face->glyph);
    }

    // A test glyph must come out in one bit.
    error = LoadGlyph(font, static_cast<unsigned char>('A'));
    if (error == 0)
    {
        error = FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_MONO);
    }
    if (error != 0)
    {
        return Unexpected(std::format("la fuente {} no dibuja: {}", name, FreeTypeError(error)));
    }
    if (font.face->glyph->bitmap.pixel_mode != FT_PIXEL_MODE_MONO)
    {
        return Unexpected(std::format("la fuente {} no da letras de un bit", name));
    }
    return {};
}

// The file's first face (0, also in a TTC collection). The bytes must outlive it (FT_New_Memory_Face does not copy
// them).
Core::Expected<FaceHandle, std::string> OpenFace(FT_Library library, const std::vector<FT_Byte>& bytes,
                                                 const std::string& name)
{
    FT_Face face = nullptr;
    const FT_Error error = FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &face);
    if (error != 0)
    {
        return Unexpected(std::format("FreeType no abre la fuente {}: {}", name, FreeTypeError(error)));
    }
    return FaceHandle(face);
}

// Bitmap rows top to bottom: with a negative pitch, buffer is the bottom row (ftimage.h) and adding pitch still goes
// down a row.
const unsigned char* TopRow(const FT_Bitmap& bitmap)
{
    if (bitmap.pitch >= 0 || bitmap.rows == 0)
    {
        return bitmap.buffer;
    }
    return bitmap.buffer + static_cast<std::ptrdiff_t>(bitmap.rows - 1) * -static_cast<std::ptrdiff_t>(bitmap.pitch);
}

// Copies a one-bit glyph to the ink with its corner at (left, top); what falls outside is lost, like TextOutA at the
// DIB's edge.
void CopyGlyph(const FT_Bitmap& bitmap, int left, int top, InkMask ink)
{
    const unsigned char* row = TopRow(bitmap);
    for (unsigned int y = 0; y < bitmap.rows; ++y, row += bitmap.pitch)
    {
        const int inkRow = top + static_cast<int>(y);
        if (inkRow < 0 || inkRow >= TextBoxHeight)
        {
            continue;
        }
        for (unsigned int x = 0; x < bitmap.width; ++x)
        {
            const int inkColumn = left + static_cast<int>(x);
            if (inkColumn < 0 || inkColumn >= TextBoxWidth)
            {
                continue;
            }
            // FT_PIXEL_MODE_MONO: one bit per pixel, the leftmost in the high bit.
            const bool set = (row[x / 8] & (0x80u >> (x % 8))) != 0;
            if (set)
            {
                ink[static_cast<std::size_t>(inkRow * TextBoxWidth + inkColumn)] = 0xff;
            }
        }
    }
}
} // namespace

FreeTypeRasterizer::FreeTypeRasterizer(std::unique_ptr<State> state) : m_state(std::move(state)) {}

FreeTypeRasterizer::~FreeTypeRasterizer() = default;

Core::Expected<std::unique_ptr<FreeTypeRasterizer>, std::string>
FreeTypeRasterizer::Open(const std::filesystem::path& regular, const std::optional<std::filesystem::path>& bold,
                         int screenWidth)
{
    auto state = std::make_unique<State>();
    const std::string regularName = Core::PathText(regular);
    const std::string boldName = bold ? Core::PathText(*bold) : regularName;

    auto regularBytes = ReadFontFile(regular);
    if (!regularBytes.HasValue())
    {
        return Unexpected(regularBytes.Error());
    }
    state->regularBytes = std::move(regularBytes).Value();
    if (bold)
    {
        auto boldBytes = ReadFontFile(*bold);
        if (!boldBytes.HasValue())
        {
            return Unexpected(boldBytes.Error());
        }
        state->boldBytes = std::move(boldBytes).Value();
    }

    FT_Library library = nullptr;
    FT_Error error = FT_Init_FreeType(&library);
    if (error != 0)
    {
        return Unexpected(std::format("FreeType no arranca: {}", FreeTypeError(error)));
    }
    state->library.reset(library);

    // The v35 interpreter (FreeTypeRasterizer.h), before opening the faces.
    FT_UInt interpreter = TT_INTERPRETER_VERSION_35;
    error = FT_Property_Set(library, "truetype", "interpreter-version", &interpreter);
    if (error != 0)
    {
        return Unexpected(std::format("FreeType no tiene el intérprete v35: {}", FreeTypeError(error)));
    }

    constexpr std::array Kinds = {FontKind::Normal, FontKind::Bold, FontKind::BigBold, FontKind::OtherFace12,
                                  FontKind::Credits};
    for (const FontKind kind : Kinds)
    {
        const bool fromBoldFile = bold.has_value() && IsBold(kind);
        const std::string& name = fromBoldFile ? boldName : regularName;
        auto face = OpenFace(library, fromBoldFile ? state->boldBytes : state->regularBytes, name);
        if (!face.HasValue())
        {
            return Unexpected(face.Error());
        }
        Font& font = state->fonts[static_cast<std::size_t>(kind)];
        font.face = std::move(face).Value();
        const auto ready = SetUp(font, FontHeightFor(kind, screenWidth), IsBold(kind) && !fromBoldFile, name);
        if (!ready.HasValue())
        {
            return Unexpected(ready.Error());
        }
    }
    return std::unique_ptr<FreeTypeRasterizer>(new FreeTypeRasterizer(std::move(state)));
}

TextExtent FreeTypeRasterizer::Measure(FontKind font, std::string_view text) const
{
    const Font& face = m_state->fonts[static_cast<std::size_t>(font)];
    int width = 0;
    for (const char letter : text)
    {
        width += face.advances[static_cast<unsigned char>(letter)];
    }
    return {width, face.height};
}

void FreeTypeRasterizer::DrawInk(FontKind font, std::string_view text, int firstColumn, InkMask ink) const
{
    const Font& face = m_state->fonts[static_cast<std::size_t>(font)];
    int pen = -firstColumn;
    for (const char letter : text)
    {
        const auto byte = static_cast<unsigned char>(letter);
        // A glyph that does not load or render (Open already loaded them all) gets no ink, and the text goes on with
        // its advance, as Measure measures it.
        const FT_GlyphSlot slot = face.face->glyph;
        if (LoadGlyph(face, byte) == 0 && FT_Render_Glyph(slot, FT_RENDER_MODE_MONO) == 0 &&
            slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
        {
            CopyGlyph(slot->bitmap, pen + slot->bitmap_left, face.baseline - slot->bitmap_top, ink);
        }
        pen += face.advances[byte];
    }
}

int FreeTypeRasterizer::PixelsPerEm(FontKind font) const
{
    return static_cast<int>(m_state->fonts[static_cast<std::size_t>(font)].face->size->metrics.y_ppem);
}

unsigned FreeTypeRasterizer::InterpreterVersion() const
{
    FT_UInt version = 0;
    if (FT_Property_Get(m_state->library.get(), "truetype", "interpreter-version", &version) != 0)
    {
        return 0;
    }
    return version;
}
} // namespace Client::Render::Text
