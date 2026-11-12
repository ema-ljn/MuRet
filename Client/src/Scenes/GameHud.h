#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string>

// El HUD del juego (06 § HUD y ventanas, CL8 hito 2): 0x5a7320(1), lo que el dibujo del juego llama en 0x637660, las
// superposiciones de 0x5b0550 (0x6376bb) y el cursor del juego, 0x5ac2d0, que llama dos veces (0x6376d7 y 0x6376e1).
// En 640 × 480, con el estado de GL que dejan sus primitivas y con los rand() de la tabla de protección en su orden.
// Lo que es de otra ola corta con su dirección donde su condición se cumple; lo que solo dibuja y no saca rand(),
// como los ítems 3D de las ranuras rápidas, no se dibuja hasta su hito y lo dice en su lugar.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct GameProjection;
struct HudState;

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
    // 0x5767b88: el reloj del mundo, en ms.
    float worldClock = 0.0f;
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480 (UI::MouseIn640x480).
    UI::Point mouse{};
    // 0x7d36874: el botón izquierdo abajo.
    bool leftDown = false;
};

// 0x5a7320(1): los 31 hijos del HUD en su orden (06 § HUD y ventanas, "El HUD hijo por hijo"). En el estado base
// (sin ventanas, sin party, sin evento) dibuja las arenas o el humo de su mapa, la barra de abajo, la experiencia,
// el AG, los orbes, la skill actual, las celdas, los números y las cuentas de las ranuras rápidas, y saca del rand()
// los cierres del registro propio de las flechas, los doce avisos de durabilidad, la barra, la experiencia, el AG y
// la barra principal, las consultas del comercio y de la tienda y las dos celdas del héroe.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHud(GameHudContext& context);

// 0x5b0550: las superposiciones del modo 0x77f3b08. Con 0 no dibuja nada (la ventana /move, el 4, no está abierta);
// las ayudas de F1 (1 y 2), la del ítem (3) y la ventana /move son del hito 6 y cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheOverlays(GameHudContext& context);

// 0x5ac2d0: el cursor del juego, con su tabla de elección (06 § El cursor del juego). Con un diálogo (0x7d3a10c) o
// 0x78abb20, el de siempre; si no, el del ítem del piso (5), el de hablar (6, que anima sus UV con el reloj), el del
// objeto, el de atacar (4, o 0x68d en el 30), el de los comandos (0x65a), el de reparar (7, girando con el modo 2),
// el de apretar (3, o 0xa con 0x77f3b5c) y el normal (2). Lo que lee de otra ola corta con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameCursor(GameHudContext& context);
} // namespace Client::Scenes
