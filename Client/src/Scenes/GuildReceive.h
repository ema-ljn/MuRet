#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Guild/Guild.h"
#include "Scenes/GuildPanels.h"

#include <cstdint>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// Los paquetes del gremio, de la alianza y de la guerra que llegan (CL9 hito 8; 04 § El gremio, la alianza y la
// guerra; 06 § El gremio): lo que hacen 0x4e2b10 y sus funciones sobre WorldRuntime::guild, los personajes, los paneles
// y el chat. Ninguno mira la escena ni el flag de cifrado.
namespace Client::Scenes
{
struct WorldEntryResources;

struct GuildReceiveContext
{
    Kernel::WorldState& world;
    // El rand() de la CRT: lo sacan las consultas del registro de los paneles (0x6599a0 y 0x65cae0).
    Crt::CrtRandom& random;
    // Con qué se miden los mensajes (0x555190, 0x554610) y los renglones del diálogo (0x6600d0); sin medidor, lo que
    // los usa corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // Text.bmd: los textos de los resultados y de los diálogos.
    const Tables::TextTable* texts = nullptr;
    // 0x57678bc: la clave del jugador (el E6 la compara).
    std::uint16_t playerKey = 0;
    // GetTickCount(): con lo que se abre el diálogo de los paneles (0x6600d0).
    std::uint32_t now = 0;
    // El GameServer: los C1 que mandan el 50, el 53, el 62, el 63, el 65, el E5 y el EB, y las ventanas al cerrarse.
    GuildSender send{};
    // Las acciones de los modelos (0x4ea030) para el gesto de la guerra: el 62 y el 63 llaman a 0x576a60 con el héroe
    // (Actors::PlayTheEmote, hito 1 de la CL9) antes de su C1 18. Sin ellos, cortan antes del gesto.
    const Models::IModelLibrary* models = nullptr;
    // El 54 abre creación y cierra ventanas de NPC/castillo/batalla por los servicios canónicos del mundo.
    const WorldEntryResources* worldEntry = nullptr;
};

// 0x4e2b10 con un paquete del gremio: 50 (0x4d74b0), 51 (0x4d7a20), 52 (0x4d7b60), 53 (0x4d7c30), 54 (0x4e4279), 56
// (0x4d7ff0), 5D (0x4e42f0), 60 (0x4d8150), 61 (0x4e4382), 62 (0x4d8260), 63 (0x4d8870), 64 (0x4e4400), 65 (0x4d9260),
// 66 (0x4e42c7 → 0x447b90), 67 (0x4e44aa), E1 (0x4d9810), E5 (0x6565e0), E6 (0x4d9940), E9 (0x4d9e50) y EB (0x4d9b80).
[[nodiscard]] Core::Expected<void, std::string> ApplyGuildPacket(const Protocol::Guild::GuildPacket& packet,
                                                                 const GuildReceiveContext& context);

// 0x4c1800: sin guerra (0x576796c en 0), sin rival (0x6c82f0 en −1 y el primer byte de 0x5767804 en 0) y +0x374 de
// cada personaje vivo en 1 si es del gremio del héroe (con gremio) y en 0 si no. La llaman el 60 sin guerra y el no del
// diálogo 0x7f (0x62c854 y 0x62c86a, dos veces).
void ClearTheWar(Kernel::WorldState& world);
} // namespace Client::Scenes
