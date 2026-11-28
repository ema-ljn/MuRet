#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

// CL9 milestone 11, phase 4: the Crywolf altar contract (06 § La interfaz de Crywolf), *static*. GS 0.97 has no
// Crywolf: the oracle is the client's ASM. The window (0x723a44, its two lines and 0x723be0) is filled by 0x435b00,
// from BD 03 (0x4e1010, Events/EventPackets), the click on an altar (0x567b00, inside 0x5650c0) and the window's own
// buttons; 0x436240 draws it (Events/Crywolf/CrywolfInterfaceDrawing).
namespace Client::Events::Crywolf
{

// The messages of 0x435b00: text 1950 + n of Text.bmd ([0x774bf04 + 300 · n]).
inline constexpr std::int32_t ContractRefusedMessage = 2; // the No (0x435ca4)
inline constexpr std::int32_t ContractSignedMessage = 3;  // BD 03 with 1 (0x4e11a3), and the 4 below
inline constexpr std::int32_t ContractSignedSecondMessage = 4;
inline constexpr std::int32_t ContractWithPetMessage = 6;   // the Yes with the pet (0x4361ac)
inline constexpr std::int32_t AltarTakenMessage = 0x34;     // the altar already contracted (0x567c1b)
inline constexpr std::int32_t AltarSpentMessage = 0x35;     // the altar with no contracts (0x567be0)
inline constexpr std::int32_t ContractFailedMessage = 0x36; // BD 03 with 0 and under 350 (0x4e1152), and the 0x37
inline constexpr std::int32_t ContractFailedSecondMessage = 0x37;
inline constexpr std::int32_t AltarOfferMessage = 0x38;    // wsprintfA with 2006 and the contracts left
inline constexpr std::int32_t AltarQuestionMessage = 0x39; // the question, which opens window 1
inline constexpr std::int32_t ContractLevelMessage = 0x3a; // BD 03 with 0 and 350 or more (0x4e117a)

// The arguments of 0x435b00(message, line, value, altar), in that order on the stack.
struct ContractMessage
{
    std::int32_t message = 0;
    // 0 or 1: 0x723a48 + 200 · line.
    std::int32_t line = 0;
    // To 0x723be0: the key of the altar the Yes sends, or 0.
    std::int32_t value = 0;
    // Only with 0x38: the altar of 0x6c73cc, 0 to 4. The others pass −1.
    std::int32_t altar = -1;
};

// 0x435a00 && 0x4303c0: the stage is the ready one or the started one, in Crywolf's map.
[[nodiscard]] bool InTheCrywolfDefense(const Kernel::WorldRuntime& runtime);

// 0x435b00: with the stage at 3 or 4 (0x435a00), in map 34 (0x4303c0) and without the requested exit (0x576793c). With
// 0x38, wsprintfA(line, text 2006, altar & 0xf) and window 1; with another, strcpy(line, text 1950 + n) and window 2,
// which 0x39 turns into 1. Line 0 empties line 1; at the end, 0x723be0 = value. Outside its guard it does nothing.
// Stops without the text, with a line or an altar outside its tables, or with a text of 200 bytes or more, which would
// overwrite what follows the line (the strcpy and wsprintfA have no bound).
[[nodiscard]] Core::Expected<void, std::string>
ShowTheContractMessage(Kernel::WorldRuntime& runtime, const Tables::TextTable* texts, const ContractMessage& message);

// 0x435ad0(altar, value): with the stage at 3 or 4 in map 34, the byte at 0x6c73cc + altar; ignores the requested exit.
// The altar comes from the BD 03 key − 0x13c with no bound (Q-C1347): inside the guard and outside 0..4, the original
// writes into the neighbouring .data; MuRet does not write and returns false, and BD 03 drops the rest.
[[nodiscard]] bool SetTheAltar(Kernel::WorldRuntime& runtime, std::int32_t altar, std::uint8_t value);

struct ContractWindowContext
{
    Kernel::WorldRuntime& runtime;
    const Tables::TextTable* texts = nullptr;
    // The hero's word +0x318 (0x435cf7): with 0x1c14, 0x1c15 or 0x1c37 the Yes sends nothing.
    std::uint16_t heroPet = 0;
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // The step's send(), the one of C1 06 BD 03. Empty, the Yes stops after 0x723bdc.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x435c5c..0x436220, the contract window step inside 0x435bd0 (already in map 34), with the left button down
// (0x7d36874) and without each box's edges:
//   - window 1: the No at (0x14a, 0x180) × (0xfa, 0x118), 0x723bdc at 1 and 0x435b00(2); then the Yes at (0xfa,
//     0x130) × (0xfa, 0x118): with the pet, 0x435b00(6); otherwise 0x723bdc at 2 and the C1 06 BD 03 of 0x723be0. With
//     the button down, anywhere, it clears it with the edge (0x7d366d4): it keeps every click;
//   - window 2: the OK at (0x122, 0x158) × (0xfa, 0x118) clears the button and the edge and leaves 0x723bdc at 3.
// No rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheContractWindow(const ContractWindowContext& context);

// Where 0x5650c0 goes after the click on an altar.
enum class AltarClick : std::uint8_t
{
    // 0x5688ca (without stage 3 or 4 in 34) and 0x568288 (the target's type outside 0x204..0x208).
    NotAnAltar,
    // 0x567c2d: the hero is not an elf (+0x1d4 & 7 other than 2): the C1 30 to talk to the target.
    Talk,
    // 0x435b00 with what it hit; 0x5650c0 goes on at 0x569573 (0x6eeda8 and 0x6c8e54 at −1).
    Contract,
};

// 0x567b00..0x567c23, the arrival of 0x5650c0 at target 0x6c8e54 with an altar (types 0x204..0x208, altar 0..4): with
// an elf, the contracted altar (0x4365a0, high nibble at 1) gives 0x435b00(0x34); with no contracts (low nibble at 0),
// 0x435b00(0x35); with contracts, 0x435b00(0x38, 0, key, altar) and 0x435b00(0x39, 1, key, −1), with the target's
// signed key +0x1f4. Called by the arrival at an NPC with Crywolf active (0x5650c0 with +0x379 at 2,
// Npc::TalkToTheNpc), before the C1 30.
[[nodiscard]] Core::Expected<AltarClick, std::string> ClickTheCrywolfAltar(Kernel::WorldRuntime& runtime,
                                                                           const Tables::TextTable* texts,
                                                                           const Kernel::ActorState& hero,
                                                                           const Kernel::ActorState& target);
} // namespace Client::Events::Crywolf
