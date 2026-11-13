#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelRegistry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::UI
{
struct GameUiState;
}

// La ventana del gremio, [0x7d536f0] (0x64f3e0, vtable 0x6baa58; CL9 hito 8, fases 4 y 5; 06 § El gremio): la abre el
// registro con el panel 0x10, la pasa con 0x45aa80(0) (la vtable + 0x18, que llama a la + 0x20, 0x651840) y la dibuja
// con la + 0x14 (0x651c20). Tiene tres pestañas (+0x80: 0, 1 y 2, desde 1), cada una con su paso (0x64fc00, 0x650350
// y 0x650c10) y su dibujo (0x64fdf0, 0x6507d0 y 0x651400, Scenes/GuildWindowTabs), y tres listas sobre UI::OldList:
// los miembros (+0x94), el aviso (+0x1c8) y la alianza (+0x2f8).
namespace Client::Scenes
{
class WorldState;
struct GameHudContext;

struct GuildWindowStepContext
{
    WorldState& world;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // La pantalla del aviso que deja salir de la alianza (0x555190 corta y mide en ella).
    App::PixelSize screen{};
    // Lo que manda (el E9) y la respuesta del diálogo que cancela: el GameServer de la sesión.
    GuildSender send{};
    // 0x553520(vk), el flanco de PgUp (0x21) y PgDn (0x22), y GetAsyncKeyState de las flechas de la lista (0x47b0f0).
    // Sin ellos, la pestaña de los miembros corta.
    std::function<bool(std::uint8_t)> keyEdge{};
    std::function<bool(std::uint8_t)> keyDown{};
    // GetTickCount() del paso, el medidor y Text.bmd: los diálogos de los botones del maestro (0x6600d0, los textos
    // 1367 a 1369). Sin ellos, el botón que abre uno corta.
    std::uint32_t now = 0;
    const Render::Text::ITextMeasure* measure = nullptr;
    const Data::Text::TextTable* texts = nullptr;
};

// 0x6517a0 → 0x46c930, la vtable + 0x94 de la lista de los miembros: sin nombre, nada. Con la lista vacía, +0x130 dice
// si el que entra es el héroe (+0x1d9). Lo pone adelante (el elemento de 0x575de08: +0 en 0 y el nombre con strncpy de
// 0xb), con más de 0x96 saca del final (0x47b7e0), y elige y corre: 0, uno más de corrimiento con más que los
// renglones, lo acota, 1 si es el único, y al final el último (la cuenta) con el corrimiento hasta el final (−10.000).
void AddTheGuildMember(WorldState& world, const GuildMemberSlot& slot);

// 0x6517d0 → 0x47a410, la vtable + 0x2c: la vacía, pone +0 en 0 a todos (0x47ab40, ya ninguno), elige 0 y vuelve el
// corrimiento a 0.
void ClearTheGuildMembers(GuildWindowState& window);

// 0x651680(texto), el aviso del gremio del 0D tipo 2 (0x4c665f): 0x459c70 lo corta en hasta 5 renglones de 0x6e de
// ancho con la fuente del DC (UI::WrapByWidth), agrega cada renglón que no está vacío con la vtable + 0x94 de la
// lista +0x1c8 (0x46bca0: como 0x46c930, adelante, sin el héroe) y la corre la cuenta menos los renglones (+0x34 con
// +0x38): lo último que entró queda a la vista.
[[nodiscard]] Core::Expected<void, std::string> AddTheGuildNotice(GuildWindowState& window,
                                                                  const Render::Text::ITextMeasure& measure,
                                                                  Render::Text::FontKind font, App::PixelSize screen,
                                                                  std::string_view text);

// 0x651790 → 0x480260, la vtable + 0x2c de la lista del aviso: como 0x47a410.
void ClearTheGuildNotice(GuildWindowState& window);

// 0x6517e0(marca, nombre, número) → 0x46d900, la vtable + 0x94 de la lista de la alianza: sin nombre (o vacío), nada;
// si no, el elemento de 0x5762e50 (+0 en 0, la marca, el nombre con strncpy de 8 y +0x4c en 0, el número en +0x50)
// adelante, como 0x46c930 sin el héroe.
void AddTheUnion(GuildWindowState& window, const std::array<std::uint8_t, GuildMarkNibbles>& mark,
                 std::string_view name, std::uint8_t number);

// 0x651830 → 0x4818b0, la vtable + 0x2c de la lista de la alianza: como 0x47a410.
void ClearTheUnions(GuildWindowState& window);

// 0x45aa80(0) sobre una ventana del registro, sin padre y fuera del administrador (las dos del gremio, como el registro
// del chat): con el mouse sobre ella y otra bajo el mouse o agarrándolo, no da su paso. Con el izquierdo abajo,
// 0x490aa0 no la encuentra para traerla al frente (*inferido*).
[[nodiscard]] bool PassesTheGuildWindowGate(const UI::OldWindowBase& base, UI::GameUiState& ui, std::int32_t mouseX,
                                            std::int32_t mouseY);

// 0x651840: el mouse sobre 0xbe × 0x1b1 prende 0x775857c; el clic en el botón de cerrar (0x18 × 0x18 en (0x19,
// 0x18b)) lo apaga, deja 0x77f3b10 en 0 y 0x6c8dcc en 6, suena el 0x19 y cierra la ventana (0x6522f0). Con el héroe en
// un gremio (+0x368 distinto de 0xff) y el izquierdo abajo, las pestañas (0x34 × 0x10 en x + 0xf, 0x43 y 0x77, y +
// 0x4b): la elegida cancela el diálogo suyo (0x64fbd0) y la de la alianza pide antes una vez la lista (C1 03 E9) si el
// gremio del héroe tiene alianza (+4 de su entrada). Después, el paso de la pestaña.
//
// 0x64fc00, la pestaña 0: la lista del aviso (0x480e00 con su renglón 0x46c3d0) con 0x5762eb4, y el botón +0x42c.
// El maestro de la alianza (el gremio +0xd igual a la alianza +4 de su entrada) recibe el aviso 1270 (OK); el otro
// maestro, el diálogo de disolver el gremio (1363 a 1366, Confirmar, 0x64de90); el que no es maestro sale: su lugar en
// 0x787a5e8 (0x64faa0 con su nombre) a 0x6d5e2c y el diálogo 0x7d del juego con los campos del chat como 0x64de90.
//
// 0x650350, la pestaña de los miembros: 0x5762eb4 con el id de la lista, la lista (0x45aa80(0) y 0x47b0f0, con su
// renglón 0x46d3a0) y 0x5762eb4 en 0; PgUp y PgDn la corren los renglones que muestra (la vtable + 0x38 y + 0x34). Con
// el héroe maestro (+0x368 en 0x80), los tres botones (0x45b200) sobre el elegido: el cargo (no al maestro) abre la
// ventana de los cargos, que corta (0x64e600, P-C288); quitarlo (solo al 0x40 o al 0x20) y echarlo (no al maestro)
// abren el diálogo de los paneles con Confirmar y Cancelar, que responden 0x64def0 y 0x64e550 (GuildPanels).
//
// 0x650c10, la pestaña de la alianza: la lista +0x2f8 (0x4824d0 con 0x46e1e0) con 0x5762eb4 y, con el héroe maestro y
// alianza (+4 de su entrada), +0x64c: el maestro de la alianza recibe el aviso 1271; el otro manda C1 07 E5 01 02 con
// su clave y avisa el 1387 (0x555190, tipo 1). Después +0x6d4 sobre la fila elegida que no es su gremio: su nombre a
// 0x7d535a8 y el diálogo del 1423 con el nombre y el 1369 (Confirmar, 0x650830, el EB).
[[nodiscard]] Core::Expected<void, std::string> StepTheGuildWindow(const GuildWindowStepContext& context);

// 0x657977: el paso de arriba en el registro. `context` se copia; el mundo tiene que vivir más que el contexto del
// registro.
void LendTheGuildWindowStepTo(UI::PanelContext& panels, const GuildWindowStepContext& context);

// 0x651c20: el fondo de las ventanas sin mezcla, el título (el 180, en negrita, 0xffdcdcdc sobre 0) y, con el héroe en
// un gremio, "%s ( Score:%d )" (0xff64ffc8) o, sin él, los textos 185 a 187 (la normal, 0xffe6e6e6); el botón de cerrar
// con su ayuda (el 220); con gremio, las tres pestañas (las rayas de 0x621aa0 bajo las otras, el 0x646 y los textos
// 946, 1330 y 1352 centrados en 52 · rx) y el dibujo de la pestaña.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildWindow(GameHudContext& context);
} // namespace Client::Scenes
