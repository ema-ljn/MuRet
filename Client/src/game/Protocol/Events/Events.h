#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

// The events (04, "Los eventos"): the Devil Square, Blood Castle, Chaos Castle, Kalima, Crywolf and chip packets that
// the dispatcher 0x4e2b10 reads, as the ASM reads them. Plain formats: the scene (Scenes/EventPackets) applies them.
// Each reader asks for the bytes the original always reads; the ones it reads depending on a field are checked when
// applied.
namespace Client::Protocol::Events
{
// C→S: 0x5e65f6, the C1 04 98 01 of the button of the window of 0x78abb00 with the 4 (the GS answers it in
// CGEventChip98Request and does not read the 01).
inline constexpr std::uint8_t MutoNumberRequestByte = 0x01;

// 0x4e3594: C1 05 0B state event (PMSG_EVENTSTATE).
struct EventState
{
    std::uint8_t state = 0;
    std::uint8_t event = 0;
};

// 0x4dab90: the cell of a type 1, (x, y) from pkt[7].
struct AttributeCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// 0x4dab90: the rectangle of a type 0, (x1, y1, x2, y2) from pkt[7].
struct AttributeArea
{
    std::uint8_t x1 = 0;
    std::uint8_t y1 = 0;
    std::uint8_t x2 = 0;
    std::uint8_t y2 = 0;
};

// 0x4dab90: C1 46 type attribute mode count, and the count of cells (type 1) or of rectangles (type 0). Another type
// reads nothing else.
struct MapAttributes
{
    std::uint8_t type = 0;
    std::uint8_t attribute = 0;
    std::uint8_t mode = 0;
    std::uint8_t count = 0;
    std::vector<AttributeCell> cells{};
    std::vector<AttributeArea> areas{};
};

// 0x4d6990: the big-endian key of pkt[3..4], the item's dword at pkt[8..0xb] and pkt[0xc].
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

// 0x4da8c0: pkt[3] and, depending on it, pkt[4] (1 and 2) or pkt[4..6] (4). The ones it does not read stay 0.
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

// 0x4e4959: pkt[3] (the own rank), pkt[4] (the count) and what follows from pkt[5], which the method +0xc of the
// event's object copies 24 bytes at a time (PMSG_DEVILSQUARERESULT).
struct EventResult
{
    std::uint8_t rank = 0;
    std::uint8_t count = 0;
    std::vector<std::byte> scores{};
};

// 0x4dd8a0: pkt[3], the word of pkt[4] and, if it carries them, the three of pkt[6], pkt[8] and pkt[0xa]
// (PMSG_EVENTCHIPINFO).
struct EventChipInfo
{
    std::uint8_t type = 0;
    std::uint16_t count = 0;
    std::optional<std::array<std::uint16_t, 3>> muto{};
};

// 0x4e49d4: the word of pkt[4].
struct EventChipCount
{
    std::uint16_t count = 0;
};

// 0x4e49fb: the words of pkt[4], pkt[6] and pkt[8].
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

// 0x4e4999 → method +8 of the event's object: pkt[3], the words of pkt[4], pkt[6], pkt[8] and pkt[0xa], and pkt[0xc]
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

// 0x4e4a86: pkt[3] and the 0x40 bytes from pkt[4].
struct SerialResult
{
    std::uint8_t result = 0;
    std::array<std::byte, 0x40> data{};
};

// 0x4e4ad4: the word of pkt[4].
struct EventSound
{
    std::uint16_t sound = 0;
};

// 0x4daa90: pkt[4] of the AF 01.
struct ChaosCastleEntryResult
{
    std::uint8_t result = 0;
};

// 0x4e4547 (BC 00) and 0x4da540 (BC 01): pkt[3] and pkt[4].
struct JewelMixResult
{
    Network::Subcode subcode{};
    std::uint8_t result = 0;
};

// 0x4e5941 (BD 00): pkt[4], the state, and pkt[5], the stage.
struct CrywolfState
{
    std::uint8_t state = 0;
    std::uint8_t stage = 0;
};

// The values of CrywolfState::stage (0x723c05), named after the later source's CRYWOLF_STATE_TYPE by value.
inline constexpr std::uint8_t CrywolfNoStage = 0;
inline constexpr std::uint8_t CrywolfFirstNoticeStage = 1;
inline constexpr std::uint8_t CrywolfSecondNoticeStage = 2;
inline constexpr std::uint8_t CrywolfReadyStage = 3;
inline constexpr std::uint8_t CrywolfStartStage = 4;
inline constexpr std::uint8_t CrywolfEndStage = 5;
inline constexpr std::uint8_t CrywolfEndCycleStage = 6;

// The values of CrywolfState::state (0x723c04), named after the later source's CRYWOLF_OCCUPATION_STATE_TYPE.
inline constexpr std::uint8_t CrywolfPeaceState = 0;
inline constexpr std::uint8_t CrywolfOccupiedState = 1;
inline constexpr std::uint8_t CrywolfWarState = 2;

// 0x435a00: the stage is the ready one or the started one.
[[nodiscard]] constexpr bool CrywolfIsReadyOrStarted(std::uint8_t stage)
{
    return stage == CrywolfReadyStage || stage == CrywolfStartStage;
}

// 0x4e5990 → 0x4e0fe0 (BD 02): 0x430000(dword of pkt+4, pkt[8], pkt[9], pkt[0xa], pkt[0xb], pkt[0xc]), the light
// (0x723c08) and the five altars (0x6c73cc..0x6c73d0).
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

// 0x4e59eb (BD 05): 0x435a70(dword of pkt+4, pkt[8]).
struct CrywolfBoss
{
    std::int32_t life = 0;
    std::uint8_t count = 0;
};

// 0x4e5a09 (BD 07): 0x437600(pkt[4], dword of pkt+8).
struct CrywolfRank
{
    std::uint8_t rank = 0;
    std::int32_t experience = 0;
};

// A 0x14-byte record of the BD 08 from pkt+5 (0x4e1695..0x4e16a9): the place at +0, the name at +1 (10 bytes), the
// points in the dword +0xc and the class at +0x10.
struct CrywolfRankingEntry
{
    std::uint8_t place = 0;
    std::array<char, 10> name{};
    std::int32_t score = 0;
    std::uint8_t classByte = 0;
};

// 0x4e5a39 → 0x4e1680 (BD 08): pkt[4] records, each one to 0x437620.
struct CrywolfRanking
{
    std::vector<CrywolfRankingEntry> entries;
};

// 0x4e5a25 (BD 09): pkt[4] to 0x7d2dc9d.
struct CrywolfChaosBox
{
    std::uint8_t value = 0;
};

// 0x4e59a5 → 0x4e1010 (BD 03), the altars' contract: the result of pkt[4] (0 or 1; another does nothing), the altar's
// value of pkt[5] (only with 1) and the big-endian key of pkt[6] and pkt[7] (0x4e1034..0x4e1046), the altar's in the
// viewport (0x13c + altar).
struct CrywolfContract
{
    std::uint8_t result = 0;
    std::uint8_t value = 0;
    std::uint16_t key = 0;
};

// 0x4e59d6 → 0x4e15f0 (BD 0C): what MuRet does not port (the effects of 0x430040, CL11), with the whole packet.
struct CrywolfUnported
{
    std::uint8_t subcode = 0;
    std::vector<std::byte> bytes;
};

// 0x4e31f8 (F3 22): the byte of pkt[4] and the word of pkt[6].
struct EventTime
{
    std::uint8_t value4 = 0;
    std::uint16_t value6 = 0;
};

// 0x4d9f00 (F3 23): the 8 bytes of pkt[4] and of pkt[0xd], and the points of pkt[0xc] and pkt[0x15].
struct SoccerScore
{
    std::array<char, 8> firstName{};
    std::uint8_t firstScore = 0;
    std::array<char, 8> secondName{};
    std::uint8_t secondScore = 0;
};

// F3 24, 0x4deef0: with present < 3 it copies the 10-byte names and the word scores; another present reads nothing
// else.
struct GuildWarOutcome
{
    std::uint8_t present = 0;
    std::array<char, 10> firstName{};
    std::array<char, 10> secondName{};
    std::uint16_t firstScore = 0;
    std::uint16_t secondScore = 0;
};

// 0x4da070 (F3 40): pkt[4], pkt[5] and pkt[6] (PMSG_F3_40).
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
                 CrywolfAltars, CrywolfContract, CrywolfTimer, CrywolfBoss, CrywolfRank, CrywolfRanking,
                 CrywolfChaosBox, CrywolfUnported, EventTime, SoccerScore, GuildWarOutcome, ServerCommand>;

// One of the packets above by its opcode (and its subcode in AF, BC, BD and F3). A type other than C1, another opcode
// or another subcode → WrongPacket; without the bytes the original always reads → TooShort. Extra bytes are ignored.
[[nodiscard]] Core::Expected<EventPacket, Network::ProtocolError> ReadEventPacket(const Network::Packet& packet);

// 0x56b4e8..0x56b5a0: C1 03 97 in the clear, without XOR32 or encryption, with a direct send.
[[nodiscard]] Outgoing WriteEventWindowLeave();

// 0x5c6865/0x5c6c36/0x5c7010: C1 04 91 kind, with the XOR32 and sent in the clear.
[[nodiscard]] Outgoing WriteEventTimeRequest(std::uint8_t kind);

// 0x5e65da..0x5e6864: C1 04 98 01, with the XOR32 and in the clear (the step's send).
[[nodiscard]] Outgoing WriteMutoNumberRequest();

// 0x5e68ed..0x5e6c94: C1 05 95 type slot, the type (0x78abb00 − 1, in a byte) and the inventory slot of the Rena or
// the stone (0x1c15; from 0 to 63, with nothing added: the GS adds 12), with the XOR32 and in the clear
// (PMSG_REQ_EVENTCHIP; the GS accepts the types 0 and 1).
[[nodiscard]] Outgoing WriteEventChipRegister(std::uint8_t type, std::uint8_t itemPlace);

// 0x5e6db9..0x5e723b: C1 12 9D with the three pieces of the serial number, each of 4 bytes and a 0, with the XOR32 and
// in the clear, with the opcode of the 9D that arrives (SerialResultOpcode). The GS 0.97 does not answer it: the oracle
// is the ASM.
inline constexpr std::size_t SerialPieceCount = 3;
inline constexpr std::size_t SerialPieceBytes = 4;
using SerialPieces = std::array<std::array<std::uint8_t, SerialPieceBytes>, SerialPieceCount>;
[[nodiscard]] Outgoing WriteSerialNumber(const SerialPieces& pieces);

// 0x4fbeed..0x4fc2c9: C1 06 AF 02 x y, with the XOR32 and in the clear (the step's send, without C3).
// 0x4fbe10 writes the 02 with MOV CL,0x2 at 0x4fbf6a: it is not the AF 01.
[[nodiscard]] Outgoing WriteChaosCastleReposition(std::uint8_t x, std::uint8_t y);

// 0x435d1c..0x4361a2: C1 06 BD 03 high low, the Yes button of the Crywolf contract window with 0x723be0 (the altar's
// key that 0x435b00 left): the byte of SAR 8 and the low one, with the XOR32 and in the clear (the step's send). GS
// 0.97 has no Crywolf: the oracle is the ASM.
[[nodiscard]] Outgoing WriteCrywolfContract(std::int32_t key);

// 0x5e5b2f..0x5e5d7e: C1 05 90 square place, the square from 0 to 5 and the invitation's place (the slot + 0x18), with
// the XOR32 and in the clear (PMSG_REQ_ENTER_DEVILSQUARE; the GS subtracts 12 and takes pInventory, which starts with
// the equipment).
[[nodiscard]] Outgoing WriteDevilSquareEntry(std::uint8_t square, std::uint8_t itemPlace);

// 0x5e6156..0x5e62cb: C1 05 9A bridge place, the bridge from 1 to 7 and the cloak's (the slot + 12), with the XOR32
// and in the clear (PMSG_REQ_BLOODCASTLE_ENTER; the GS subtracts 1 from the bridge).
[[nodiscard]] Outgoing WriteBloodCastleEntry(std::uint8_t bridge, std::uint8_t itemPlace);
} // namespace Client::Protocol::Events
