#pragma once

#include "Core/Expected.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "UI/OldWindows.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Scenes
{
class ITextDrawer;
}

// El dibujo de las ventanas viejas (CL8 hito 7; 06 § HUD y ventanas): 0x490390 (0x637673) dibuja las del
// administrador de abajo hacia arriba, y 0x637680 la lista de ventanas, siempre, con su vtable + 0x14 (0x492500, el
// marco). Todo en 640 × 480.
namespace Client::UI
{
// Los slots de Interface\ que carga 0x61ccb0: la barra del título (win_titlebar.jpg), el botón (win_button.tga), los
// botones de la barra (win_size.jpg) y la esquina del tamaño (win_resize.tga).
inline constexpr std::uint16_t WindowTitleSlot = 0x645;
inline constexpr std::uint16_t WindowButtonSlot = 0x646;
inline constexpr std::uint16_t WindowTitleButtonsSlot = 0x647;
inline constexpr std::uint16_t WindowResizeSlot = 0x648;

// Lo que el dibujo usa y deja: el lote, los textos, la fuente del DC (0x57634f4) y los colores de 0x465950.
struct OldWindowDrawContext
{
    Render::SpriteBatch& sprites;
    Scenes::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c y 0x6c8e64.
    Render::Text::TextColors& textColors;
};

// Lo que el dibujo lee del juego.
struct OldWindowDrawView
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d366d4: el flanco del clic (0x492460).
    bool leftClick = false;
    // 0x659290(1): el panel 1, la ventana de amigos. Sin ella, todas se dibujan como si no fueran la de arriba.
    bool friendsOpen = false;
    // Los textos de Text.bmd de la ventana de amigos (los encabezados de las columnas y el botón del estado). Sin
    // ellos, vacíos.
    const OldWindowTexts* texts = nullptr;
    // 0x5762ec0: la lista que toma las flechas del teclado; su renglón elegido va más claro (0x461cd0).
    std::int32_t focusedList = 0;
    // GetFocus() de 0x468320: el id del EDIT con el foco de Windows (OldWindowManager::focusedEdit con
    // ChatFocus::OldWindow), o 0.
    std::int32_t focusedEdit = 0;
};

// 0x491960(índice, alfa): el glColor4ub de cada uno de los 19 colores de las ventanas, con el alfa en ftol(alfa ·
// 255) (el 3, sombra, en ftol(alfa · 179)). Con otro índice, ninguno.
[[nodiscard]] std::optional<Render::Color> OldWindowPalette(std::int32_t index, float alpha);

// 0x490390: las ventanas a la vista, de abajo hacia arriba. Después, la proyección (glMatrixMode, glLoadIdentity y
// gluOrtho2D(0, W, 0, H)), que en MuRet es la del lote, y +0x40 en 1.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheOldWindows(OldWindowManager& manager, const OldWindowDrawContext& context, const OldWindowDrawView& view);

// 0x637680: el marco de la lista de ventanas (0x492500), aunque esté oculta.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheWindowList(OldWindowManager& manager, const OldWindowDrawContext& context, const OldWindowDrawView& view);
} // namespace Client::UI
