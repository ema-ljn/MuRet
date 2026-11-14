#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Outgoing.h"

#include <functional>
#include <string>

namespace Client::Core
{
class Log;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

// Los paquetes del castillo que llegan fuera de la batalla (CL9 hito 12; 04 § Castle Siege, las ventanas del
// castillo): lo que hacen 0x4e2b10 y sus funciones con el B2 (0x00..0x10, 0x1A y 0x1F), el B3, el B4, el B5, el B6 y
// el B8 sobre WorldRuntime::castleSiege, el héroe, los paneles y el chat. Ninguno mira la escena ni el flag de cifrado.
namespace Client::Scenes
{
class WorldState;
struct WorldRuntime;

// El GameServer: los C1 que manda el B2 02 con 1 (0x4df1a0).
using CastleSender = std::function<void(Protocol::Outgoing)>;

struct CastleReceiveContext
{
    WorldState& world;
    // El rand() de la CRT: lo que abre el B2 00 (0x6599a0) pasa por el registro de los paneles.
    Render::Models::CrtRandom& random;
    // Lo que se descarta (un registro que caería fuera de 0x7d5c504..0x7d5c658, Q-C1360) queda en el log.
    Core::Log& log;
    // Con qué se miden los mensajes (0x555190); sin medidor, lo que muestra uno corta con error.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    // Text.bmd: 860, 1386, 1500..1527, 1609 y 1616.
    const Data::Text::TextTable* texts = nullptr;
    CastleSender send{};
};

// 0x4e2b10 con un paquete del castillo: B2 00 (0x4e50bd), 01 (0x4df080), 02 (0x4df1a0), 03 (0x4e5186), 04
// (0x4e51fa), 05 (0x4df7d0), 06 (0x4e52b7), 07 (0x4df950), 08 (0x4e537b), 09 (0x4e53e7), 10 (0x4e548a), 1A
// (0x4e54f6) y 1F (0x4e5532); B3 (0x4dfc60), B4 (0x4dfce0), B5 (0x4dfd80), B6 (0x4e0ed0) y B8 01 (0x4e0700).
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleSiegePacket(const Protocol::CastleSiege::CastleSiegePacket& packet, const CastleReceiveContext& context);

// 0x64b0f0, lo que el B2 00 con 1 o 2 guarda en [0x7d536f8] después de abrir el panel 0x80000: +4 en 1 (el byte de
// abierto, DelegatedPanels::castleSiege), el estado, los dos nombres con strncpy, las tres fechas y lo que falta.
void StoreTheCastleState(const Protocol::CastleSiege::CastleState& state, WorldRuntime& runtime);
} // namespace Client::Scenes
