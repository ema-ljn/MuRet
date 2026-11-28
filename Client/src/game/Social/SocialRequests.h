#pragma once

#include "Core/Expected.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Chat/ChatCommands.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The chat commands' requests to another player (CL9 milestone 7, phases 2 and 5; 06 § Lo social en pantalla): /trade
// (0x56c68b..0x56d623), /duelstart without a duel (0x56d823..0x56e9ec), /party (0x56f13f..0x570d47) and /purchase
// (0x574c0c..0x57694d) of 0x56c440. Each one looks for the other: the chosen one (0x6c8e30) or, without it, the first
// of the 400 in front of the hero. Each cell it compares opens and closes its protected entry (0x40b570 and 0x40b800,
// or the same inline), with its close's four rand(); the packet goes in C3, with the padding's and the serial's two.
// *Static*.
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Social
{

struct SocialRequestContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // Text.bmd: 475 and 476 (sprintf with the name), 493 (the personal shop) and 257 (not the leader).
    const Tables::TextTable& texts;
    // The wire (App::ServerSession). Empty, a request that gets to be sent is the error.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x69fd70 (the padding) and a C3's serial (0x40ae60 with the close of 0x40b0a0 or 0x40b3f0): their two rand() and
// the packet to the wire. Without the wire, the error with `where`.
[[nodiscard]] Core::Expected<void, std::string> SendInC3(const SocialRequestContext& context,
                                                         Protocol::Outgoing outgoing, std::string_view where);

// The 36 to character `index` and notice 475 (0x56c7c8..0x56cdfc and 0x56cffe..0x56d60e of /trade;
// 0x4064d6..0x406aea of command 1 of the command window): with the own or another's personal shop open (0x5af560) or
// without the world loaded (0x7d3a101), nothing; otherwise the C1 05 36 with its key in C3 and the notice built with
// the name of the first one alive with that key (0x5110d0), with 1.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string>
SendATradeRequest(const SocialRequestContext& context, std::size_t index, std::string_view where);

// The 40 to character `index` and notice 476 (0x56f6d1..0x56fd88 and 0x570638..0x570d22 of /party;
// 0x407352..0x40798d of command 3): without the world loaded (0x7d3a101), nothing; otherwise 0x78aba4c = its key
// (MOVSX), the C1 05 40 in C3 and the notice with 1.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string>
SendAPartyRequest(const SocialRequestContext& context, std::size_t index, std::string_view where);

// 0x5110d0(key), 0x69fd9b(buffer, text, name) and 0x555190 with 1: notice `text` with the name of the first one alive
// with the key (the command window's guild commands, 0x4089cd and the other three).
[[nodiscard]] Core::Expected<Chat::ChatCommandNotice, std::string>
NoticeWithTheName(const SocialRequestContext& context, std::size_t text, std::uint16_t key, std::string_view where);

// 0x56f13f..0x56f1b0 and 0x4072df..0x407324: with a party (0x78aba48 above 0), the first slot of 0x77f3cd0 is not the
// hero (strcmp with +0x1d9).
[[nodiscard]] bool AnotherLeadsTheParty(const Kernel::WorldState& world);

// 0x4061ef..0x40625f: the character is in the party (strcmp of its +0x1d9 with the name of each of the 0x78aba48 slots
// of 0x77f3cd0, 0x24 bytes each).
[[nodiscard]] bool IsInTheParty(const Kernel::WorldState& world, std::size_t index);

// 0x408101..0x40813b: strcmp of the character's +0x1d9 with the duel rival's name (0x78abae0).
[[nodiscard]] bool IsTheDuelRival(const Kernel::WorldState& world, std::size_t index);

// 0x4060c1..0x406169: the character's distance in cells to the published hero, reading the four (the other's x, the
// hero's x, the other's y, the hero's y) each in its own access, with its close's rand(), always all four.
struct CellDistance
{
    std::int32_t x = 0;
    std::int32_t y = 0;
};
[[nodiscard]] CellDistance CellDistanceToTheHero(Kernel::WorldState& world, Crt::CrtRandom& random, std::size_t index);

// The other one that can be asked something (0x56c6ab..0x56c6d6 and its copies): a player (+0x88 at 1) who is not the
// published hero (0x743a170), of type 0x20a or with +0x3e3 other than 0. The chosen one is not checked for being
// alive; the one in front is (the byte at +0).
[[nodiscard]] bool CanBeAsked(const Kernel::WorldState& world, std::size_t index);

// 0x56c6dc..0x56c78a: the other's logical cell and the hero's, each x and each y in its own access (the other's
// +0x41c, the hero's, the other's +0x420 and the hero's): at most 1 apart in both. With the x far it does not read the
// y's.
[[nodiscard]] bool NextToTheHero(Kernel::WorldState& world, Crt::CrtRandom& random, std::size_t index);

// 0x56cf2f..0x56cfac: the directions from +0x24 (the _ftol of (angle + 22.5) / 45 + 1, & 0xff and remainder by 8) of
// the other and the hero, 4 apart: they face each other. Outside the _ftol, the error.
[[nodiscard]] Core::Expected<bool, std::string> FacesTheHero(const Kernel::WorldState& world, std::size_t index);

// 0x56c68b..0x56d623 (/trade with the level). The chosen one, if present and it can be asked (otherwise 1), or the
// first of the 400 alive, adjacent and in front (with none, 1). Next to a personal shop (0x451810), notice 493 with 2
// and 1. With the own or another's personal shop open (0x5af560) or without the world loaded (0x7d3a101), 1.
// Otherwise the C1 05 36 with its key and notice 475 with the name of the first one alive with that key (0x5110d0),
// with 1, and 1.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string> RequestATrade(const SocialRequestContext& context);

// Whom a chat command asks: the chosen one (0x6c8e30, without checking whether alive) if it is someone to ask
// (CanBeAsked) and adjacent (NextToTheHero, its accesses); without a chosen one, the first of the 400 alive, someone
// to ask, adjacent and in front (FacesTheHero), like the loops of /duelstart (0x56e0b5), /guild (0x574587) and /union
// (0x572679). With none, no value; `where` is the chosen one's address.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string> TheOneToAsk(const SocialRequestContext& context,
                                                                                  std::string_view where);

// 0x56d823..0x56e9ec (/duelstart without a duel): the chosen one or the one in front, like /trade but without the
// shop, without 0x5af560 or 0x7d3a101 and without a notice, and the C1 0F AA with its key and its name. With or
// without it, the chain goes on (0x56e07c).
[[nodiscard]] Core::Expected<void, std::string> RequestADuel(const SocialRequestContext& context);

// 0x56f13f..0x570d47 (/party outside Chaos Castle): with a party (0x78aba48 above 0) and another leader (strcmp of
// 0x77f3cd0's first slot with the hero's name), notice 257 with 1. Otherwise the chosen one or the one in front, like
// /trade but without the shop or 0x5af560; with the world loaded, 0x78aba4c = its key (MOVSX), the C1 05 40 and
// notice 476. Always returns 1.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string> RequestAParty(const SocialRequestContext& context);

// 0x574c0c..0x57694d (/purchase outside Chaos Castle): with the NPC shop (0x10000), the warehouse (0x40), the trade
// (0x20), the chaos box (0x100) or the trainer (0x8000) open (in that order, up to the first; 0x10000 and 0x20 with
// their rand()), notice 1121 with 1 and 0. Otherwise the chosen one (0x6c8e30): if it can be asked, without checking
// whether alive or adjacent, the C1 10 3F 05 with its key and its name in C3; with or without it, 1. Without a chosen
// one, `name` is what strlen and strcmp read in sscanf's second buffer: non-empty, the 3F 05 to each of the 400 alive
// with that name (strcmp with +0x1d9, without checking the type or whether it is the hero) and 1; empty, to the first
// of the 400 alive, someone to ask and in front (without checking whether adjacent), and 1. With no value (the
// uninitialized stack, 07 Q-C967), the error.
[[nodiscard]] Core::Expected<Chat::ChatCommandOutcome, std::string>
RequestTheShopItems(const SocialRequestContext& context, std::optional<std::string_view> name);
} // namespace Client::Social
