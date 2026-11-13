#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Events.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

// CL9 hito 11: lo que los paquetes de los eventos (Protocol::Events, 04 § Los eventos) hacen al llegar, como el
// dispatcher 0x4e2b10 y sus handlers, sobre los globales de WorldRuntime: los avisos (0x632e30, 0x633300), los
// mensajes (0x555190), el .att (0x7d0d348), el objeto del evento de [0x723c60], los chips (0x78abb00), Crywolf y los
// personajes. Lo que es de otra ola u otro hito corta con su dirección, después de lo que el original hizo antes.
namespace Client::Scenes
{
class WorldState;
struct EventMapObject;
struct WorldRuntime;

struct EventPacketContext
{
    WorldState& world;
    // El rand() de la CRT, que gastan los cierres de los paneles (0x657a90, 0x65cae0).
    Render::Models::CrtRandom& random;
    // Text.bmd: sin la tabla, un aviso corta.
    const Data::Text::TextTable* texts = nullptr;
    // El medidor y la pantalla de los mensajes (0x555190); sin medidor, un mensaje corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    // GetTickCount al aplicar el paquete (0x40ba60).
    std::uint32_t tickCount = 0;
    // Player.bmd, el esqueleto del 0x20a para 0x4ea030 desde 0x5111a0; sin él, ese caso corta.
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    // El C1 03 31 de cerrar el NPC de evento (0x658a18 y 0x658c0e) desde el 90 y los cierres de todos los paneles.
    // Vacío, cerrarlo corta.
    std::function<void()> sendWindowClose{};
    // El send() de los paquetes de mundo: el C1 05 18 del BD 03 con el resultado 1 (0x4e11e1). Vacío, ese envío corta.
    std::function<void(Protocol::Outgoing)> send{};
    // La carpeta Data, de donde el BD 00 vuelve a leer la luz y el .att con otro estado de Crywolf (0x42ff81 y
    // 0x42ffe8). Vacía, ese cambio corta.
    std::filesystem::path dataRoot{};
};

// Los sonidos que pidió, en orden: 0x41ed10 (los que suenan) y 0x41ee00 (los que se apagan). El último que suena
// queda además en WorldRuntime::lastSoundId, el gancho de la CL10.
struct EventPacketResult
{
    std::vector<std::int32_t> sounds;
    std::vector<std::int32_t> stoppedSounds;
};

[[nodiscard]] Core::Expected<EventPacketResult, std::string>
ApplyEventPacket(const Protocol::Events::EventPacket& packet, const EventPacketContext& context);

// 0x40bab0, el texto de la cuenta de 30 s de la entrada a Devil Square, Blood Castle y Chaos Castle con +8 de 1 a
// 14: nada con otro valor; pasados 30 s desde +0xc (la resta sin signo), +8 en 0 si `write` y nada; si no, el texto
// con los segundos que faltan (wsprintfA). Sin Text.bmd corta.
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string>
EventCountdownText(EventMapObject& event, std::uint32_t tickCount, const Data::Text::TextTable* texts, bool write);

// 0x42fdc0: los globales de la interfaz de Crywolf a su valor inicial, el estado y la etapa en 0, el NPC elegido de la
// llegada (0x6c8e54, GameMovementState::chosenNpc) y el segundo de los objetivos elegidos (0x6c8e2c) en −1. Lo llaman
// la vuelta a la selección (0x4c363a) y el BD 00 con la etapa 6.
void ResetTheCrywolfInterface(WorldRuntime& runtime);
} // namespace Client::Scenes
