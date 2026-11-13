#include "Scenes/SocialRequests.h"

#include "Core/Expected.h"
#include "Protocol/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/GamePanels.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"

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

namespace Client::Scenes
{
namespace
{
// 0x56c6ab: +0x88 en 1, un jugador; 0x56c6c6: el tipo de los jugadores.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint16_t PlayerType = 0x20a;
// 0x56cfbe (0x69780 = 400 × 0x438) y 0x570617 (0x190): los que recorre el de enfrente.
constexpr std::size_t SearchedActors = 400;
// 0x56c730 y 0x56c78a: a lo sumo una celda.
constexpr std::int32_t NextCell = 1;
// 0x56cfa9: las ocho direcciones, opuestas a 4.
constexpr std::int32_t Opposite = 4;
// 0x6c8e30.
constexpr std::size_t ChosenTarget = 2;
// El relleno (rand() & 0xff, 0x56cac8) y el serial (0x40ae60 y el cierre de 0x40b0a0) de un C3.
constexpr std::uint32_t C3Randoms = 2;
// Los textos: 0x76dfe80, 0x76dffac, 0x76e1398 y 0x76cff08.
constexpr std::size_t TradeRequestedText = 475;
constexpr std::size_t PartyRequestedText = 476;
constexpr std::size_t PersonalShopText = 493;
constexpr std::size_t NotTheLeaderText = 257;

// Lo que lee una celda protegida: abre su entrada y la cierra, con los rand() de su cierre.
std::int32_t ReadCell(std::uint8_t value, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return value;
}

// 0x451d10 y 0x451810: el personaje tiene un cartel de tienda personal en el mapa de 0x7245c8.
bool HasAPersonalShop(const WorldState& world, std::size_t index)
{
    return HasShopTitle(world.Runtime().social.personalShop, index);
}

// 0x5af560: ni la tienda personal propia (0x800) ni la ajena (0x20000) abiertas, sin rand().
bool NoShopOpen(const SocialRequestContext& context)
{
    GamePanels panels(context.world, context.random);
    return !panels.Registry().IsOpen(UI::PersonalShopPanel) && !panels.Registry().IsOpen(UI::OtherShopPanel);
}

// 0x5110d0 con el MOVSX de una clave: el primero de los 400 vivo con la misma (los dos con MOVSX), o el 400.
std::size_t ActorWithTheSameKey(const WorldState& world, std::uint16_t key)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (actors[index].alive && actors[index].key == key)
            return index;
    }
    return SearchedActors;
}

// El elegido, si hay uno: 0x6c8e30 sin −1. El original lo usa de índice sin mirar más.
Core::Expected<std::optional<std::size_t>, std::string> TheChosen(const WorldState& world, std::string_view where)
{
    const std::int32_t chosen = world.Runtime().selectedTargets[ChosenTarget];
    if (chosen == -1)
        return std::optional<std::size_t>{};
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= ActorPoolSize)
        return Core::Unexpected(std::format("{}: el elegido {} lee fuera de los personajes", where, chosen));
    return std::optional<std::size_t>{static_cast<std::size_t>(chosen)};
}

// El de enfrente, desde el lazo de 0x56ce1e: vivo (+0), alguien a quien pedirle y al lado.
bool CanBeAskedFromTheFront(const SocialRequestContext& context, std::size_t index)
{
    return context.world.Actors()[index].alive && CanBeAsked(context.world, index) &&
           NextToTheHero(context.world, context.random, index);
}

// 0x69fd9b(buffer, texto, nombre) y 0x555190 con el 1: el aviso con el nombre del primero vivo con la clave.
Core::Expected<UI::ChatCommandNotice, std::string> NamedNotice(const SocialRequestContext& context, std::size_t text,
                                                               std::uint16_t key, std::string_view where)
{
    const auto format = UI::TextAt(&context.texts, text);
    if (!format)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, text));
    const std::string name = ChatNameOf(context.world.Actors()[ActorWithTheSameKey(context.world, key)]);
    return UI::ChatCommandNotice{.text = text, .type = UI::SystemMessage, .formatted = UI::Sprintf(*format, name)};
}

UI::ChatCommandOutcome Handled(std::vector<UI::ChatCommandNotice> notices = {})
{
    return UI::ChatCommandOutcome{.handled = true, .notices = std::move(notices)};
}

// 0x574ceb..0x57538b, 0x575570..0x575d96 y 0x575e90..0x576696: el C1 10 3F 05 con la clave (+0x1f4) y los diez bytes
// del nombre (+0x1d9) del personaje, en C3.
Core::Expected<void, std::string> SendTheShopRequest(const SocialRequestContext& context, std::size_t index,
                                                     std::string_view where)
{
    const ActorState& actor = context.world.Actors()[index];
    return SendInC3(context, Protocol::PersonalShop::WriteShopItemsRequest(actor.key, actor.name), where);
}

// El nombre de un lugar de 0x77f3cd0 o del héroe hasta su primer 0, para strcmp.
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

Core::Expected<UI::ChatCommandOutcome, std::string> SendATradeRequest(const SocialRequestContext& context,
                                                                      std::size_t index, std::string_view where)
{
    if (!NoShopOpen(context) || !context.world.Runtime().drawingEnabled)
        return Handled();
    const std::uint16_t key = context.world.Actors()[index].key;
    if (auto sent = SendInC3(context, Protocol::Trade::WriteTradeRequest(key), where); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    auto notice = NamedNotice(context, TradeRequestedText, key, where);
    if (!notice.HasValue())
        return Core::Unexpected(notice.Error());
    return Handled({std::move(notice).Value()});
}

Core::Expected<UI::ChatCommandOutcome, std::string> SendAPartyRequest(const SocialRequestContext& context,
                                                                      std::size_t index, std::string_view where)
{
    WorldRuntime& runtime = context.world.Runtime();
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

bool AnotherLeadsTheParty(const WorldState& world)
{
    const WorldRuntime& runtime = world.Runtime();
    return runtime.hud.partyCount > 0 &&
           NameUntilZero(runtime.social.party.slots[0].name) != ChatNameOf(world.Actors()[runtime.hero]);
}

bool IsInTheParty(const WorldState& world, std::size_t index)
{
    const WorldRuntime& runtime = world.Runtime();
    const std::string name = ChatNameOf(world.Actors()[index]);
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

bool IsTheDuelRival(const WorldState& world, std::size_t index)
{
    return ChatNameOf(world.Actors()[index]) == NameUntilZero(world.Runtime().social.duel.rivalName);
}

CellDistance CellDistanceToTheHero(WorldState& world, Render::Models::CrtRandom& random, std::size_t index)
{
    const auto actors = world.Actors();
    ActorState& other = actors[index];
    ActorState& hero = actors[world.Runtime().hero];
    const std::int32_t otherX = ReadCell(other.cellX, other.cellXAccesses, random);
    const std::int32_t heroX = ReadCell(hero.cellX, hero.cellXAccesses, random);
    const std::int32_t otherY = ReadCell(other.cellY, other.cellYAccesses, random);
    const std::int32_t heroY = ReadCell(hero.cellY, hero.cellYAccesses, random);
    return CellDistance{.x = std::abs(otherX - heroX), .y = std::abs(otherY - heroY)};
}

bool CanBeAsked(const WorldState& world, std::size_t index)
{
    const ActorState& actor = world.Actors()[index];
    return actor.byte88 == PlayerKind && index != world.Runtime().hero &&
           (actor.type == PlayerType || actor.byte3e3 != 0);
}

bool NextToTheHero(WorldState& world, Render::Models::CrtRandom& random, std::size_t index)
{
    const auto actors = world.Actors();
    ActorState& other = actors[index];
    ActorState& hero = actors[world.Runtime().hero];
    const std::int32_t otherX = ReadCell(other.cellX, other.cellXAccesses, random);
    const std::int32_t heroX = ReadCell(hero.cellX, hero.cellXAccesses, random);
    if (std::abs(otherX - heroX) > NextCell)
        return false;
    const std::int32_t otherY = ReadCell(other.cellY, other.cellYAccesses, random);
    const std::int32_t heroY = ReadCell(hero.cellY, hero.cellYAccesses, random);
    return std::abs(otherY - heroY) <= NextCell;
}

Core::Expected<bool, std::string> FacesTheHero(const WorldState& world, std::size_t index)
{
    const auto actors = world.Actors();
    const auto other = ActionDirection(actors[index].angles.z);
    if (!other)
        return Core::Unexpected(std::string("0x56cf44: el ángulo del otro está fuera del _ftol (0x69fd3c)"));
    const auto hero = ActionDirection(actors[world.Runtime().hero].angles.z);
    if (!hero)
        return Core::Unexpected(std::string("0x56cf78: el ángulo del héroe está fuera del _ftol (0x69fd3c)"));
    return std::abs(static_cast<std::int32_t>(*other) - static_cast<std::int32_t>(*hero)) == Opposite;
}

Core::Expected<UI::ChatCommandOutcome, std::string> RequestATrade(const SocialRequestContext& context)
{
    auto chosen = TheChosen(context.world, "0x56c68b");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto index = chosen.Value())
    {
        if (!CanBeAsked(context.world, *index) || !NextToTheHero(context.world, context.random, *index))
            return Handled();
        if (HasAPersonalShop(context.world, *index))
            return Handled({UI::ChatCommandNotice{.text = PersonalShopText, .type = UI::ErrorMessage}});
        return SendATradeRequest(context, *index, "0x56c7e2");
    }
    // 0x56ce17..0x56cfc6: el primero de los 400 al lado y enfrente.
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        if (!CanBeAskedFromTheFront(context, index))
            continue;
        if (HasAPersonalShop(context.world, index))
            return Handled({UI::ChatCommandNotice{.text = PersonalShopText, .type = UI::ErrorMessage}});
        auto faces = FacesTheHero(context.world, index);
        if (!faces.HasValue())
            return Core::Unexpected(faces.Error());
        if (faces.Value())
            return SendATradeRequest(context, index, "0x56d018");
    }
    return Handled();
}

Core::Expected<void, std::string> RequestADuel(const SocialRequestContext& context)
{
    auto chosen = TheChosen(context.world, "0x56d823");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    std::optional<std::size_t> other;
    if (const auto picked = chosen.Value())
    {
        if (CanBeAsked(context.world, *picked) && NextToTheHero(context.world, context.random, *picked))
            other = *picked;
    }
    else
    {
        // 0x56e0b5..0x56e2da: el primero de los 400 al lado y enfrente.
        for (std::size_t index = 0; index < SearchedActors && !other; ++index)
        {
            if (!CanBeAskedFromTheFront(context, index))
                continue;
            auto faces = FacesTheHero(context.world, index);
            if (!faces.HasValue())
                return Core::Unexpected(faces.Error());
            if (faces.Value())
                other = index;
        }
    }
    if (!other)
        return {};
    // 0x56d9b8..0x56e06b y 0x56e2df..0x56e9ec: el AA con la clave y los diez bytes de +0x1d9.
    const ActorState& actor = context.world.Actors()[*other];
    return SendInC3(context, Protocol::Duel::WriteDuelStart(actor.key, actor.name), "0x56d9b8");
}

Core::Expected<UI::ChatCommandOutcome, std::string> RequestAParty(const SocialRequestContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const ActorState& hero = context.world.Actors()[runtime.hero];
    // 0x56f13f..0x56f1b0.
    // strcmp: el nombre del héroe sigue en +0x1e3 si llena los diez bytes.
    if (runtime.hud.partyCount > 0 && NameUntilZero(runtime.social.party.slots[0].name) != ChatNameOf(hero))
        return Handled({UI::ChatCommandNotice{.text = NotTheLeaderText, .type = UI::SystemMessage}});
    auto chosen = TheChosen(context.world, "0x56f1b1");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    if (const auto index = chosen.Value())
    {
        if (!CanBeAsked(context.world, *index) || !NextToTheHero(context.world, context.random, *index))
            return Handled();
        return SendAPartyRequest(context, *index, "0x56f706");
    }
    // 0x56fda3..0x570625: el primero de los 400 al lado y enfrente.
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
Core::Expected<UI::ChatCommandOutcome, std::string> RequestTheShopItems(const SocialRequestContext& context,
                                                                        std::optional<std::string_view> name)
{
    // 0x574c0c..0x574c78.
    GamePanels panels(context.world, context.random);
    UI::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(UI::NpcShopPanel) || registry.IsOpen(UI::WarehousePanel) || registry.IsOpen(UI::TradePanel) ||
        registry.IsOpen(UI::ChaosBoxPanel) || registry.IsOpen(UI::TrainerPanel))
        return UI::ChatCommandOutcome{.handled = false,
                                      .notices = {{.text = UI::ShopTrainerText, .type = UI::SystemMessage}}};
    // 0x574c97..0x574ce5: el elegido, sin mirar si está vivo ni al lado.
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
        // 0x575529..0x575e77: a cada uno de los 400 vivo con ese nombre, uno por uno (07 Q-C967).
        for (std::size_t index = 0; index < SearchedActors; ++index)
        {
            if (!actors[index].alive || ChatNameOf(actors[index]) != *name)
                continue;
            if (auto sent = SendTheShopRequest(context, index, "0x575570"); !sent.HasValue())
                return Core::Unexpected(sent.Error());
        }
        return Handled();
    }
    // 0x575daf..0x575e66: el primero de los 400 vivo, alguien a quien pedirle y enfrente, sin mirar si está al lado.
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
} // namespace Client::Scenes
