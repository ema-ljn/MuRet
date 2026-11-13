#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Widget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// El HUD del juego (06 § HUD y ventanas, CL8 hito 2): 0x5a7320(1), lo que el dibujo del juego llama en 0x637660, las
// superposiciones de 0x5b0550 (0x6376bb) y el cursor del juego, 0x5ac2d0, que llama dos veces (0x6376d7 y 0x6376e1).
// En 640 × 480, con el estado de GL que dejan sus primitivas y con los rand() de la tabla de protección en su orden.
// Lo que es de otra ola corta con su dirección donde su condición se cumple. Los modelos de los ítems de la UI (las
// grillas, el cursor y las ranuras rápidas) van en el pase 3D de 0x5ac120 (Scenes/UiItemDrawing.h).
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct GameProjection;
struct HudState;
struct UiItemResources;

struct GameHudContext
{
    WorldState& world;
    // El rand() del frame y las cuentas de accesos del registro propio y de las celdas del héroe (+0x41c y +0x420):
    // las reales, o las copias de un dibujo intermedio de MuRet.
    Render::Models::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    std::uint8_t& cellXDepth;
    std::uint8_t& cellYDepth;
    // Lo que el HUD escribe y dura de un frame al otro: los globales de HudState y 0x576353c (el humo de Crywolf, que
    // 0x5a7520 apaga después de dibujarlo una vez). En un dibujo intermedio, copias que nadie vuelve a leer.
    HudState& hud;
    std::uint8_t& crywolfSandOverlay;
    // 0x41ed10(id, 0, 0): el sonido que pide la barra principal (0x22, la vida baja), el gancho de la CL10.
    std::optional<std::int32_t>& sound;
    // Un dibujo intermedio de MuRet no escribe los avisos de los ítems puestos (D+0x44): van a una copia que nadie
    // lee, y el frame siguiente del original los vuelve a escribir.
    bool projecting = false;
    // 0x6203c0 del pase del mundo de este frame, que leen los globos (0x5a9050 → 0x6205b0). Sin ella, un globo vivo
    // corta.
    const GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    // Text.bmd, Item.bmd y Skill.bmd; sin alguna, lo que la lee corta con error.
    const Data::Text::TextTable* texts = nullptr;
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::SkillTable* skills = nullptr;
    // [0x6eed3c] + 0x8004, los 64 conjuntos de ItemSetOption.bmd que lee el cartel (0x40eca0); sin ellos, corta.
    const Data::Tables::ItemSetOptionTable* setOptions = nullptr;
    // [0x6eed3c] + 4, ItemSetType.bmd: el conjunto de cada tipo que leen el nombre y las opciones de un ítem en su
    // tooltip (0x40cf30, 0x40eee0); sin ella, un ítem de conjunto corta.
    const Data::Tables::ItemSetTypeTable* setTypes = nullptr;
    // 0x5767b88: el reloj del mundo, en ms.
    float worldClock = 0.0f;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480 (UI::MouseIn640x480).
    UI::Point mouse{};
    // 0x7d36874: el botón izquierdo abajo.
    bool leftDown = false;
    // GetAsyncKeyState(0x11) en el dibujo: Ctrl abajo, que 0x558bb0 mira para atacar a otro jugador (0x558e7f).
    bool controlDown = false;
    // El código del servidor elegido (App::ServerSession::ChosenServer), de donde salen [0x6d5ec4] (el lugar del
    // grupo) y [0x6d5ecc] (el número) que lee la ventana del personaje; sin él, corta.
    std::optional<std::uint16_t> chosenServer{};
    // Lo que el tramo 3D de 0x5ac120 necesita para dibujar los modelos de los ítems de la UI. Sin él (una escena sin
    // la biblioteca de los modelos, como las de las pruebas), cada ítem saca sus rand() sin dibujar.
    UiItemResources* uiItems = nullptr;
    // El send() de la sesión, el del A9 que pide el tooltip de las mascotas (0x423c87..0x42403d). Sin él (una escena
    // sin sesión, como las de las pruebas), el pedido no sale; un dibujo intermedio no lo llama.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x5a7320(1): los 31 hijos del HUD en su orden (06 § HUD y ventanas, "El HUD hijo por hijo"). En el estado base
// (sin ventanas, sin party, sin evento) dibuja las arenas o el humo de su mapa, la barra de abajo, la experiencia,
// el AG, los orbes, la skill actual, las celdas, los números y las cuentas de las ranuras rápidas, y saca del rand()
// los cierres del registro propio de las flechas, los doce avisos de durabilidad, la barra, la experiencia, el AG y
// la barra principal, las consultas del comercio y de la tienda y las dos celdas del héroe.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHud(GameHudContext& context);

// 0x5b0550: las superposiciones del modo 0x77f3b08 (GameHelps.h): la primera ayuda de F1 (1) y, con el modo que quede,
// la segunda (2) o la del ítem (3); con otro, IsOpen(2) y la ventana /move (el 4, GameMoveWindow.h).
[[nodiscard]] Core::Expected<void, std::string> DrawTheOverlays(GameHudContext& context);

// 0x5ac2d0: el cursor del juego, con su tabla de elección (06 § El cursor del juego). Con un diálogo (0x7d3a10c) o
// 0x78abb20, el de siempre; si no, el del ítem del piso (5), el de hablar (6, que anima sus UV con el reloj), el del
// objeto, el de atacar (4, o 0x68d en el 30), el de los comandos (0x65a), el de reparar (7, girando con el modo 2),
// el de apretar (3, o 0xa con 0x77f3b5c) y el normal (2). Lo que lee de otra ola corta con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameCursor(GameHudContext& context);
} // namespace Client::Scenes
