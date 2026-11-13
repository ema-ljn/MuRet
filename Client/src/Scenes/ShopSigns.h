#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/Widget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::UI
{
class PanelRegistry;
}

// Los carteles de las tiendas personales sobre las cabezas (CL9 hito 7, fase 6b; 06 § Lo social en pantalla, "Las
// tiendas personales", "Los carteles"): el hijo 13 del HUD (0x451c20) con el lugar (0x451d60 → 0x452110), el reparto
// (0x451e40) y cada cartel (0x452a10, con su caja 0x452d50 y su quad 0x452e50); lo que el alta mide para él (0x452540:
// el texto 1104, las dos líneas de 0x452f80 y el tamaño de 0x4531b0), y lo que leen los globos de chat: el título corto
// (0x4518b0) y los colores del título (0x4517c0, 0x451af0, 0x451b50 y 0x451bb0). Solo 0x5bb8d0 saca rand().
namespace Client::Scenes
{
class ITextDrawer;
class WorldState;
struct ActorState;
struct GameProjection;
struct PersonalShopState;
struct ShopTitle;

// 0x770df9c: el texto 1104 de Text.bmd, la cabeza de los carteles y del título del globo.
inline constexpr std::size_t ShopSignHeaderText = 1104;

// El texto 1104, o el error con la dirección que lo lee si no hay Text.bmd.
[[nodiscard]] Core::Expected<std::string_view, std::string> ShopSignHeader(const Data::Text::TextTable* texts,
                                                                           std::string_view address);

// +0x20 y +0x30 del cartel.
struct ShopTitleLines
{
    std::string first;
    std::string second;

    friend bool operator==(const ShopTitleLines&, const ShopTitleLines&) = default;
};

// 0x452f80(título, +0x20, +0x30): con 20 bytes o menos, el título entero en la primera y la segunda vacía; con más, la
// primera hasta el primer corte desde la mitad (largo >> 1) que no parte una letra de dos bytes (un byte de 0x80 o más
// cuenta 2, con el que lo sigue) y la segunda con el resto.
[[nodiscard]] ShopTitleLines SplitTheShopTitle(std::string_view title);

// 0x4518b0(personaje, salida): sin cartel, nada (la salida queda como estaba: el texto vacío de 0x556dd0). Con él, el
// título entero si tiene 14 bytes o menos; si no, los primeros 12, o 13 si el 12 es la segunda mitad de una letra de
// dos bytes, y ".." (0x6c7970).
[[nodiscard]] std::string ShortShopTitle(const PersonalShopState& shops, std::size_t index);
[[nodiscard]] std::string ShortShopTitle(std::string_view title);

// Lo que 0x452540 deja en el cartel para el dibujo, con las medidas de GetTextExtentPoint32A.
struct ShopSignLayout
{
    // +0x58: el ancho del texto 1104 en negrita.
    std::int32_t header = 0;
    ShopTitleLines lines;
    // +0x50 y +0x54 (0x4531b0).
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// 0x452540 → 0x452f80 y 0x4531b0: el nombre (+0) en negrita y las dos líneas en la normal; el ancho es el mayor del
// texto 1104 más el nombre y de las líneas que hay, y el alto, el paso de la letra por los renglones (el nombre y una o
// dos líneas). El original lo mide al poner el cartel (0x4511f0); MuRet, en cada dibujo: las fuentes no cambian en el
// juego
// (*inferido*), así que da lo mismo.
[[nodiscard]] ShopSignLayout MeasureTheShopSign(const ShopTitle& sign, std::string_view header,
                                                const Render::Text::ITextMeasure& measure, int fontStep);

// +0x48..+0x54 de un cartel, en píxeles.
struct ShopSignBox
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;

    friend bool operator==(const ShopSignBox&, const ShopSignBox&) = default;
};

// 0x452110: el punto 60 sobre el techo del dueño (+0x138 + z + 60, como el globo) proyectado con 0x6205b0 a 640 × 480 y
// pasado a píxeles con IMUL y la división con signo; la x, menos la mitad del ancho, y la y, menos 36 · H / 480.
[[nodiscard]] Core::Expected<UI::Point, std::string>
PlaceTheShopSign(const GameProjection& projection, const ActorState& owner, std::int32_t width, App::PixelSize screen);

// 0x451e40, en el orden del mapa: para cada par que se pisa (comparaciones con signo), el primero sube hasta el borde
// de arriba del otro si su borde de abajo está antes de la mitad del otro, y si no baja a su borde de abajo; la x no
// cambia. Después cada uno contra los bordes, sin signo: la x en 0 o, desde W − ancho, en W − ancho − 4; la y en 0 o,
// desde H − alto, en H − alto.
void SpreadTheShopSigns(std::span<ShopSignBox> signs, App::PixelSize screen);

// 0x452e50(lugar, tamaño): el quad de un renglón, en píxeles. La x en 0 y, si x + ancho pasa de W (sin signo), en
// W − ancho; la y en 0 y, si y + alto pasa de H − H · 47 / 480 (sin signo), en H − H · 47 / 480: sin restar el alto, y
// con 480 donde 0x554000 tiene 640 (07 Q-C976).
[[nodiscard]] UI::Point ShopSignRowAt(UI::Point at, std::int32_t width, std::int32_t rows, App::PixelSize screen);

// Los colores de un cartel (0x452a10, por el PK) y del título de un globo (0x451af0, 0x451b50 y 0x451bb0, que miran
// también +0x370): el del nombre y el título, el del texto 1104 y el fondo.
struct ShopSignColors
{
    std::uint32_t name = 0;
    std::uint32_t header = 0;
    std::uint32_t background = 0;

    friend bool operator==(const ShopSignColors&, const ShopSignColors&) = default;
};

// 0x452a10: con el PK 4, 5 o 6, el texto 1104 y el nombre en 0xff00b4e6, 0xff006ee6 o 0xff0014f0 y el fondo en
// 0x8029396c (0x80527ab6 con +0x60); con otro, el nombre en 0xff00e6e6 (0xffffe6e6), el texto 1104 en 0xff0096fa
// (0xff29396c) y el fondo en 0x8029396c (0x800096fa).
[[nodiscard]] ShopSignColors ShopSignColorsOf(std::uint8_t pk, bool hovered);

// 0x451af0, 0x451b50 y 0x451bb0: como el cartel con +0x370 en 2 u 8 tomado como el PK 6.
[[nodiscard]] ShopSignColors ShopBubbleColorsOf(const ActorState& owner, bool hovered);

// 0x4517c0: +0x60 del cartel del personaje; sin cartel, falso.
[[nodiscard]] bool ShopSignHovered(const PersonalShopState& shops, std::size_t index);

// Lo que el dibujo de los carteles lee y deja.
struct ShopSignDrawContext
{
    WorldState& world;
    // 0x5bb8d0 (con las consultas de 0x20 y 0x10000 y sus rand()) e IsOpen(0x200).
    UI::PanelRegistry& panels;
    // 0x6203c0 del pase del mundo de este frame: 0x451d60 arma con 0x620db0 el mismo rectángulo (0, 0, w, 432) con la
    // misma cámara, y 0x6205b0 lee lo mismo (*inferido*). Sin ella, un cartel corta.
    const GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    // El SelectObject de 0x452d50: queda la fuente del último renglón dibujado.
    Render::Text::FontKind& dcFont;
    const Data::Text::TextTable* texts = nullptr;
    // Un dibujo intermedio de MuRet no escribe +0x10 ni los +0x60: van a una copia.
    bool projecting = false;
};

// 0x451c20, el hijo 13 del HUD: con el mapa vacío (+0xc en 0), nada. Si no, 0x451d60 (0x5bb8d0 con sus rand(), y el
// lugar de cada cartel), 0x451e40 y la cuenta de +0x10 (de 0 a 7). Cada cartel, en el orden del mapa: con un elegido
// (0x6c8e30), +0x10 menor que 3, la ventana de los comandos (0x200) abierta y su comando 2 (la tienda), +0x60 en 1 si
// es el del elegido (los otros lo guardan, 07 Q-C977); si no, +0x60 en 0. A la vista (+0x44) y con +0x14 del mapa (que
// apaga "/ver puestos", 0x56d722), 0x452a10 con su PK: el texto 1104 y el nombre en negrita en el primer renglón y las
// líneas del título debajo, en blanco sobre 0xc829396c con la normal, cada uno con su quad de 0x452e50.
[[nodiscard]] Core::Expected<void, std::string> DrawTheShopSigns(ShopSignDrawContext& context);
} // namespace Client::Scenes
