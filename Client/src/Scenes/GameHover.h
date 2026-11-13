#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// El hover y Alt del juego (CL8 hito 6; 06 § HUD y ventanas, "La pasada 2D del juego"): 0x5bbbc0, lo segundo de la
// pasada 2D (0x637650), entre los textos 3D (0x4383f0) y el HUD (0x5a7320). Pone el nombre del elegido bajo el mouse
// (un NPC o un jugador en un globo con 0x557170, un monstruo arriba al medio con 0x623690) y, con Alt, los nombres
// de los ítems del piso (0x5ba790, de la CL9.3). Lo que lee el picking del frame (0x59ec80) MuRet lo deja en −1
// (las búsquedas son de la CL9.1 y la CL9.3): en el juego de hoy solo queda lo que el original hace siempre.
namespace Client::Scenes
{
class ITextDrawer;
class WorldState;
struct GameDrawTailState;
struct HudState;

struct GameHoverContext
{
    WorldState& world;
    // Los ítems del piso (0x77f46d0, 1000 de 0x220 bytes) del dibujo de este frame.
    const GameDrawTailState& tail;
    // El rand() del frame, para el ancho del mundo (0x5bb8d0) del nombre de un monstruo.
    Render::Models::CrtRandom& random;
    // 0x6c8e28 (el ítem del piso), 0x6c8e2c (el NPC), 0x6c8e30 (el personaje), 0x6c8e34 y 0x6c8e38, como los dejó el
    // picking de este frame.
    const std::array<std::int32_t, 5>& targets;
    // 0x77f3b66 (durabilityWarning), la fuente del DC, 0x6c8e5c y 0x6c8e64, y 0x77f31b8 y 0x77f31bc.
    HudState& hud;
    // 0x77f36d4 + 0x12 · 4, el latch de Alt de 0x553520, y 0x78abb38, los nombres del piso prendidos. Solo los usa esta
    // función.
    std::int32_t& altLatch;
    std::uint8_t& altNames;
    // GetAsyncKeyState(0x12) en el momento del dibujo.
    bool altDown = false;
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    // Un dibujo intermedio de MuRet no escribe los globos (0x557170): el dibujo de los globos lee una copia de la
    // tabla del último frame del original, que es el que los escribe. Los demás estado que escribe le llegan en copias.
    bool projecting = false;
    // Text.bmd: los puestos del gremio del globo (0x556dd0).
    const Data::Text::TextTable* texts = nullptr;
};

// 0x5bbbc0: en Chaos Castle (18 a 23), sin "webzen" en el nombre del héroe, con un NPC o un personaje elegido no hace
// nada. Si no, 0x77f3b66 en 1, glColor3f(1, 1, 1), 0x620980 (sin mezcla) y, en este orden: con un NPC elegido, su
// nombre solo en un globo (0x557170 con el texto vacío de 0x78abb3c, el modo 0 y sin color); si no, con un personaje
// y 0x6c8e38 en −1, un monstruo (+0x88 en 2) va con su nombre centrado en la mitad del ancho del mundo y en y = 10
// (0x623690, con la fuente del DC, el fondo 0xff000064 y el texto 0xffc8e6ff) y los demás, sin tienda personal en
// 0x7245c8 (0x451770), en un globo como el NPC; si no, con un ítem del piso elegido, su nombre (0x5ba790, CL9.3,
// corta). Después Alt: el flanco (0x553520(0x12)) invierte 0x78abb38, y con él prendido o Alt abajo, el nombre de
// cada ítem del piso vivo y a la vista que no es el elegido (0x5ba790 con 1, CL9.3: corta).
[[nodiscard]] Core::Expected<void, std::string> DrawTheHover(GameHoverContext& context);
} // namespace Client::Scenes
