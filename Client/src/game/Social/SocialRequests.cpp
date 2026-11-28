#include "Social/SocialRequests.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Hud/GamePanels.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatCommands.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Tables/PlayerModels.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Social
{
namespace
{
// 0x56c6ab: +0x88 at 1, a player; 0x56c6c6: the players' type.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
// 0x56cfbe (0x69780 = 400 × 0x438) and 0x570617 (0x190): the ones the search in front walks.
constexpr std::size_t SearchedActors = 400;
// 0x56c730 and 0x56c78a: at most one cell.
constexpr std::int32_t NextCell = 1;
// 0x56cfa9: the eight directions, opposite at 4.
constexpr std::int32_t Opposite = 4;
// 0x6c8e30.
constexpr std::size_t ChosenTarget = 2;
// A C3's padding (rand() & 0xff, 0x56cac8) and serial (0x40ae60 and 0x40b0a0's close).
constexpr std::uint32_t C3Randoms = 2;
// The texts: 0x76dfe80, 0x76dffac, 0x76e1398 and 0x76cff08.
constexpr std::size_t TradeRequestedText = 475;
constexpr std::size_t PartyRequestedText = 476;
constexpr std::size_t PersonalShopText = 493;
constexpr std::size_t NotTheLeaderText = 257;

// What a protected cell reads: opens its entry and closes it, with its close's rand().
std::int32_t ReadCell(std::uint8_t value, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return value;
}

// 0x451d10 and 0x451810: the character has a personal shop sign on 0x7245c8's map.
bool HasAPersonalShop(const Kernel::WorldState& world, std::size_t index)
{
    return PersonalShop::HasShopTitle(world.Runtime().social.personalShop, index);
}

// 0x5af560: neither the own personal shop (0x800) nor another's (0x20000) open, without rand().
bool NoShopOpen(const SocialRequestContext& context)
{
    Hud::GamePanels panels(context.world, context.random);
    return !panels.Registry().IsOpen(Ui::PersonalShopPanel) && !panels.Registry().IsOpen(Ui::OtherShopPanel);
}

// 0x5110d0 with a key's MOVSX: the first of the 400 alive with the same one (both with MOVSX), or 400.
std::size_t ActorWithTheSameKey(const Kernel::WorldState& world, std::uint16_t key)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (actors[index].alive && actors[index].key == key)
            return index;
    }
    return SearchedActors;
}

// The chosen one, if any: 0x6c8e30 other than −1. The original uses it as an index without further checks.
Core::Expected<std::optional<std::size_t>, std::string> TheChosen(const Kernel::WorldState& world,
                                                                  std::string_view where)
{
    const std::int32_t chosen = world.Runtime().selectedTargets[ChosenTarget];
    if (chosen == -1)
        return std::optional<std::size_t>{};
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= Kernel::ActorPoolSize)
        return Core::Unexpected(std::format("{}: el elegido {} lee fuera de los personajes", where, chosen));
    return std::optional<std::size_t>{static_cast<std::size_t>(chosen)};
}

// The one in front, from 0x56ce1e's loop: alive (+0), someone to ask and adjacent.
bool CanBeAskedFromTheFront(const SocialRequestContext& context, std::size_t index)
{
    return context.world.Actors()[index].alive && CanBeAsked(context.world, index) &&
           NextToTheHero(context.world, context.random, index);
}

// 0x69fd9b(buffer, text, name) and 0x555190 with 1: the notice with the name of the first one alive with the key.
Core::Expected<Chat::ChatCommandNotice, std::string> NamedNotice(const SocialRequestContext& context, std::size_t text,
                                                                 std::uint16_t key, std::string_view where)
{
    const auto format = Ui::TextAt(&context.texts, text);
    if (!format)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, text));
    const std::string name = Actors::ChatNameOf(context.world.Actors()[ActorWithTheSameKey(context.world, key)]);
    return Chat::ChatCommandNotice{.text = text, .type = Ui::SystemMessage, .formatted = Crt::Sprintf(*format, name)};
}

Chat::ChatCommandOutcome Handled(std::vector<Chat::ChatCommandNotice> notices = {})
{
    return Chat::ChatCommandOutcome{.handled = true, .notices = std::move(notices)};
}

// 0x574ceb..0x57538b, 0x575570..0x575d96 and 0x575e90..0x576696: the C1 10 3F 05 with the character's key (+0x1f4)
// and the ten bytes of its name (+0x1d9), in C3.
Core::Expected<void, std::string> SendTheShopRequest(const SocialRequestContext& context, std::size_t index,
                                                     std::string_view where)
{
    const Kernel::ActorState& actor = context.world.Actors()[index];
    return SendInC3(context, Protocol::PersonalShop::WriteShopItemsRequest(actor.key, actor.name), where);
}

// The name of a slot of 0x77f3cd0 or of the hero up to its first 0, for strcmp.
std::string_view NameUntilZero(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}
} // namespace

Core::Expected<void, std::string> SendInC3(const SocialRequestContext& context, Protocol::Outgoing outgoing,
                                           std::string_view where)
{
    if (!context.send)
        return Core::Unexpected(std::format("{}: el pedido sin el cable", where));
    context.random.Discard(C3Randoms);
    context.send(std::move(outgoing));
    return {};
}

Core::Expected<Chat::ChatCommandOutcome, std::string> SendATradeRequest(const SocialRequestContext& context,
                                                                        std::size_t index, std::string_view where)
{
    if (!NoShopOpen(context) || !context.world.Runtime().drawingEnabled)
        return Handled();
    const std::uint16_t key = context.world.Actors()[index].key;
    if (auto sent = SendInC3(context, Protocol::Social::WriteTradeRequest(key), where); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    auto notice = NamedNotice(context, TradeRequestedText, key, where);
    if (!notice.HasValue())
        return Core::Unexpected(notice.Error());
    return Handled({std::move(notice).Value()});
}

Core::Expected<Chat::ChatCommandOutcome, std::string> SendAPartyRequest(const SocialRequestContext& context,
                                                                        std::size_t index, std::string_view where)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.drawingEnabled)
        return Handled();
    const std::uint16_t key = context.world.Actors()[index].key;
    runtime.social.party.requestKey = static_cast<std::int16_t>(key);
    if (auto sent = SendInC3(context, Protocol::Party::WritePartyRequest(key), where); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    auto notice = NamedNotice(context, PartyRequestedText, key, where);
    if (!notice.HasValue())
        return Core::Unexpected(notice.Error());
    return Handled({std::move(notice).Value()});
}

Core::Expected<Chat::ChatCommandNotice, std::string>
NoticeWithTheName(const SocialRequestContext& context, std::size_t text, std::uint16_t key, std::string_view where)
{
    return NamedNotice(context, text, key, where);
}

bool AnotherLeadsTheParty(const Kernel::WorldState& world)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    return runtime.hud.partyCount > 0 &&
           NameUntilZero(runtime.social.party.slots[0].name) != Actors::ChatNameOf(world.Hero());
}

bool IsInTheParty(const Kernel::WorldState& world, std::size_t index)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    const std::string name = Actors::ChatNameOf(world.Actors()[index]);
    for (std::int32_t member = 0; member < runtime.hud.partyCount; ++member)
    {
        const auto slot = static_cast<std::size_t>(member);
        if (slot >= runtime.social.party.slots.size())
            break;
        if (NameUntilZero(runtime.social.party.slots[slot].name) == name)
            return true;
    }
    return false;
}

bool IsTheDuelRival(const Kernel::WorldState& world, std::size_t index)
{
    return Actors::ChatNameOf(world.Actors()[index]) == NameUntilZero(world.Runtime().social.duel.rivalName);
}

CellDistance CellDistanceToTheHero(Kernel::WorldState& world, Crt::CrtRandom& random, std::size_t index)
{
    const auto actors = world.Actors();
    Kernel::ActorState& other = actors[index];
    Kernel::ActorState& hero = world.Hero();
    const std::int32_t otherX = ReadCell(other.cellX, other.cellXAccesses, random);
    const std::int32_t heroX = ReadCell(hero.cellX, hero.cellXAccesses, random);
    const std::int32_t otherY = ReadCell(other.cellY, other.cellYAccesses, random);
    const std::int32_t heroY = ReadCell(hero.cellY, hero.cellYAccesses, random);
    return CellDistance{.x = std::abs(otherX - heroX), .y = std::abs(otherY - heroY)};
}

bool CanBeAsked(const Kernel::WorldState& world, std::size_t index)
{
    const Kernel::ActorState& actor = world.Actors()[index];
    return actor.byte88 == PlayerKind && index != world.Runtime().hero &&
           (actor.type == PlayerType || actor.byte3e3 != 0);
}

bool NextToTheHero(Kernel::WorldState& world, Crt::CrtRandom& random, std::size_t index)
{
    const auto actors = world.Actors();
    Kernel::ActorState& other = actors[index];
    Kernel::ActorState& hero = world.Hero();
    const std::int32_t otherX = ReadCell(other.cellX, other.cellXAccesses, random);
    const std::int32_t heroX = ReadCell(hero.cellX, hero.cellXAccesses, random);
    if (std::abs(otherX - heroX) > NextCell)
        return false;
    const std::int32_t otherY = ReadCell(other.cellY, other.cellYAccesses, random);
    const std::int32_t heroY = ReadCell(hero.cellY, hero.cellYAccesses, random);
    return std::abs(otherY - heroY) <= NextCell;
}

Core::Expected<bool, std::string> FacesTheHero(const Kernel::WorldState& world, std::size_t index)
{
    const auto actors = world.Actors();
    const auto other = Actors::ActionDirection(actors[index].angles.z);
    if (!other)
        return Core::Unexpected(std::string("0x56cf44: el ángulo del otro está fuera del _ftol (0x69fd3c)"));
    const auto hero = Actors::ActionDirection(world.Hero().angles.z);
    if (!hero)
        return Core::Unexpected(std::string("0x56cf78: el ángulo del héroe está fuera del _ftol (0x69fd3c)"));
    return std::abs(static_cast<std::int32_t>(*other) - static_cast<std::int32_t>(*hero)) == Opposite;
}

Core::Expected<Chat::ChatCommandOutcome, std::string> RequestATrade(const SocialRequestContext& context)
{
    auto chosen = TheChosen(context.world, "0x56c68b");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto index = chosen.Value())
    {
        if (!CanBeAsked(context.world, *index) || !NextToTheHero(context.world, context.random, *index))
            return Handled();
        if (HasAPersonalShop(context.world, *index))
            return Handled({Chat::ChatCommandNotice{.text = PersonalShopText, .type = Ui::ErrorMessage}});
        return SendATradeRequest(context, *index, "0x56c7e2");
    }
    // 0x56ce17..0x56cfc6: the first of the 400 adjacent and in front.
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (!CanBeAskedFromTheFront(context, index))
            continue;
        if (HasAPersonalShop(context.world, index))
            return Handled({Chat::ChatCommandNotice{.text = PersonalShopText, .type = Ui::ErrorMessage}});
        auto faces = FacesTheHero(context.world, index);
        if (!faces.HasValue())
            return Core::Unexpected(faces.Error());
        if (faces.Value())
            return SendATradeRequest(context, index, "0x56d018");
    }
    return Handled();
}

Core::Expected<std::optional<std::size_t>, std::string> TheOneToAsk(const SocialRequestContext& context,
                                                                    std::string_view where)
{
    auto chosen = TheChosen(context.world, where);
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto picked = chosen.Value())
    {
        if (CanBeAsked(context.world, *picked) && NextToTheHero(context.world, context.random, *picked))
            return std::optional<std::size_t>{*picked};
        return std::optional<std::size_t>{};
    }
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (!CanBeAskedFromTheFront(context, index))
            continue;
        auto faces = FacesTheHero(context.world, index);
        if (!faces.HasValue())
            return Core::Unexpected(faces.Error());
        if (faces.Value())
            return std::optional<std::size_t>{index};
    }
    return std::optional<std::size_t>{};
}

Core::Expected<void, std::string> RequestADuel(const SocialRequestContext& context)
{
    // 0x56d823 and 0x56e0b5..0x56e2da: the chosen one or the first of the 400 adjacent and in front.
    auto found = TheOneToAsk(context, "0x56d823");
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    const std::optional<std::size_t> other = found.Value();
    if (!other)
        return {};
    // 0x56d9b8..0x56e06b and 0x56e2df..0x56e9ec: the AA with the key and the ten bytes of +0x1d9.
    const Kernel::ActorState& actor = context.world.Actors()[*other];
    return SendInC3(context, Protocol::Social::WriteDuelStart(actor.key, actor.name), "0x56d9b8");
}

Core::Expected<Chat::ChatCommandOutcome, std::string> RequestAParty(const SocialRequestContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ActorState& hero = context.world.Hero();
    // 0x56f13f..0x56f1b0. strcmp: the hero's name runs on into +0x1e3 if it fills the ten bytes.
    if (runtime.hud.partyCount > 0 && NameUntilZero(runtime.social.party.slots[0].name) != Actors::ChatNameOf(hero))
        return Handled({Chat::ChatCommandNotice{.text = NotTheLeaderText, .type = Ui::SystemMessage}});
    auto chosen = TheChosen(context.world, "0x56f1b1");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto index = chosen.Value())
    {
        if (!CanBeAsked(context.world, *index) || !NextToTheHero(context.world, context.random, *index))
            return Handled();
        return SendAPartyRequest(context, *index, "0x56f706");
    }
    // 0x56fda3..0x570625: the first of the 400 adjacent and in front.
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (!CanBeAskedFromTheFront(context, index))
            continue;
        auto faces = FacesTheHero(context.world, index);
        if (!faces.HasValue())
            return Core::Unexpected(faces.Error());
        if (faces.Value())
            return SendAPartyRequest(context, index, "0x570652");
    }
    return Handled();
}
Core::Expected<Chat::ChatCommandOutcome, std::string> RequestTheShopItems(const SocialRequestContext& context,
                                                                          std::optional<std::string_view> name)
{
    // 0x574c0c..0x574c78.
    Hud::GamePanels panels(context.world, context.random);
    Ui::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(Ui::NpcShopPanel) || registry.IsOpen(Ui::WarehousePanel) || registry.IsOpen(Ui::TradePanel) ||
        registry.IsOpen(Ui::ChaosBoxPanel) || registry.IsOpen(Ui::TrainerPanel))
        return Chat::ChatCommandOutcome{.handled = false,
                                        .notices = {{.text = Chat::ShopTrainerText, .type = Ui::SystemMessage}}};
    // 0x574c97..0x574ce5: the chosen one, without checking whether it is alive or adjacent.
    auto chosen = TheChosen(context.world, "0x574c97");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto index = chosen.Value())
    {
        if (!CanBeAsked(context.world, *index))
            return Handled();
        if (auto sent = SendTheShopRequest(context, *index, "0x574ceb"); !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return Handled();
    }
    if (!name)
        return Core::Unexpected(std::string("0x575519: /purchase sin elegido ni segunda palabra: strlen lee el buffer "
                                            "EBP − 0xc0, que sscanf no llenó (la pila sin inicializar, 07 Q-C967)"));
    const auto actors = context.world.Actors();
    if (!name->empty())
    {
        // 0x575529..0x575e77: to each of the 400 alive with that name, one by one (07 Q-C967).
        for (std::size_t index = 0; index < SearchedActors; ++index)
        {
            if (!actors[index].alive || Actors::ChatNameOf(actors[index]) != *name)
                continue;
            if (auto sent = SendTheShopRequest(context, index, "0x575570"); !sent.HasValue())
                return Core::Unexpected(sent.Error());
        }
        return Handled();
    }
    // 0x575daf..0x575e66: the first of the 400 alive, someone to ask and in front, without checking whether it is
    // adjacent.
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (!actors[index].alive || !CanBeAsked(context.world, index))
            continue;
        auto faces = FacesTheHero(context.world, index);
        if (!faces.HasValue())
            return Core::Unexpected(std::format("0x575dfe (/purchase): {}", faces.Error()));
        if (!faces.Value())
            continue;
        if (auto sent = SendTheShopRequest(context, index, "0x575e90"); !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return Handled();
    }
    return Handled();
}
} // namespace Client::Social
