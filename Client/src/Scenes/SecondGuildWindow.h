#pragma once

#include "Core/Expected.h"
#include "Scenes/GuildPanels.h"
#include "UI/PanelRegistry.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

// La ventana 2 del gremio, [0x7d536f4] (0x654900, vtable 0x6baa9c; CL9 hito 8, fase 3; 06 § El gremio): el alta de un
// gremio en páginas. La abren el 54 (0x4e4279) y el 01 del Guild Master (0x4e3450); el registro la pasa con
// 0x45aa80(0) (la vtable + 0x18, que llama a la + 0x20, 0x656df0) y la dibuja con la + 0x14 (0x656e70). La página es
// 0x7d536ec (GuildState::createStep): 0, la pregunta (0x6563e0 y 0x655d70); 1, el nombre y la marca (0x655130 y
// 0x654f80); 2, la marca sola (0x655c70 y 0x655c10, a la que solo llega 0x6538e0, sin referencias: Q-C1288); 3, la
// confirmación (0x655ac0 y 0x6552b0), que manda el C1 55. El gremio nuevo es la entrada 1999 de la tabla (0x78a5970):
// el nombre en +0xd (0x78a597d) y la marca en +0x16 (0x78a5986).
namespace Client::Scenes
{
class WorldState;
struct GameHudContext;

struct SecondGuildStepContext
{
    WorldState& world;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // GetTickCount() del paso: el reloj del diálogo que abre el botón de la marca (0x6600d0).
    std::uint32_t now = 0;
    // Con qué mide 0x6600d0 sus renglones; sin medidor, el botón de la marca corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    // Text.bmd: el 1269 del botón de la marca, y el 457 y el 458 que mira 0x622c70. Sin él, lo que los lee corta.
    const Data::Text::TextTable* texts = nullptr;
    // FilterName.bmd y Filter.bmd, las palabras que mira 0x622c70.
    std::span<const std::string> filterName{};
    std::span<const std::string> filter{};
    // Lo que manda (el 54, el 55): el GameServer de la sesión. Sin él, lo que manda corta.
    GuildSender send{};
};

// 0x45aa80(0) y 0x656df0: la cola vacía (nadie le manda mensajes: los botones mandan su comando a la ventana del id de
// su padre con 0x490b40, y la ventana 2 no está en el administrador, *inferido*); con el mouse sobre ella (0xbe × 0x5a)
// y otra ventana bajo el mouse o agarrándolo, nada. Si no, el mouse sobre la ventana de 0xbe × 0x1b1 prende 0x775857c
// y la página da su paso.
[[nodiscard]] Core::Expected<void, std::string> StepTheSecondGuildWindow(const SecondGuildStepContext& context);

// 0x657930 (0x657959): el paso de arriba en el registro, con lo que el paso de la UI le da. `context` se copia; lo que
// mira (el mundo, el medidor, Text.bmd y los filtros) tiene que vivir más que el contexto del registro.
void LendTheSecondGuildStepTo(UI::PanelContext& panels, const SecondGuildStepContext& context);

// 0x656e70: glColor3f(1, 1, 1), el fondo de la ventana (0x104 y 0x105) sin mezcla, la mezcla, el título en negrita
// (0x554180 en (x + 0x23, y + 0xc) con la caja de 120 · W / 640, los colores 0xffdcdcdc sobre 0xff141414: el 180, el
// 1303, el 1304 o el 228 según la página) y el dibujo de la página.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSecondGuildWindow(GameHudContext& context);
} // namespace Client::Scenes
