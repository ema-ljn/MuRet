#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Los globos de la selección (06 § La selección de personaje, "Los globos"): la caja oscura de character_ex.tga con el
// nombre, el rango del gremio y la clase con el nivel arriba de cada personaje de la lista. Es el objeto de +0x8dc8 de
// la UI nueva, con un arreglo de cinco globos de 0xf0 bytes en +4, cada uno con la imagen de 0xa8 bytes de 0x646fe0
// adelante: 0x63ce70 lo arma desde 0x65f4b0, 0x63cfb0 lo pone al día y 0x63cf80 lo dibuja desde 0x65fe00, antes de las
// ventanas. Lo sueltan 0x63ce50 en el login (0x65f300) y en el juego (0x65f5f0): solo hay globos en el modo 4.
// *Estático*.
namespace Client::UI
{
// Los globos de 0x63ce70, uno por lugar de la selección, en el orden del pool (0x743a16c + 0x438 · i).
inline constexpr std::size_t BalloonCount = 5;

// Lo que 0x63ca20 suma a la z del personaje antes de proyectarlo (0x6b8c78).
inline constexpr float BalloonHeadHeight = 280.0f;

// Lo que 0x63cc60 lee del registro del lugar: el +0 (vivo), la clase (+0x1d4), el nivel (+0x1d6), el CtlCode
// (+0x1d8), el nombre (+0x1d9, hasta su 0) y el rango del gremio (+0x368).
struct BalloonRecord
{
    bool alive = false;
    std::uint8_t characterClass = 0;
    std::uint16_t level = 0;
    std::uint8_t controlCode = 0;
    std::string name;
    std::uint8_t guildStatus = 0;
};

// Lo que 0x63ca20 lee del registro en cada dibujo: el personaje a z + 280 proyectado con 0x6205b0 (640 × 480) y el
// CtlCode de ahora (+0x1d8), por los tres renglones de 0x80.
struct BalloonHead
{
    Point point;
    std::uint8_t controlCode = 0;
};

// Un globo (0x63c9a0, 0x63cc60 y 0x63ca20): la imagen y los tres textos.
struct CharacterBalloon
{
    // La imagen de 0x647050(118, 54, 0x13, 0, 0, 59, 54, 0, 0, 1, 1), en píxeles: en (0, 0) hasta que 0x63ca20 la
    // ubica, cada vez después de dibujarla.
    Render::UiImage image;
    // +0xa0: lo pone 0x63cc60 con el registro vivo y lo baja con uno muerto; 0x647050 lo deja en 0.
    bool shown = false;
    // +0xac: el color del nombre, por el CtlCode.
    std::uint32_t nameColor = 0;
    // +0xb0, +0xc0 y +0xd0, como los lee 0x5542f0 (hasta el primer 0): el nombre, "(%s)" con el rango del gremio y
    // "%s %d" con la clase y el nivel. El de +0xc0 tiene 16 bytes: si no entra, sigue en el de +0xd0 (Q-C1503).
    std::string name;
    std::string guild;
    std::string classAndLevel;
};

class CharacterBalloons
{
public:
    // `texts` puede ser nullptr (los textos vacíos) y tiene que vivir lo que los globos.
    CharacterBalloons(TextureSizes sizes, const Data::Text::TextTable* texts);

    // 0x63ce70: los cinco, cada uno con 0x63c9a0: la imagen armada de nuevo en (0, 0), sin mostrar, sin color y sin
    // textos.
    void Build();
    // 0x63ce50: sin globos, 0x63cf80 y 0x63cfb0 no hacen nada.
    void Release();
    [[nodiscard]] bool IsBuilt() const;

    // 0x63cfb0 → 0x63cc60 en los cinco: uno con el registro muerto solo se esconde, y conserva los textos; uno vivo
    // toma el color y los textos. Corta con su dirección si un sprintf pasa de los 0xf0 bytes del globo (0x63cd70 y
    // 0x63cdba).
    [[nodiscard]] Core::Expected<void, std::string> Update(std::span<const BalloonRecord, BalloonCount> records);

    [[nodiscard]] bool IsShown(std::size_t index) const;

    // 0x63cf80 → 0x63ca20 en los cinco, con la mezcla que haya: la caja donde quedó y, después, el lugar nuevo y los
    // textos. Cada globo mostrado lleva su BalloonHead; corta sin él (0x63ca76).
    [[nodiscard]] Core::Expected<void, std::string>
    Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
         std::span<const std::optional<BalloonHead>, BalloonCount> heads);

    [[nodiscard]] const std::array<CharacterBalloon, BalloonCount>& Balloons() const;

private:
    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts = nullptr;
    bool m_built = false;
    std::array<CharacterBalloon, BalloonCount> m_balloons{};
};
} // namespace Client::UI
