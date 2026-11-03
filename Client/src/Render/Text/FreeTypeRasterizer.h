#pragma once

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// El texto con FreeType (pregunta 7 del plan de la CL5): lo más cerca de las fuentes de GDI sin
// suavizado (NONANTIALIASED_QUALITY) que hay fuera de Windows. Cada letra se carga con el hinting
// de TrueType para un bit (FT_LOAD_TARGET_MONO) y se rasteriza en un bit (FT_RENDER_MODE_MONO),
// con el intérprete v35, el del rasterizador de Windows 98 (no el v40 que FreeType usa por
// defecto, que en mono deja todo al programa de la fuente y es otro). Píxel a píxel no es GDI: el
// alto de la celda, el peso y que no haya suavizado sí (Q-C76).
//
// - Las cuatro fuentes de FontSet.h, con el alto de FontHeightFor para el ancho de la pantalla: el
//   alto de la celda (ascenso más descenso), que es lo que pide CreateFontA con un alto positivo.
// - La negrita sale de `bold`, si se da; si no, de la normal ensanchada un píxel
//   (FT_Outline_EmboldenXY) con un píxel más de avance por letra, como la negrita que GDI simula
//   cuando no hay una cara negrita (*inferido*).
// - Cada byte del texto es el carácter Unicode de ese número (Latin-1): Text.bmd es de un byte por
//   letra (Q-C67). Sin kerning, como GetTextExtentPointA.
// - Solo el contorno: no usa los bitmaps que traiga la fuente (FT_LOAD_NO_BITMAP).
//
// FreeType es de la FTL (el crédito va en el README del cliente). Sus headers no salen de acá.
namespace Client::Render::Text
{
class FreeTypeRasterizer final : public IGlyphRasterizer
{
public:
    // Abre las fuentes para una pantalla de `screenWidth` de ancho. Error con la ruta si un archivo
    // no se puede leer o FreeType no lo abre, o si una fuente no da letras de un bit.
    [[nodiscard]] static Core::Expected<std::unique_ptr<FreeTypeRasterizer>, std::string>
    Open(const std::filesystem::path& regular, const std::optional<std::filesystem::path>& bold, int screenWidth);

    ~FreeTypeRasterizer() override;
    FreeTypeRasterizer(const FreeTypeRasterizer&) = delete;
    FreeTypeRasterizer& operator=(const FreeTypeRasterizer&) = delete;
    FreeTypeRasterizer(FreeTypeRasterizer&&) = delete;
    FreeTypeRasterizer& operator=(FreeTypeRasterizer&&) = delete;

    [[nodiscard]] TextExtent Measure(FontKind font, std::string_view text) const override;
    void DrawInk(FontKind font, std::string_view text, int firstColumn, InkMask ink) const override;

    // La versión del intérprete de TrueType que quedó en FreeType: 35.
    [[nodiscard]] unsigned InterpreterVersion() const;

    // Los píxeles del em de la fuente (y_ppem) a su alto. La celda de CreateFontA es el ascenso
    // más el descenso, que en una fuente de texto pasan del em: el em queda más chico que la celda.
    [[nodiscard]] int PixelsPerEm(FontKind font) const;

private:
    struct State;
    explicit FreeTypeRasterizer(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};
} // namespace Client::Render::Text
