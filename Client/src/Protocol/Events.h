#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

// 04 § Los eventos (CL9 hito 11): los paquetes de Devil Square, Blood Castle, Chaos Castle, Kalima, Crywolf y los
// chips que lee el dispatcher 0x4e2b10, como los lee el ASM. Formatos puros: la escena (Scenes/EventPackets) aplica.
// Cada lector pide los bytes que el original lee siempre; los que lee según un campo se miran al aplicar.
namespace Client::Protocol::Events
{
inline constexpr std::uint8_t EventStateOpcode = 0x0b;
inline constexpr std::uint8_t MapAttributesOpcode = 0x46;
inline constexpr std::uint8_t ItemAcquiredOpcode = 0x47;
inline constexpr std::uint8_t DevilSquareEntryOpcode = 0x90;
inline constexpr std::uint8_t EventOpenTimeOpcode = 0x91;
inline constexpr std::uint8_t EventCountdownOpcode = 0x92;
inline constexpr std::uint8_t EventResultOpcode = 0x93;
inline constexpr std::uint8_t EventChipInfoOpcode = 0x94;
inline constexpr std::uint8_t EventChipCountOpcode = 0x95;
inline constexpr std::uint8_t MutoNumbersOpcode = 0x96;
// C→S: 0x56b4e8, el C1 03 97 de SendMove con 0x78abb00 (el GS lo atiende en CGEventChip97Request).
inline constexpr std::uint8_t EventWindowLeaveOpcode = 0x97;
inline constexpr std::uint8_t EventDialogOpcode = 0x99;
inline constexpr std::uint8_t BloodCastleEntryOpcode = 0x9a;
inline constexpr std::uint8_t EventPlayStateOpcode = 0x9b;
inline constexpr std::uint8_t SerialResultOpcode = 0x9d;
inline constexpr std::uint8_t EventSoundOpcode = 0x9e;
inline constexpr std::uint8_t ChaosCastleOpcode = 0xaf;
// 0x4e5032: el dispatcher solo lee el AF con pkt[3] en 1.
inline constexpr std::uint8_t ChaosCastleEntrySubcode = 0x01;
// C→S: 0x4fbee8, el C1 06 AF 02 x y de 0x4fbe10 (en el original, MOV CL,0x2 en 0x4fbf6a: no es el AF 01).
inline constexpr std::uint8_t ChaosCastleRepositionSubcode = 0x02;
inline constexpr std::uint8_t JewelMixOpcode = 0xbc;
inline constexpr std::uint8_t JewelMixSubcode = 0x00;
inline constexpr std::uint8_t JewelUnmixSubcode = 0x01;
inline constexpr std::uint8_t CrywolfOpcode = 0xbd;
inline constexpr std::uint8_t CrywolfStateSubcode = 0x00;
// 0x4e620c, la tabla del BD por pkt[3] (0 a 0xc): el 1, el 6, el 0xa y el 0xb no hacen nada (0x4e5b4a).
inline constexpr std::uint8_t CrywolfAltarsSubcode = 0x02;
inline constexpr std::uint8_t CrywolfContractSubcode = 0x03;
inline constexpr std::uint8_t CrywolfTimerSubcode = 0x04;
inline constexpr std::uint8_t CrywolfBossSubcode = 0x05;
inline constexpr std::uint8_t CrywolfRankSubcode = 0x07;
inline constexpr std::uint8_t CrywolfRankingSubcode = 0x08;
inline constexpr std::uint8_t CrywolfChaosBoxSubcode = 0x09;
inline constexpr std::uint8_t CrywolfEffectSubcode = 0x0c;
inline constexpr std::uint8_t WorldOpcode = 0xf3;
inline constexpr std::uint8_t EventTimeSubcode = 0x22;
inline constexpr std::uint8_t SoccerScoreSubcode = 0x23;
inline constexpr std::uint8_t ServerCommandSubcode = 0x40;

// 0x4e3594: C1 05 0B estado evento (PMSG_EVENTSTATE).
struct EventState
{
    std::uint8_t state = 0;
    std::uint8_t event = 0;
};

// 0x4dab90: la celda de un tipo 1, (x, y) desde pkt[7].
struct AttributeCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// 0x4dab90: el rectángulo de un tipo 0, (x1, y1, x2, y2) desde pkt[7].
struct AttributeArea
{
    std::uint8_t x1 = 0;
    std::uint8_t y1 = 0;
    std::uint8_t x2 = 0;
    std::uint8_t y2 = 0;
};

// 0x4dab90: C1 46 tipo atributo modo cuenta, y la cuenta de celdas (tipo 1) o de rectángulos (tipo 0). Otro tipo no
// lee nada más.
struct MapAttributes
{
    std::uint8_t type = 0;
    std::uint8_t attribute = 0;
    std::uint8_t mode = 0;
    std::uint8_t count = 0;
    std::vector<AttributeCell> cells{};
    std::vector<AttributeArea> areas{};
};

// 0x4d6990: la clave big-endian de pkt[3..4], el dword del ítem en pkt[8..0xb] y pkt[0xc].
struct ItemAcquired
{
    std::uint16_t key = 0;
    std::uint32_t item = 0;
    std::uint8_t level = 0;
};

// 0x4e4870: pkt[3].
struct DevilSquareEntryResult
{
    std::uint8_t result = 0;
};

// 0x4da8c0: pkt[3] y, según él, pkt[4] (1 y 2) o pkt[4..6] (4). Los que no lee quedan en 0.
struct EventOpenTime
{
    std::uint8_t kind = 0;
    std::uint8_t value4 = 0;
    std::uint8_t value5 = 0;
    std::uint8_t value6 = 0;
};

// 0x4e4930: pkt[3].
struct EventCountdown
{
    std::uint8_t type = 0;
};

// 0x4e4959: pkt[3] (el puesto propio), pkt[4] (la cuenta) y lo que sigue desde pkt[5], que el método +0xc del
// objeto del evento copia de a 24 bytes (PMSG_DEVILSQUARERESULT).
struct EventResult
{
    std::uint8_t rank = 0;
    std::uint8_t count = 0;
    std::vector<std::byte> scores{};
};

// 0x4dd8a0: pkt[3], el word de pkt[4] y, si los trae, los tres de pkt[6], pkt[8] y pkt[0xa] (PMSG_EVENTCHIPINFO).
struct EventChipInfo
{
    std::uint8_t type = 0;
    std::uint16_t count = 0;
    std::optional<std::array<std::uint16_t, 3>> muto{};
};

// 0x4e49d4: el word de pkt[4].
struct EventChipCount
{
    std::uint16_t count = 0;
};

// 0x4e49fb: los words de pkt[4], pkt[6] y pkt[8].
struct MutoNumbers
{
    std::array<std::uint16_t, 3> numbers{};
};

// 0x4e4a40: pkt[3].
struct EventDialog
{
    std::uint8_t kind = 0;
};

// 0x4da780: pkt[3].
struct BloodCastleEntryResult
{
    std::uint8_t result = 0;
};

// 0x4e4999 → método +8 del objeto del evento: pkt[3], los words de pkt[4], pkt[6], pkt[8] y pkt[0xa], y pkt[0xc]
// (PMSG_STATEBLOODCASTLE).
struct EventPlayState
{
    std::uint8_t state = 0;
    std::uint16_t remaining = 0;
    std::uint16_t maxKill = 0;
    std::uint16_t currentKill = 0;
    std::uint16_t weaponOwner = 0;
    std::uint8_t weapon = 0;
};

// 0x4e4a86: pkt[3] y los 0x40 bytes desde pkt[4].
struct SerialResult
{
    std::uint8_t result = 0;
    std::array<std::byte, 0x40> data{};
};

// 0x4e4ad4: el word de pkt[4].
struct EventSound
{
    std::uint16_t sound = 0;
};

// 0x4daa90: pkt[4] del AF 01.
struct ChaosCastleEntryResult
{
    std::uint8_t result = 0;
};

// 0x4e4547 (BC 00) y 0x4da540 (BC 01): pkt[3] y pkt[4].
struct JewelMixResult
{
    std::uint8_t subcode = 0;
    std::uint8_t result = 0;
};

// 0x4e5941 (BD 00): pkt[4], el estado, y pkt[5], la etapa.
struct CrywolfState
{
    std::uint8_t state = 0;
    std::uint8_t stage = 0;
};

// 0x4e5990 → 0x4e0fe0 (BD 02): 0x430000(dword de pkt+4, pkt[8], pkt[9], pkt[0xa], pkt[0xb], pkt[0xc]), la luz
// (0x723c08) y los cinco altares (0x6c73cc..0x6c73d0).
struct CrywolfAltars
{
    std::int32_t light = 0;
    std::array<std::uint8_t, 5> altars{};
};

// 0x4e59ba (BD 04): 0x435a20(pkt[4], pkt[5]).
struct CrywolfTimer
{
    std::uint8_t value4 = 0;
    std::uint8_t minutes = 0;
};

// 0x4e59eb (BD 05): 0x435a70(dword de pkt+4, pkt[8]).
struct CrywolfBoss
{
    std::int32_t life = 0;
    std::uint8_t count = 0;
};

// 0x4e5a09 (BD 07): 0x437600(pkt[4], dword de pkt+8).
struct CrywolfRank
{
    std::uint8_t rank = 0;
    std::int32_t experience = 0;
};

// Un registro de 0x14 bytes del BD 08 desde pkt+5 (0x4e1695..0x4e16a9): el lugar en +0, el nombre en +1 (10 bytes),
// los puntos en el dword +0xc y la clase en +0x10.
struct CrywolfRankingEntry
{
    std::uint8_t place = 0;
    std::array<char, 10> name{};
    std::int32_t score = 0;
    std::uint8_t classByte = 0;
};

// 0x4e5a39 → 0x4e1680 (BD 08): pkt[4] registros, cada uno a 0x437620.
struct CrywolfRanking
{
    std::vector<CrywolfRankingEntry> entries;
};

// 0x4e5a25 (BD 09): pkt[4] a 0x7d2dc9d.
struct CrywolfChaosBox
{
    std::uint8_t value = 0;
};

// 0x4e59a5 → 0x4e1010 (BD 03) y 0x4e59d6 → 0x4e15f0 (BD 0C): lo que MuRet no porta, con el paquete entero.
struct CrywolfUnported
{
    std::uint8_t subcode = 0;
    std::vector<std::byte> bytes;
};

// 0x4e31f8 (F3 22): el byte de pkt[4] y el word de pkt[6].
struct EventTime
{
    std::uint8_t value4 = 0;
    std::uint16_t value6 = 0;
};

// 0x4d9f00 (F3 23): los 8 bytes de pkt[4] y de pkt[0xd], y los puntos de pkt[0xc] y pkt[0x15].
struct SoccerScore
{
    std::array<char, 8> firstName{};
    std::uint8_t firstScore = 0;
    std::array<char, 8> secondName{};
    std::uint8_t secondScore = 0;
};

// 0x4da070 (F3 40): pkt[4], pkt[5] y pkt[6] (PMSG_F3_40).
struct ServerCommand
{
    std::uint8_t command = 0;
    std::uint8_t value5 = 0;
    std::uint8_t value6 = 0;
};

using EventPacket =
    std::variant<EventState, MapAttributes, ItemAcquired, DevilSquareEntryResult, EventOpenTime, EventCountdown,
                 EventResult, EventChipInfo, EventChipCount, MutoNumbers, EventDialog, BloodCastleEntryResult,
                 EventPlayState, SerialResult, EventSound, ChaosCastleEntryResult, JewelMixResult, CrywolfState,
                 CrywolfAltars, CrywolfTimer, CrywolfBoss, CrywolfRank, CrywolfRanking, CrywolfChaosBox,
                 CrywolfUnported, EventTime, SoccerScore, ServerCommand>;

// Uno de los paquetes de arriba por su opcode (y su subcódigo en AF, BC, BD y F3). Otro tipo que C1, otro opcode u
// otro subcódigo → WrongPacket; sin los bytes que el original lee siempre → TooShort. Los bytes de más se ignoran.
[[nodiscard]] Core::Expected<EventPacket, ProtocolError> ReadEventPacket(const Network::Packet& packet);

// 0x56b4e8..0x56b5a0: C1 03 97 en claro, sin XOR32 ni cifrado, con send directo.
[[nodiscard]] Outgoing WriteEventWindowLeave();

// 0x4fbeed..0x4fc2c9: C1 06 AF 02 x y, con el XOR32 y en claro (el send del paso, sin C3).
[[nodiscard]] Outgoing WriteChaosCastleReposition(std::uint8_t x, std::uint8_t y);

// 0x5e5b2f..0x5e5d7e: C1 05 90 cuadrado lugar, el cuadrado de 0 a 5 y el lugar de la invitación (la casilla + 0x18),
// con el XOR32 y en claro (PMSG_REQ_ENTER_DEVILSQUARE; el GS le resta 12 y toma pInventory, que empieza con el equipo).
[[nodiscard]] Outgoing WriteDevilSquareEntry(std::uint8_t square, std::uint8_t itemPlace);

// 0x5e6156..0x5e62cb: C1 05 9A puente lugar, el puente de 1 a 7 y el de la capa (la casilla + 12), con el XOR32 y en
// claro (PMSG_REQ_BLOODCASTLE_ENTER; el GS le resta 1 al puente).
[[nodiscard]] Outgoing WriteBloodCastleEntry(std::uint8_t bridge, std::uint8_t itemPlace);
} // namespace Client::Protocol::Events
