#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <vector>

// 04 § Las misiones y los NPC (CL9 hito 2): los 9F y A0..A3 que llegan, y el A0, el A2 y el 30 que salen. Los
// handlers del original (0x4ddaa0, 0x4ddad0, 0x4ddb00, 0x4ddb40 y 0x4ddb80, desde 0x4e2b10) leen del buffer sin mirar
// el largo; acá un paquete corto es TooShort y la sesión lo ignora con un aviso, como los demás.
namespace Client::Protocol::Quest
{
inline constexpr std::uint8_t QuestFlagOpcode = 0x9f;
inline constexpr std::uint8_t QuestListOpcode = 0xa0;
inline constexpr std::uint8_t QuestStateOpcode = 0xa1;
inline constexpr std::uint8_t SetQuestOpcode = 0xa2;
inline constexpr std::uint8_t QuestResultOpcode = 0xa3;
inline constexpr std::uint8_t TalkOpcode = 0x30;

// C1 05 9F índice valor (0x4e4b3a → 0x4ddaa0): con índice de 0 a 3, el valor va a +6 + índice de [0x710148].
struct QuestFlag
{
    std::uint8_t index = 0;
    std::uint8_t value = 0;
};

// C1 xx A0 cuenta estados... (0x4ddad0): `states` son los bytes que trae el paquete después de la cuenta, tal cual.
struct QuestList
{
    std::uint8_t count = 0;
    std::vector<std::uint8_t> states;
};

// C1 05 A1 índice estado (0x4ddb00): el estado es el byte entero de las cuatro misiones (GetQuestStateBYTE del GS).
struct QuestStateChange
{
    std::uint8_t index = 0;
    std::uint8_t state = 0;
};

// C1 06 A2 índice resultado estado (0x4ddb40): la respuesta al A2 que manda la ventana; con resultado 0, como el A1.
struct SetQuestResult
{
    std::uint8_t index = 0;
    std::uint8_t result = 0;
    std::uint8_t state = 0;
};

// C1 07 A3 claveH claveL tipo valor (0x4ddb80): el premio de una misión a un personaje del viewport.
inline constexpr std::uint8_t PointsPrize = 0xc8;
inline constexpr std::uint8_t ClassChangePrize = 0xc9;
inline constexpr std::uint8_t ClassPointsPrize = 0xca;
inline constexpr std::uint8_t EffectPrize = 0xcb;
struct QuestResult
{
    // (pkt[3] · 0x100 + pkt[4]) & 0x7fff, como lo busca 0x5110d0.
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
    std::uint8_t value = 0;
};

// Tipo u opcode distintos → WrongPacket; campos incompletos → TooShort. Los bytes que sobran se ignoran (salvo los
// estados del A0, que se guardan todos).
[[nodiscard]] Core::Expected<QuestFlag, ProtocolError> ReadQuestFlag(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestList, ProtocolError> ReadQuestList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestStateChange, ProtocolError> ReadQuestState(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SetQuestResult, ProtocolError> ReadSetQuestResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestResult, ProtocolError> ReadQuestResult(const Network::Packet& packet);

// 0x63693a y 0x5676a6: C1 03 A0, con el XOR32 y Delivery::Encrypted (C3). El relleno y el serial sacan sus rand() en
// quien lo pide.
[[nodiscard]] Outgoing WriteQuestListRequest();
// 0x419d13 y 0x41a35b: C1 05 A2 misión 1, con el XOR32 y entrega C3.
[[nodiscard]] Outgoing WriteSetQuest(std::uint8_t quest, std::uint8_t state);
// 0x567c2d..: C1 05 30 claveH claveL (+0x1f4 del NPC, el byte alto con SAR), con el XOR32 y entrega C3.
[[nodiscard]] Outgoing WriteTalk(std::int16_t key);
} // namespace Client::Protocol::Quest
