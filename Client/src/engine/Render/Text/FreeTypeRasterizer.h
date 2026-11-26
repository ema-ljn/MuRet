#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// FreeType text, the closest thing outside Windows to GDI's fonts without antialiasing (NONANTIALIASED_QUALITY): each
// letter is loaded with TrueType one-bit hinting (FT_LOAD_TARGET_MONO) and rendered in one bit (FT_RENDER_MODE_MONO)
// with the v35 interpreter, Windows 98's rasterizer's (not FreeType's default v40, which in mono leaves everything to
// the font program). Not GDI pixel for pixel, but the cell height, the weight and no antialiasing match (Q-C76).
//
// - Bold comes from `bold` if given; otherwise from the regular one widened one pixel (FT_Outline_EmboldenXY) with one
//   more advance pixel per letter, like the bold GDI simulates when there is no bold face (*inferred*).
// - Each byte is decoded as Windows-1252 before loading the glyph: Text.bmd is one byte per letter (Q-C67;
//   12-codificacion.md). No kerning, like GetTextExtentPointA.
// - Outlines only: the font's own bitmaps are not used (FT_LOAD_NO_BITMAP).
//
// FreeType is under the FTL (credited in the client README). Its headers do not leave here.
namespace Client::Render::Text
{
class ENGINE_API FreeTypeRasterizer final : public IGlyphRasterizer
{
public:
    // Error with the path if a file cannot be read or FreeType does not open it, or if a font gives no one-bit letters.
    [[nodiscard]] static Core::Expected<std::unique_ptr<FreeTypeRasterizer>, std::string>
    Open(const std::filesystem::path& regular, const std::optional<std::filesystem::path>& bold, int screenWidth);

    ~FreeTypeRasterizer() override;
    FreeTypeRasterizer(const FreeTypeRasterizer&) = delete;
    FreeTypeRasterizer& operator=(const FreeTypeRasterizer&) = delete;
    FreeTypeRasterizer(FreeTypeRasterizer&&) = delete;
    FreeTypeRasterizer& operator=(FreeTypeRasterizer&&) = delete;

    [[nodiscard]] TextExtent Measure(FontKind font, std::string_view text) const override;
    void DrawInk(FontKind font, std::string_view text, int firstColumn, InkMask ink) const override;

    // The TrueType interpreter version FreeType ended up with: 35.
    [[nodiscard]] unsigned InterpreterVersion() const;

    // The font's em pixels (y_ppem) at its height. CreateFontA's cell is the ascent plus the descent, which in a text
    // font exceed the em: the em is smaller than the cell.
    [[nodiscard]] int PixelsPerEm(FontKind font) const;

private:
    struct State;
    explicit FreeTypeRasterizer(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};
} // namespace Client::Render::Text
