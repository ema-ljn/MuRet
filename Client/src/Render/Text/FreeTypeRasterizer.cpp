#include "Render/Text/FreeTypeRasterizer.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

// Define las macros FT_*_H de abajo: include-cleaner no ve ese uso.
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

// El tamaño más grande de fuente que se lee. No sale del original: una TTF de Latin-1 pesa cientos
// de KiB y una colección CJK unas decenas de MiB; con el tope, un dispositivo no se lee entero.
constexpr std::uintmax_t MaxFontFileBytes = 64 * 1024 * 1024;

// El hinting para un bit, sin los bitmaps de la fuente (FreeTypeRasterizer.h).
constexpr FT_Int32 LoadFlags = static_cast<FT_Int32>(FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP);

// Un píxel en 26.6, el ensanche de la negrita simulada.
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
    // El puerto de vcpkg no trae los textos de los errores (la feature error-strings): el número.
    return std::format("error {:#04x} de FreeType", static_cast<unsigned>(error));
}

// Los bytes del archivo, que FreeType lee desde la memoria: así una ruta con letras que no son
// ASCII abre igual en Windows, donde FT_New_Face pide la ruta en la página de códigos ANSI.
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

// La celda de GDI con un alto positivo: el ascenso y el descenso de Windows de la tabla OS/2
// (usWinAscent y usWinDescent), que en las fuentes de los runners son los mismos de la hhea
// (*inferido*: lo dice la documentación de LOGFONT y de la OS/2). Sin OS/2, los de la hhea.
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

// Una de las cuatro fuentes, abierta.
struct Font
{
    FaceHandle face;
    // El alto de la celda y dónde cae la línea de base desde arriba, en píxeles.
    int height = 0;
    int baseline = 0;
    bool syntheticBold = false;
    // El avance de cada byte, en píxeles, con el hinting: el que suma GetTextExtentPointA.
    std::array<int, 256> advances{};
};
} // namespace

struct FreeTypeRasterizer::State
{
    // En este orden: los bytes viven más que las caras, y las caras se cierran antes que la
    // biblioteca (los miembros se destruyen al revés).
    std::vector<FT_Byte> regularBytes;
    std::vector<FT_Byte> boldBytes;
    LibraryHandle library;
    std::array<Font, 5> fonts;
};

namespace
{
// Carga el glifo del byte con la fuente: el de Latin-1 de ese número, con la negrita simulada si
// corresponde. Devuelve el error de FreeType, o 0.
FT_Error LoadGlyph(const Font& font, unsigned char byte)
{
    const FT_Error error = FT_Load_Char(font.face.get(), static_cast<FT_ULong>(byte), LoadFlags);
    if (error != 0)
    {
        return error;
    }
    FT_GlyphSlot slot = font.face->glyph;
    if (font.syntheticBold && slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        // Ensancha el contorno un píxel hacia la derecha (el borde izquierdo queda donde estaba).
        // Un contorno sin área (el guion bajo de algunas fuentes, que el hinting deja en una línea)
        // no tiene sentido de giro, y FT_Outline_EmboldenXY lo rechaza: no hay qué ensanchar, y la
        // letra queda como está, con su píxel de avance.
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

// La cara de una fuente al alto de su celda, con los avances de los 256 bytes. Error si un glifo no
// carga o si la fuente no da letras de un bit.
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
    // FT_SIZE_REQUEST_TYPE_SCALES: unidades de la fuente a píxeles en 26.6, en 16.16. La celda
    // (ascenso más descenso) mide `height` píxeles, como en CreateFontA con un alto positivo.
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

    // Un glifo de prueba tiene que salir en un bit.
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

// La primera cara del archivo (la 0, también en una colección TTC). Los bytes tienen que vivir
// lo que ella (FT_New_Memory_Face no los copia).
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

// Las filas de un bitmap de arriba hacia abajo: con pitch negativo, buffer es la fila de abajo
// (ftimage.h) y para bajar una fila se suma pitch igual.
const unsigned char* TopRow(const FT_Bitmap& bitmap)
{
    if (bitmap.pitch >= 0 || bitmap.rows == 0)
    {
        return bitmap.buffer;
    }
    return bitmap.buffer + static_cast<std::ptrdiff_t>(bitmap.rows - 1) * -static_cast<std::ptrdiff_t>(bitmap.pitch);
}

// Copia un glifo de un bit a la tinta, con su esquina en (left, top); lo que cae afuera se pierde,
// como TextOutA en el borde del DIB.
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
            // FT_PIXEL_MODE_MONO: un bit por píxel, el de más a la izquierda en el bit alto.
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

    // El intérprete v35 (FreeTypeRasterizer.h), antes de abrir las caras.
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
        // Un glifo que no carga o no dibuja (Open ya los cargó a todos) queda sin tinta, y el texto
        // sigue con su avance, como lo mide Measure.
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
