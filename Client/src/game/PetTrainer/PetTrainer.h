#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Npc/NpcWindows.h"

#include <cstddef>
#include <cstdint>
#include <string>

// The Dark Lord's pet trainer (CL9 milestone 6; 06 § HUD y ventanas, "El entrenador"): panel 0x8000, its mode
// (0x724580, Ui::DelegatedPanels::trainer) and its box (0x723c80, NpcWindowState::trainerBox). 0x44b4e0's step
// dispatches by mode: 1 the menu (0x44b540), 2 recovering the life of the dark horse (0x1a04, equipment slot 8) or the
// raven (0x1a05, slot 1) with the 34 (0x44b910), and 3 the box, which mixes the spirit of one of the two with jewels
// (0x44d010, with 0x44ae10). The drawing of the three modes is PetTrainer/PetTrainerDrawing.h.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Ui
{
struct GameUiContext;
} // namespace Client::Ui

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::PetTrainer
{

// 0x724580
inline constexpr std::int32_t TrainerMenuMode = 1;
inline constexpr std::int32_t TrainerRecoveryMode = 2;
inline constexpr std::int32_t TrainerBoxMode = 3;

// 0x449940(mode): the mode in 0x724580 and the box empty (0x449c40). 0x6599a0(0x8000) calls it with 1 (0x65be8b).
void OpenTheTrainer(Kernel::WorldState& world, std::int32_t mode);

// 0x449c10: whether any box cell has a type (other than 0xffff), without looking at +0x3c.
[[nodiscard]] bool TrainerBoxHasItems(const Kernel::WorldState& world);

// 0x4499b0: with something in the box, error text 1211 (0x555190(0x6e57a4, .., 2)) and false; otherwise 0x724584 and
// 0x724580 at 0 and the C1 03 31 in the clear through `send`, and true. Without `services` or its message, the
// warning fails.
[[nodiscard]] Core::Expected<bool, std::string>
CloseTheTrainer(Kernel::WorldState& world, const Items::InventoryServices* services, const Npc::NpcWindowSender& send);

// 0x44ae10: 0x724584 and 0x724585 at 0 and the box's mix (the cells with a type and +0x3c other than 0): the horse's
// spirit (0x1a1f of level 0) with 5 bless jewels (0x1c0d), 5 soul (0x1c0e), one creation (0x1c16) and one chaos
// (0x180f) is 1, and the raven's (level 1) with 2, 2, 1 and 1 is 2. With either, the own record opened (3848 rand() on
// closing it without other accesses) and 0x724585 at 1 if the zen (+0xecc, unsigned) does not cover its price
// (0x6b8c68: 5000000 and 1000000).
void ScanTheTrainerBox(Kernel::WorldState& world, Crt::CrtRandom& random);

// 0x6b8c68: the price of the box's mix `kind` (0, 1 or 2).
[[nodiscard]] std::uint32_t TrainerMixPrice(std::uint8_t kind);

// 0x421d90 (from 0x44c633): zeroes the 0x48 bytes of 0x723918..0x72395f, the last thing the pets' tooltip asked for
// (0x422990, P-C126): the type and D+4 of WorldRuntime::petTooltipQuery. Keeps its key 0x723914 and the pet stats
// that start at 0x723960.
void ForgetThePetTooltipQuery(Kernel::WorldState& world);

// What 0x44b190 leaves in the caller's buffer: the Text.bmd text and what the sprintf passes to it.
enum class PetQuoteLine : std::uint8_t
{
    NoPet, // 1229, without arguments
    Full,  // 1230, without arguments: the price at 0
    Short, // 1231 with the shortfall (price − zen) with its commas (0x5b1130)
    Cost,  // 1232 with 0x5b13e0's text, the price without the doubling (Q-C955)
};

struct PetRecoveryQuote
{
    // −1 without the pet.
    std::uint32_t price = 0;
    PetQuoteLine line = PetQuoteLine::NoPet;
    std::string number;
};

// What 0x44b190 reads: the world, the rand(), the own record's count (the real one or an intermediate draw's) and
// Item.bmd. With `writesGlobals` false (an intermediate draw) it does not set 0x78abb10 to 0, though it counts as if
// it had.
struct PetQuoteContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    const Tables::ItemTable* items = nullptr;
    bool writesGlobals = true;
};

// 0x44b190(button + 1, buffer): the price of recovering the pet of button 0 (the horse) or 1 (the raven), with one
// close of the own record (3848 rand()) and another with a price other than 0.
[[nodiscard]] Core::Expected<PetRecoveryQuote, std::string> QuoteThePetRecovery(const PetQuoteContext& context,
                                                                                std::size_t button);

// 0x44b4e0: with 0x724580 at 0, at 4 or outside 1..3, nothing.
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainer(const Ui::GameUiContext& context,
                                                               Ui::PanelRegistry& panels);

// 0x44b540, mode 1. The mouse in [0xd5, 0x1aa) × [0x5a, 0x122) belongs to the UI. With the click, the three buttons
// ([0x103, 0x17b) × [0xb4 + 0x1e · i, +0x16)) clear the click and the button: 0 goes to mode 2 (0x449960), 1 to 3 and
// opens the inventory (0x449970), and 2, which the drawing does not show, nothing more (Q-C957). Without a button, the
// close one ([0x189, 0x1a1) × [0x109, 0x121)) clears both and closes (0x4499b0).
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainerMenu(const Ui::GameUiContext& context,
                                                                   Ui::PanelRegistry& panels);

// 0x44b910, mode 2. The mouse in [0xd5, 0x1aa) × [0x5a, 0x122) belongs to the UI. With the click, the two buttons
// ([0x103, 0x17b) × [0x96 + 0x3c · i, +0x16)) clear the click and the button and ask for the price (0x44b190); with
// the own record open, if the zen covers it (unsigned) and the price is not −1, they send C1 05 34 <8 or 1> <the
// price's low byte> with the XOR32 and in C3, close the trainer (0x4499b0) and call 0x421d90. Without a button, the
// close one clears both and closes. The rand(): the 3848 of each own-record close that leaves it at 0 (one or two in
// 0x44b190 and the step's) and the C3's two.
[[nodiscard]] Core::Expected<void, std::string> StepThePetRecovery(const Ui::GameUiContext& context);

// 0x44d010, mode 3. The mouse in [0x104, 0x1c2) × [0, 0x1b1) belongs to the UI. With the click, the mix button
// ([0x14f, 0x17b) × [0x12c, 0x14c)) clears the click and the button and, with the mix the previous step's 0x44ae10
// left (0x724584), sends C1 04 86 0D (mix 1) or 0E (mix 2) with the XOR32 and in the clear, without checking the zen
// (Q-C956); the close one ([0x118, 0x130) × [0x18b, 0x1a3)) clears both and closes the panel (0x65cae0(0x8000)).
// Always, at the end, 0x44ae10.
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainerBox(const Ui::GameUiContext& context,
                                                                  Ui::PanelRegistry& panels);
} // namespace Client::PetTrainer
