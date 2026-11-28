#pragma once

#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The trade, party and duel globals (CL9 milestone 7, 04 § El comercio, § La party and § El duelo), in the .bss
// unless they say otherwise. The packets write them (Social/SocialEvents) and their windows read them.
namespace Client::Social
{
// 0x78a5a20 and 0x78a92a8: the two trade grids, 8 × 4 of 0x48 bytes; 0x77f3dc8, 0x5bc5e0's copy, 32 more.
inline constexpr std::size_t TradeGridCells = 0x20;
inline constexpr std::int32_t TradeGridColumns = 8;
inline constexpr std::int32_t TradeGridRows = 4;
// 0x78abac8 is set to 0x96 by the zen (0x626b20) and by an item leaving the own grid without the OK set (0x5cf396).
inline constexpr std::int32_t TradeOkWait = 0x96;
// 0x78aba1c..0x78aba26: 10 bytes and the terminator.
inline constexpr std::size_t TradeNameSize = 11;
// +0x44 of a 0x78a5a20 cell that comes back worse than its copy (0x5bc470); the inventory step does not overwrite it
// (0x5e5172).
inline constexpr std::uint8_t ChangedFromTheCopy = 99;
// 0x77f3cd0: 0x24 bytes each. The 42 and the 44 do not cap them (Q-C960).
inline constexpr std::size_t PartySlots = 5;
// 0x78abae0..0x78abaea: the name and its terminator.
inline constexpr std::size_t DuelNameSize = 11;

struct TradeState
{
    std::array<std::byte, TradeNameSize> otherName{};
    // 0x78abab0: the 37's WORD, in a DWORD.
    std::uint32_t otherLevel = 0;
    // 0x6c9324 (.data −1).
    std::int32_t otherGuild = -1;
    // 0x78abab4, from the 3B.
    std::uint32_t otherMoney = 0;
    // 0x78abab8: the own zen that the GS accepted (3A).
    std::uint32_t ownMoney = 0;
    // 0x57678ac: the zen that dialog 0x73 sent in the last 3A.
    std::uint32_t sentMoney = 0;
    // 0x78abac0 and 0x78abac1.
    std::uint8_t otherOk = 0;
    std::uint8_t ownOk = 0;
    // 0x78abac4: the 37 sets it to 0; the step reads it (0x5ecde0).
    std::int32_t field78abac4 = 0;
    // 0x4d55bd..0x4d562b: the 37 with 1 calls SetCursorPos(mouse x · 260 / 640 · width / 640, mouse y · height / 480)
    // with the mouse and the screen of the moment. MuRet leaves it requested here and does not move the cursor (08
    // P-C202).
    std::uint8_t cursorMoveRequested = 0;
    // 0x78abac8: set to 0x96 by the zen (0x626b20) and by the item leaving the own grid (0x5cf396); the step lowers it.
    std::int32_t okWait = 0;
    // 0x78abaa5: something changed in the other's grid (0x5bc470) or the OK was pressed (0x5ecf95).
    std::uint8_t changed = 0;
    // 0x78abacc: the zen button sets it to 2 (0x5ece9a) and the storage ones to 0 (deposit) or 1 (withdraw) (0x5ea18f);
    // the 0x73's OK reads it without the trade open (0x6270ed, the storage zen, milestone 6). The other readers are
    // left for the VM (08 P-C203).
    std::int32_t field78abacc = 0;
    // 0x78abb14 and 0x6c932e (.data 0xff): the warning waiting for its answer and the answer (1, yes; 2, no; 0xff,
    // none). Selling something valuable (0x5df417) sets 1 with the 0x98; the chaos box mix (0x5fe610), 2 with the 0x98;
    // the OK with the other's grid changed (0x5ecf67), 3 with 0x633300(4, 0x173, 0x94); panel 0x2000 (0x5e75fc), 4;
    // buying in another's shop (0x5c5181), 5 with the 0x94; a price below the value (0x5dc7e9), 6 with the 0x99. The
    // steps of those dialogs (0x6282dc) and the jewel mix question (0x62846f) write the answers, and 0x5d8d50 reads
    // them (0x5d8dee..).
    std::int32_t warningKind = 0;
    std::uint8_t warningByte = 0xff;
    std::array<Kernel::ItemState, TradeGridCells> otherGrid{};
    std::array<Kernel::ItemState, TradeGridCells> ownGrid{};
    std::array<Kernel::ItemState, TradeGridCells> backup{};
    // 0x5767968 and 0x57635bc: the 3D that arrived with the lock 0x78abb39, for 0x4d1400.
    std::uint8_t resultDeferred = 0;
    std::array<std::uint8_t, 4> deferredHead{};
};

// A slot of 0x77f3cd0 (0x4d67e0): the name at +0 with its terminator at +0xa, the number (+0xb), the map (+0xc), the
// cell (+0xd and +0xe), the life (+0x10), the maximum (+0x14) and the 44's tenths (+0x18).
struct PartySlot
{
    std::array<std::byte, TradeNameSize> name{};
    std::uint8_t number = 0;
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint32_t life = 0;
    std::uint32_t maxLife = 0;
    std::uint8_t tenths = 0;
    // +0x1c: the viewport character with that name, or −1 (absent), −2 (not searched: 0x5e4140 leaves it) or −3 (the
    // published hero is not visible). 0x59e3b0 and 0x59e700 write it every frame (PartyActors.h); the .bss starts at 0
    // and the 42 does not touch it.
    std::int32_t actorIndex = 0;
    // +0x20: that character's +0x78 when it was found, or 0. With 8, the right-hand list tints it green (0x5f14c1).
    std::uint32_t actorFlags = 0;
};

struct PartyState
{
    std::array<PartySlot, PartySlots> slots{};
    // 0x78aba4c: the inviter's key (40), which dialog 0x77 returns in the 41. The one followed (0x6c8e50) is
    // GameMovementState::followTarget.
    std::int32_t requestKey = 0;
};

// 0x78abad8, 0x78abadc and 0x78abae0 are also read by the attack cursor (0x558bb0, Hero/HeroAttack): this is the only
// copy (08 P-C206 b).
struct DuelState
{
    // 0x78abad8: the duel in progress (0x5a6cc0's scoreboard).
    std::uint8_t active = 0;
    // 0x78abadc.
    std::int32_t rivalKey = 0;
    std::array<std::byte, DuelNameSize> rivalName{};
    // 0x78abaf8 and 0x78abafc (AD).
    std::int32_t ownScore = 0;
    std::int32_t rivalScore = 0;
    // 0x5767814 and 0x5767816, which the AB checks and clears (0x4e4d58), are the F3 22's time (EventGlobals::timeKind
    // and timeValue, 08 P-C201).
};

// A sign of 0x7245c8's map (0x4521d0's 100-byte object, which 0x4524e0 builds): the key (+0x40), the character's name
// (+0, from +0x1d9) and the title (+0x10). The title's two lines (+0x20 and +0x30, 0x452f80) and the sizes (+0x50,
// 0x4531b0, and +0x58, text 1104) come from these two texts (PersonalShop::MeasureTheShopSign), and the drawing
// (0x451d60) sets the place (+0x48, 0x4527d0) again every frame before reading it (PersonalShop/ShopSigns).
struct ShopTitle
{
    std::int32_t key = 0;
    std::string name;
    std::string title;
    // +0x44. The creation (0x4524e0 → 0x4529b0) and 0x4516d0 turn it on and 0x451720 turns it off, both from the
    // bubbles (0x5576a0..0x5576c9).
    bool visible = true;
    // +0x60 (0 from 0x452320): the chosen one's sign blinks with the shop command. The signs' drawing (0x451c20) writes
    // it and their colors (0x452a10) and the bubble title's (0x4517c0) read it, in the next frame.
    std::uint8_t hovered = 0;
    // +0x48 and +0x4c: the sign's place on screen. The creation and the update with the same key set it to (−1, −1)
    // (0x4527d0 from 0x4524e0 and 0x451301); the drawing writes it every frame, already spread out and clamped to the
    // edges (0x451d60 and 0x451e40), before reading it.
    std::int32_t x = -1;
    std::int32_t y = -1;
};

// 0x78a6768: the own shop's grid (0x78ab0b8, OwnInventory::secondCells) while another's is viewed.
inline constexpr std::size_t ShopGridCells = 0x20;
// 0x78a64f9: the viewed shop owner's name (+0x1d9 of its copy), 10 bytes.
inline constexpr std::size_t ShopOwnerNameSize = 10;
// 0x6c931c: no shop (the .data), the own (Open(0x800)) and another's (Open(0x20000)).
inline constexpr std::int32_t NoShopView = -1;
inline constexpr std::int32_t OwnShopView = 1;
inline constexpr std::int32_t OtherShopView = 2;

struct PersonalShopState
{
    // 0x7245c8 (0x451d10, a std::map by the character's pointer, which sorts like its index). Only the 3F 00, 02 and 10
    // fill it.
    std::map<std::size_t, ShopTitle> titles;
    // +0x10 of 0x7245c8 (0 from 0x450f20): the signs' drawing count, from 0 to 7 (0x451c3e).
    std::int32_t signBlink = 0;
    // +0x14 of 0x7245c8 (1 from 0x450f20): the signs are drawn. "/ver puestos" and its pair write it (0x4516a0 and
    // 0x4516b0 from 0x56d722 and 0x56d776, Chat::RunTheChatCommand) and the drawing reads it (0x4516c0).
    std::uint8_t signsShown = 1;
    // 0x78aba50: the 3F 02 with 1 sets it; the hero's 3F 03 clears it.
    std::uint8_t ownOpen = 0;
    // 0x78aba64: the window title, written by dialog 0x91's step (0x6260f6) and by Enter with the title field open
    // (0x4b55db), if 0x5ebbd0 passes, and read by the 3F 02 (0x5ea407 and 0x62378b). The 3F 05 sets the viewed shop's
    // (strncpy of 0x24, 0x4db92b) and closing it goes back to the hero sign's (0x5eb020).
    std::string ownTitle;
    // 0x6c931c (.data −1): Open(0x800) sets it to 1 (0x65b21d) and Open(0x20000) to 2 (0x65ba37); 0x5eb020, on closing
    // either, to −1. The 3F lists, the 28 and the 3F 12 check it.
    std::int32_t mode = NoShopView;
    // 0x7245e4 and 0x7245e0: the 0x10-byte std::maps that Open(0x800) and Open(0x20000) build the first time
    // (0x65b194.., 0x65b888..), from slot to price. The first is the own shop's (0x5dea3f, with 0x6c931c at 1; a
    // failing 3F 01 erases it) and the second another's (the 3F 05, 06 and 0x13).
    std::map<std::int32_t, std::int32_t> ownPrices;
    std::map<std::int32_t, std::int32_t> otherPrices;
    // Both pointers other than 0: until a shop's first Open nobody writes into the maps (lists arriving earlier put the
    // items without the prices).
    bool pricesBuilt = false;
    // 0x78a6768, the copy that the 3F 05 makes if 0x20000 was not open and that 0x5eb020 puts back.
    std::array<Kernel::ItemState, ShopGridCells> ownGridBackup{};
    // 0x78a6320: the copy of the viewed shop owner's record (0x438 bytes, 0x4dbaab), zeros without one. Only its key
    // (+0x1f4, 0x78a6514) and its name (+0x1d9, 0x78a64f9) are read, in 0x5eb020.
    std::uint16_t ownerKey = 0;
    std::array<std::byte, ShopOwnerNameSize> ownerName{};
    // 0x78aba58 and 0x78aba5c (.bss 0): the two windows' corner, which 0x5ea290 sets to (0x104, 0) every step with the
    // own one open.
    std::int32_t windowX = 0;
    std::int32_t windowY = 0;
    // 0x78aba60 (.bss 0): the dialog 0x91 that 0x5eba70 opened: 1 the title's, 2 the price's. The price answer
    // (0x5dc7b2) and the answered title (0x5ea30a) set it back to 0.
    std::int32_t dialogMode = 0;
};

// 0x575d9d0 (0x471e80, 0x471650's object): dialog 0x9c's confirmation box. 0x4716e0 opens it (only from 0x5ea290,
// with the 1000 and text 1131) and 0x471930 handles it: +0 the id that 0x623750 receives, +4 the button count (1 or 2)
// and +8 the lines (a std::vector of std::string).
struct ConfirmBoxState
{
    std::int32_t id = 0;
    std::int32_t type = 0;
    std::vector<std::string> lines;
};

struct SocialState
{
    TradeState trade{};
    PartyState party{};
    DuelState duel{};
    PersonalShopState personalShop{};
    ConfirmBoxState confirmBox{};
    // 0x6c8e78 (.data 10), 0x77f3b6a and 0x77f3b6c: the dialog field's length, the zen mode and the typed zen (0x5ece58
    // opens the 0x73 with 8, 1 and 0; 0x4d58da closes it with 0x2a and 0).
    std::int32_t dialogFieldLimit = 10;
    std::uint8_t moneyField = 0;
    std::int32_t typedMoney = 0;
};
} // namespace Client::Social
