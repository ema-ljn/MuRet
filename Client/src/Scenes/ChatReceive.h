#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/SceneId.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// El chat que llega (CL8 hito 3; 04 § El chat que llega y 06 § El chat): lo que hacen 0x4e2b10 y sus funciones con el
// 00, el 01, el 02, el 0C, el 0D y el F3 30, sobre WorldRuntime. Ninguno mira el estado de la sesión ni el flag de
// cifrado.
namespace Client::Scenes
{
class WorldState;

struct ChatReceiveContext
{
    WorldState& world;
    // El rand() de la CRT: lo sacan los cierres del registro propio de 0x554d20 y de 0x4dd570.
    Render::Models::CrtRandom& random;
    // Con qué se miden los mensajes; sin medidor, los que entran al chat cortan con error.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    // 0x6d5dc8, la escena en curso.
    std::optional<SceneId> scene{};
    // Text.bmd: el 482 del 0C y el 483 del 0D con 2.
    const Data::Text::TextTable* texts = nullptr;
    // 0x6d5edc: el lugar del elegido en la selección, de 0 a 4. Vacío, el −1.
    std::optional<std::uint8_t> chosenSlot{};
};

// 0x4c6070 fuera de la escena 2: el nombre de pkt[3..12] y, según pkt[0xd], el texto al chat de la party (~, tipo 4),
// al aviso del gremio (@@, tipo 6), al del gremio (@, tipo 5), solo a un globo que también va al registro (#,
// 0x557560 con 1) o a un globo y al chat normal (0x557560 con 0 y el tipo 3). Cada rama copia 59, 58, 59, 59 o 60
// bytes a un buffer de 61 en cero.
[[nodiscard]] Core::Expected<void, std::string> ApplyChat(const Protocol::Messages::Chat& chat,
                                                          const ChatReceiveContext& context);

// 0x4e33fc: el personaje de la clave (0x5110d0, el 401 si no hay) y su globo (0x557170 con 0 y sin color). Con el
// héroe en +0x368 = 0x80 y ese personaje llamado "길드 마스터" (0x6c8404) abre la ventana del gremio: corta (CL9.4).
[[nodiscard]] Core::Expected<void, std::string> ApplyKeyChat(const Protocol::Messages::KeyChat& chat,
                                                             const ChatReceiveContext& context);

// 0x4e34ea: con 0x77f3b98 en 0, el emisor a la lista de 0x554d20 (un acceso al registro propio, 3.848 rand() al
// cerrarlo), el sonido 0x26 con 0x77f3b78 y el susurro (0x555190 con el tipo 0) con 60 bytes desde pkt[0xd].
[[nodiscard]] Core::Expected<void, std::string> ApplyWhisper(const Protocol::Messages::Whisper& whisper,
                                                             const ChatReceiveContext& context);

// 0x4e366f: con pkt[3] en 0, el error del susurro (0x555190 con el destino del último, 0x57678c4, el texto 482 y el
// tipo 2).
[[nodiscard]] Core::Expected<void, std::string> ApplyWhisperResult(const Protocol::Messages::WhisperResult& result,
                                                                   const ChatReceiveContext& context);

// 0x4c65b0: 0, un aviso centrado (0x554610 sin resaltar); 1, fuera de la selección, un mensaje de sistema y, en ella,
// el primero va a la franja de la barra (0x63eda0) y los demás al chat; 2, el aviso del gremio (el 483 con el texto,
// resaltado) y su ventana (0x651680, CL9.4: corta); 10 a 15 con texto, el cartel (0x46b720), que en la escena 5 y con
// 1 a 30 repeticiones corta (08, P-C80).
[[nodiscard]] Core::Expected<void, std::string> ApplyNotice(const Protocol::Messages::Notice& notice,
                                                            const ChatReceiveContext& context);

// 0x4dd570: con un acceso al registro propio (3.848 rand() al cerrarlo), las teclas de las skills del elegido; después,
// el ataque automático, el sonido de los avisos, los carteles, Q, W y E, y los renglones y la transparencia del
// registro del chat. Sin elegido, corta antes de abrir el registro (escribiría en +0x16c).
[[nodiscard]] Core::Expected<void, std::string> ApplyOptionData(const Protocol::OptionData::OptionData& data,
                                                                const ChatReceiveContext& context);
} // namespace Client::Scenes
