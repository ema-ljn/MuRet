#pragma once

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// El 2D de un frame como datos, sin GL (CL5.2): las primitivas del original (06 § Primitivas 2D)
// pasadas a vértices en píxeles de la pantalla, y los tramos que Gl::GlSpriteRenderer dibuja cada
// uno con un glDrawArrays. El original manda cada quad con un glBegin(GL_TRIANGLE_FAN); acá cada
// quad son los dos triángulos de su fan, (0, 1, 2) y (0, 2, 3), y los quads seguidos con la misma
// textura, la misma mezcla y el mismo alpha test van en un solo tramo. Nunca se reordena: con la
// mezcla normal, lo que va después tapa lo de antes.
//
// Guarda lo del estado de GL que cambian esas primitivas, y lo deja como lo dejaría el original:
// el color (glColor), la mezcla (0x620a00 y 0x620a90), si la textura está prendida
// (GL_TEXTURE_2D) y el alpha test. Un quad con alfa deja el color en blanco (Q-C57), el quad sin
// textura apaga la textura y el alpha test, la imagen de la UI nueva deja su color y prende o apaga
// la textura según su slot (Q-C75), y Begin no toca nada de eso: como en el original, el estado
// pasa de un frame al siguiente.
namespace Client::Render
{
enum class BlendMode : std::uint8_t
{
    // 0x620a00: (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), y prende el alpha test.
    Alpha,
    // 0x620a90: (GL_ONE, GL_ONE), y apaga el alpha test.
    Additive,
};

// Un tramo de vértices que se dibuja de una vez, con el mismo estado.
struct DrawCommand
{
    // La textura, o ninguna: el quad sin textura de 0x621aa0, una imagen sin slot, o un quad que
    // llegó con la textura apagada (Q-C75).
    std::optional<TextureSlot> texture;
    BlendMode blend = BlendMode::Alpha;
    // El glAlphaFunc(GL_GREATER, 0,25) de la cámara (06 § Cámara): con él, lo que tiene un alfa de
    // 0,25 o menos no se dibuja.
    bool alphaTest = true;
    std::size_t firstVertex = 0;
    // De a 6: dos triángulos por quad.
    std::size_t vertexCount = 0;
};

class SpriteBatch
{
public:
    // Empieza el 2D de un frame en una pantalla de `screen` píxeles, como 0x6219b0: vacía los
    // vértices y los tramos, y no toca el estado de GL que guarda.
    void Begin(App::PixelSize screen);

    // 0x620a00(1) o 0x620a90: la mezcla, y con ella la textura prendida y el alpha test prendido
    // (Alpha) o apagado (Additive). Corre aunque la mezcla sea la misma, como el original.
    void SetBlend(BlendMode blend);

    // glColor4f: el color de los vértices que siguen, recortado a [0, 1] como lo usa la función
    // fija (06 § Primitivas 2D).
    void SetColor(Color color);

    // 0x621b60 (OriginalQuad.h).
    void Quad(const OriginalQuad& quad);

    // 0x622000 (OriginalSkewedQuad.h), con el color que haya.
    void SkewedQuad(const OriginalSkewedQuad& quad);

    // 0x621aa0(x, y, w, h): el rectángulo en 640 × 480, siempre escalado, del color que haya y sin
    // textura. Antes, como 0x620910(0), apaga la textura y el alpha test, y quedan apagados
    // (Q-C75).
    void ColorQuad(float x, float y, float width, float height);

    // 0x647650 (UiImage.h): pone su color como el de GL y prende la textura si tiene slot, o la
    // apaga si no (Q-C75); la mezcla y el alpha test quedan como estaban.
    void Image(const UiImage& image);

    [[nodiscard]] App::PixelSize Screen() const;
    [[nodiscard]] std::span<const SpriteVertex> Vertices() const;
    [[nodiscard]] std::span<const DrawCommand> Commands() const;

private:
    // Los dos triángulos de `corners`, cada vértice con su color, en el tramo de `texture` (si la
    // textura está prendida) con la mezcla y el alpha test de ahora.
    void Append(const QuadCorners& corners, const std::array<Color, 4>& colors, std::optional<TextureSlot> texture);

    App::PixelSize m_screen;
    // Lo que deja 0x620a00(1) con glColor4f(1, 1, 1, 1), como empieza a dibujar el login (06 § El
    // login, el dibujo): las escenas ponen el suyo.
    Color m_color{};
    BlendMode m_blend = BlendMode::Alpha;
    bool m_textureOn = true;
    bool m_alphaTest = true;
    std::vector<SpriteVertex> m_vertices;
    std::vector<DrawCommand> m_commands;
};
} // namespace Client::Render
