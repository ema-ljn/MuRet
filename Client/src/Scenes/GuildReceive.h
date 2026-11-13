#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Guild.h"
#include "Scenes/GuildPanels.h"

#include <cstdint>
#include <string>

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

// Los paquetes del gremio, de la alianza y de la guerra que llegan (CL9 hito 8; 04 § El gremio, la alianza y la
// guerra; 06 § El gremio): lo que hacen 0x4e2b10 y sus funciones sobre WorldRuntime::guild, los personajes, los paneles
// y el chat. Ninguno mira la escena ni el flag de cifrado.
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;

struct GuildReceiveContext
{
    WorldState& world;
    // El rand() de la CRT: lo sacan las consultas del registro de los paneles (0x6599a0 y 0x65cae0).
    Render::Models::CrtRandom& random;
    // Con qué se miden los mensajes (0x555190, 0x554610) y los renglones del diálogo (0x6600d0); sin medidor, lo que
    // los usa corta.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    // Text.bmd: los textos de los resultados y de los diálogos.
    const Data::Text::TextTable* texts = nullptr;
    // 0x57678bc: la clave del jugador (el E6 la compara).
    std::uint16_t playerKey = 0;
    // GetTickCount(): con lo que se abre el diálogo de los paneles (0x6600d0).
    std::uint32_t now = 0;
    // El GameServer: los C1 que mandan el 50, el 53, el 62, el 63, el E5 y el EB, y las ventanas al cerrarse.
    GuildSender send{};
    // Las acciones de los modelos (0x4ea030) para el gesto de la guerra: el 62 y el 63 llaman a 0x576a60 con el héroe
    // (Scenes::PlayTheEmote, hito 1 de la CL9) antes de su C1 18. Sin ellos, cortan antes del gesto.
    const IModelLibrary* models = nullptr;
};

// 0x4e2b10 con un paquete del gremio: 50 (0x4d74b0), 51 (0x4d7a20), 52 (0x4d7b60), 53 (0x4d7c30), 54 (0x4e4279), 56
// (0x4d7ff0), 5D (0x4e42f0), 60 (0x4d8150), 61 (0x4e4382), 62 (0x4d8260), 63 (0x4d8870), 64 (0x4e4400), 66 (0x4e42c7 →
// 0x447b90), E1 (0x4d9810), E5 (0x6565e0), E6 (0x4d9940), E9 (0x4d9e50) y EB (0x4d9b80).
[[nodiscard]] Core::Expected<void, std::string> ApplyGuildPacket(const Protocol::Guild::GuildPacket& packet,
                                                                 const GuildReceiveContext& context);
} // namespace Client::Scenes
