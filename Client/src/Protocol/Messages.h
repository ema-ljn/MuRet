#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// 04 § El chat que llega (CL8 hito 3): los mensajes del GS que reparte 0x4e2b10, sin aplicar nada. El original lee
// sus campos a desplazamientos fijos sin mirar el largo del paquete; acá los textos guardan los bytes crudos que el
// original copia, pero solo los que llegaron (Q-C510): lo que quede afuera del paquete es como un '\0', también en el
// nombre del 00 y del 02. Tipo u opcode distintos → WrongPacket; sin los campos fijos del 01, el 0C o el 0D →
// TooShort. Los bytes sobrantes se ignoran.
namespace Client::Protocol::Messages
{
inline constexpr std::uint8_t ChatOpcode = 0x00;
inline constexpr std::uint8_t KeyChatOpcode = 0x01;
inline constexpr std::uint8_t WhisperOpcode = 0x02;
inline constexpr std::uint8_t WhisperResultOpcode = 0x0c;
inline constexpr std::uint8_t NoticeOpcode = 0x0d;

// pkt[3..12]: el nombre de quien habla. Con los diez llenos no trae '\0'; 0x4c6070 y 0x4e34ea le agregan uno.
inline constexpr std::size_t SpeakerNameSize = 10;
// 0x4c6070 y 0x4e34ea: los textos van a un buffer de 61 bytes en cero, y la copia más larga es de 60 desde pkt[0xd].
inline constexpr std::size_t SpokenTextSize = 60;
// pkt[0xd]: donde empiezan el texto del 00, el del 02 y el del 0D.
inline constexpr std::size_t SpokenTextOffset = 0xd;

// 00 (0x4c6070) y 02 (0x4e34ea): el nombre y hasta 60 bytes crudos desde pkt[0xd], con sus '\0'.
struct SpokenText
{
    std::array<std::byte, SpeakerNameSize> name{};
    std::string text;
};

struct Chat : SpokenText
{
};

struct Whisper : SpokenText
{
};

// 01 (0x4e3509): la clave del personaje en pkt[3] (alto) y pkt[4] (bajo), y el texto desde pkt[5]. 0x557170 lo lee
// en el paquete mismo, con strlen y strcpy: acá, los bytes crudos hasta el final del paquete.
struct KeyChat
{
    std::uint16_t key = 0;
    std::string text;
};

// 0C (0x4e366f): pkt[3]. Solo el 0 hace algo.
struct WhisperResult
{
    std::uint8_t result = 0;
};

// 0D (0x4c65b0): el tipo en pkt[3] y, para el cartel (10 a 15), pkt[4], el WORD de pkt[6], el DWORD de pkt[8] y la
// velocidad de pkt[0xc]. pkt[5] no se lee. El texto, crudo desde pkt[0xd] hasta el final del paquete.
struct Notice
{
    std::uint8_t type = 0;
    std::uint8_t byte4 = 0;
    std::uint16_t word6 = 0;
    std::uint32_t color = 0;
    std::uint8_t speed = 0;
    std::string text;
};

[[nodiscard]] Core::Expected<Chat, ProtocolError> ReadChat(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Whisper, ProtocolError> ReadWhisper(const Network::Packet& packet);
[[nodiscard]] Core::Expected<KeyChat, ProtocolError> ReadKeyChat(const Network::Packet& packet);
[[nodiscard]] Core::Expected<WhisperResult, ProtocolError> ReadWhisperResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Notice, ProtocolError> ReadNotice(const Network::Packet& packet);
} // namespace Client::Protocol::Messages
