#include "Chat/ChatCommands.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Tables/RecordTable.h"
#include "Tables/TextTable.h"
#include "Chat/MacroFile.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"

#include "support/ChatTexts.h"
#include "support/ItemSetFiles.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The chat commands (0x56c440; CL8 milestone 3): the gates, /move, /filter, /1../0 and the stops. The texts are
// invented (support/ChatTexts.h).
namespace
{
using Client::Chat::ChatCommandContext;
using Client::Chat::ChatCommandNotice;
using Client::Tables::MacroCount;
using Client::Ui::ChatLogState;

class Commands : public ::testing::Test
{
protected:
    ChatCommandContext Context()
    {
        return ChatCommandContext{.texts = texts,
                                  .items = items,
                                  .setOptions = setOptions,
                                  .map = map,
                                  .logoutRequested = logoutRequested,
                                  .warehouseOpen = warehouseOpen,
                                  .log = log,
                                  .macros = macros,
                                  .overlayMode = overlayMode,
                                  .helpItem = helpItem,
                                  .setLabel = &setLabel};
    }

    const Client::Tables::ItemTable* items = nullptr;
    const Client::Tables::ItemSetOptionTable* setOptions = nullptr;

    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::int32_t map = 0;
    bool logoutRequested = false;
    bool warehouseOpen = false;
    ChatLogState log;
    std::array<std::string, MacroCount> macros{};
    std::int32_t overlayMode = 0;
    std::int32_t helpItem = 0;
    std::uint8_t setLabel = 0;
};

// A table of `count` records of `Size` bytes with `name` in the `named` slots and `flag` in byte `at` of each,
// encrypted with XOR3 as RecordTable reads it.
template <std::size_t Size>
Client::Data::Tables::RecordTable<Size> TableWith(std::size_t count, std::span<const std::size_t> named,
                                                  std::string_view name, std::size_t at, std::byte flag,
                                                  std::byte other)
{
    std::vector<std::byte> bytes(count * Size);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::span<std::byte> record = std::span(bytes).subspan(index * Size, Size);
        record[at] = other;
        for (const std::size_t chosen : named)
        {
            if (chosen != index)
                continue;
            for (std::size_t i = 0; i < name.size(); ++i)
                record[i] = static_cast<std::byte>(name[i]);
            record[at] = flag;
        }
        Client::Crypto::Xor3(record);
    }
    return Client::Data::Tables::RecordTable<Size>(bytes);
}

TEST_F(Commands, AnythingElseIsNotACommand)
{
    const auto outcome = Client::Chat::RunTheChatCommand(Context(), "hola a todos", false);
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_FALSE(outcome.Value().sound);
}

// Every social command starts with the Chaos Castle notice (and goes out as chat); outside, the rest belongs to its
// wave.
TEST_F(Commands, TheSocialCommandsStopInChaosCastle)
{
    map = 18;
    for (const std::string_view command : {"/comerciar", "/TRADE algo", "/puesto", "/retar", "/clan", "/grupo"})
    {
        const auto outcome = Client::Chat::RunTheChatCommand(Context(), command, false);
        ASSERT_TRUE(outcome.HasValue()) << command;
        EXPECT_FALSE(outcome.Value().handled);
        EXPECT_EQ(outcome.Value().notices,
                  (std::vector<ChatCommandNotice>{{Client::Chat::ChaosCastleCommandText, Client::Ui::SystemMessage}}));
    }
    map = 0;
    const auto trade = Client::Chat::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_FALSE(trade.HasValue());
    EXPECT_NE(trade.Error().find("0x56c5d0"), std::string::npos);
    // With the warehouse open, trade is not checked.
    warehouseOpen = true;
    const auto skipped = Client::Chat::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_TRUE(skipped.HasValue());
    EXPECT_FALSE(skipped.Value().handled);
}

// 0x56c5d0..0x56c690: /trade reads the level from the own record; below 0x6c8dc8 (6), notice 478 and 1. With it, the
// request (Social::RequestATrade) gives the outcome; without a request, it stops.
TEST_F(Commands, TradeNeedsTheLevel)
{
    std::uint16_t level = 5;
    int reads = 0;
    ChatCommandContext context = Context();
    context.ownLevel = [&level, &reads]()
    {
        ++reads;
        return level;
    };
    const auto low = Client::Chat::RunTheChatCommand(context, "/trade", false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(low.Value().handled);
    EXPECT_EQ(low.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::Chat::TradeLevelText, Client::Ui::SystemMessage}}));
    EXPECT_EQ(reads, 1);
    level = 6;
    const auto unwired = Client::Chat::RunTheChatCommand(context, "/comerciar", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x56c68b"), std::string::npos);
    int asked = 0;
    context.requestATrade = [&asked]() -> Client::Core::Expected<Client::Chat::ChatCommandOutcome, std::string>
    {
        ++asked;
        return Client::Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    const auto enough = Client::Chat::RunTheChatCommand(context, "/comerciar", false);
    ASSERT_TRUE(enough.HasValue()) << enough.Error();
    EXPECT_TRUE(enough.Value().handled);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(reads, 3);
}

// 0x5766ce..0x576890: /personalshop. With the trainer open, notice 1121 and it goes out as chat; below 6, 1123 built
// with 6 and 1; from 6, Open(0x800) and 1. Each time it reaches the level it reads it once.
TEST_F(Commands, PersonalShopNeedsTheLevel)
{
    std::uint16_t level = 5;
    int reads = 0;
    int opened = 0;
    ChatCommandContext context = Context();
    context.ownLevel = [&level, &reads]()
    {
        ++reads;
        return level;
    };
    context.trainerOpen = true;
    const auto trainer = Client::Chat::RunTheChatCommand(context, "/personalshop", false);
    ASSERT_TRUE(trainer.HasValue()) << trainer.Error();
    EXPECT_FALSE(trainer.Value().handled);
    EXPECT_EQ(trainer.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::Chat::ShopTrainerText, Client::Ui::SystemMessage}}));
    EXPECT_EQ(reads, 0);
    context.trainerOpen = false;
    const auto low = Client::Chat::RunTheChatCommand(context, "/puesto", false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(low.Value().handled);
    EXPECT_EQ(low.Value().notices, (std::vector<ChatCommandNotice>{{.text = Client::Chat::ShopLevelText,
                                                                    .type = Client::Ui::SystemMessage,
                                                                    .formatted = "desde el nivel 6"}}));
    level = 6;
    const auto unwired = Client::Chat::RunTheChatCommand(context, "/PersonalShop", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x576822"), std::string::npos);
    context.openThePersonalShop = [&opened]() -> Client::Core::Expected<void, std::string>
    {
        ++opened;
        return {};
    };
    const auto enough = Client::Chat::RunTheChatCommand(context, "/puesto", false);
    ASSERT_TRUE(enough.HasValue()) << enough.Error();
    EXPECT_TRUE(enough.Value().handled);
    EXPECT_TRUE(enough.Value().notices.empty());
    EXPECT_EQ(opened, 1);
    EXPECT_EQ(reads, 3);
}

// 0x574bd6..0x57694d: /purchase (or 1118) anywhere in the text. In Chaos Castle, notice 1150 and it goes out as chat;
// outside, sscanf("%s %s") and the request gets the second word, or nothing if there is none (the uninitialized stack).
TEST_F(Commands, PurchaseScansTheNameForTheRequest)
{
    std::vector<std::optional<std::string>> names;
    ChatCommandContext context = Context();
    const auto unwired = Client::Chat::RunTheChatCommand(context, "/purchase", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x574c0c"), std::string::npos);
    context.requestShopItems = [&names](std::optional<std::string_view> name)
        -> Client::Core::Expected<Client::Chat::ChatCommandOutcome, std::string>
    {
        names.emplace_back(name ? std::optional<std::string>(*name) : std::nullopt);
        return Client::Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    for (const std::string_view command :
         {"/purchase", "/adquirir Brezal", "  /purchase\t Zorzal  extra", "mira /adquirir", "/adquirir \t "})
    {
        const auto outcome = Client::Chat::RunTheChatCommand(context, command, false);
        ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
        EXPECT_TRUE(outcome.Value().handled);
    }
    EXPECT_EQ(names,
              (std::vector<std::optional<std::string>>{std::nullopt, "Brezal", "Zorzal", "/adquirir", std::nullopt}));
    // A first word of 0x48 bytes overruns the name buffer: MuRet does not model it.
    const auto overflow = Client::Chat::RunTheChatCommand(context, std::string(0x48, 'a') + "/purchase", false);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x574c92"), std::string::npos);
    EXPECT_EQ(names.size(), 5U);
    context.map = 23;
    const auto inside = Client::Chat::RunTheChatCommand(context, "/purchase Brezal", false);
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_FALSE(inside.Value().handled);
    EXPECT_EQ(inside.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::Chat::ChaosCastleCommandText, Client::Ui::SystemMessage}}));
    EXPECT_EQ(names.size(), 5U);
}

// 0x56f109..0x56f13f: /party (or /pt) in Chaos Castle gives notice 1150 and goes out as chat; outside, the request
// gives the outcome.
TEST_F(Commands, PartyAsksOutsideChaosCastle)
{
    int asked = 0;
    ChatCommandContext context = Context();
    context.requestAParty = [&asked]() -> Client::Core::Expected<Client::Chat::ChatCommandOutcome, std::string>
    {
        ++asked;
        return Client::Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    const auto outside = Client::Chat::RunTheChatCommand(context, "/PT", false);
    ASSERT_TRUE(outside.HasValue()) << outside.Error();
    EXPECT_TRUE(outside.Value().handled);
    EXPECT_EQ(asked, 1);
    map = 18;
    const auto inside = Client::Chat::RunTheChatCommand(Context(), "/grupo", false);
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_FALSE(inside.Value().handled);
    EXPECT_EQ(inside.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::Chat::ChaosCastleCommandText, Client::Ui::SystemMessage}}));
}

// 0x56d816 and 0x56ea5f: with a duel in progress, /duelstart gives notice 915 and the chain goes on; /duelend sends the
// AB and goes on. Without a duel, /duelend only goes on and /duelstart requests the duel (Social::RequestADuel) and
// goes on too.
TEST_F(Commands, TheDuelCommandsGoOnAsChat)
{
    int ended = 0;
    ChatCommandContext context = Context();
    context.duelActive = true;
    context.endTheDuel = [&ended]() -> Client::Core::Expected<void, std::string>
    {
        ++ended;
        return {};
    };
    const auto busy = Client::Chat::RunTheChatCommand(context, "/retar", false);
    ASSERT_TRUE(busy.HasValue()) << busy.Error();
    EXPECT_FALSE(busy.Value().handled);
    EXPECT_EQ(busy.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::Chat::DuelBusyText, Client::Ui::ErrorMessage}}));
    EXPECT_EQ(ended, 0);
    const auto end = Client::Chat::RunTheChatCommand(context, "/DUELEND", false);
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_FALSE(end.Value().handled);
    EXPECT_EQ(ended, 1);
    ASSERT_TRUE(Client::Chat::RunTheChatCommand(context, "/noretar", false).HasValue());
    EXPECT_EQ(ended, 2);

    context.duelActive = false;
    ASSERT_TRUE(Client::Chat::RunTheChatCommand(context, "/noretar", false).HasValue());
    EXPECT_EQ(ended, 2);
    const auto unwired = Client::Chat::RunTheChatCommand(context, "/duelstart", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x56d823"), std::string::npos);
    int asked = 0;
    context.requestADuel = [&asked]() -> Client::Core::Expected<void, std::string>
    {
        ++asked;
        return {};
    };
    const auto start = Client::Chat::RunTheChatCommand(context, "/duelstart", false);
    ASSERT_TRUE(start.HasValue()) << start.Error();
    EXPECT_FALSE(start.Value().handled);
    EXPECT_TRUE(start.Value().notices.empty());
    EXPECT_EQ(asked, 1);
    const auto guild = Client::Chat::RunTheChatCommand(context, "/guild", false);
    ASSERT_FALSE(guild.HasValue());
    EXPECT_NE(guild.Error().find("0x573f85"), std::string::npos);
}

// 0x573f49 and 0x570d48: in Chaos Castle, notice 1150 and 0; outside, /guild asks to join (Hud::RequestAGuildJoin) and
// /union, /rival and /rivaloff (or their texts 1354, 1356 and 1357; _stricmp with the literal) each one's E5
// (Hud::RequestAGuildRelation). A text that matches only 1357 ends the rivalry.
TEST_F(Commands, TheGuildCommandsAskTheScene)
{
    int joins = 0;
    std::vector<Client::Chat::GuildRelationCommand> relations;
    ChatCommandContext context = Context();
    context.requestAGuildJoin = [&joins]() -> Client::Core::Expected<Client::Chat::ChatCommandOutcome, std::string>
    {
        ++joins;
        return Client::Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    context.requestAGuildRelation = [&relations](Client::Chat::GuildRelationCommand command)
        -> Client::Core::Expected<Client::Chat::ChatCommandOutcome, std::string>
    {
        relations.push_back(command);
        return Client::Chat::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    for (const std::string_view text : {"/GUILD", "/Union", "/rival", "/RIVALOFF"})
    {
        const auto ran = Client::Chat::RunTheChatCommand(context, text, false);
        ASSERT_TRUE(ran.HasValue()) << ran.Error();
        EXPECT_TRUE(ran.Value().handled);
    }
    EXPECT_EQ(joins, 1);
    EXPECT_EQ(relations, (std::vector<Client::Chat::GuildRelationCommand>{
                             Client::Chat::GuildRelationCommand::Union, Client::Chat::GuildRelationCommand::Rival,
                             Client::Chat::GuildRelationCommand::RivalOff}));

    map = 18;
    for (const std::string_view text : {"/guild", "/union"})
    {
        const auto inside = Client::Chat::RunTheChatCommand(Context(), text, false);
        ASSERT_TRUE(inside.HasValue()) << inside.Error();
        EXPECT_FALSE(inside.Value().handled);
        EXPECT_EQ(inside.Value().notices,
                  (std::vector<ChatCommandNotice>{{Client::Chat::ChaosCastleCommandText, Client::Ui::SystemMessage}}));
    }
}

// 0x56d624: /move opens the window and goes on as chat.
// 0x56d6ee..0x56d796: "/ver puestos" turns the signs on (0x4516a0) with notice 1138 of type 1 and its pair turns them
// off (0x4516b0) with 1139 of type 2, also in Chaos Castle; the chain goes on and it goes out as chat. Without the
// map, it stops.
TEST_F(Commands, TheSignCommandsTurnTheShopSignsOnAndOff)
{
    for (const std::int32_t where : {0, 2, 18})
    {
        map = where;
        std::uint8_t signs = 1;
        ChatCommandContext context = Context();
        context.signsShown = &signs;
        const auto off = Client::Chat::RunTheChatCommand(context, "/no ver puestos", false);
        ASSERT_TRUE(off.HasValue()) << off.Error();
        EXPECT_EQ(signs, 0);
        EXPECT_FALSE(off.Value().handled);
        EXPECT_EQ(off.Value().notices,
                  (std::vector<ChatCommandNotice>{{Client::Chat::ShopViewOffNoticeText, Client::Ui::ErrorMessage}}));
        const auto on = Client::Chat::RunTheChatCommand(context, "/ver puestos", false);
        ASSERT_TRUE(on.HasValue()) << on.Error();
        EXPECT_EQ(signs, 1);
        EXPECT_FALSE(on.Value().handled);
        EXPECT_EQ(on.Value().notices,
                  (std::vector<ChatCommandNotice>{{Client::Chat::ShopViewOnNoticeText, Client::Ui::SystemMessage}}));
    }
    const auto cut = Client::Chat::RunTheChatCommand(Context(), "/ver puestos", false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x56d722"), std::string::npos) << cut.Error();
}

TEST_F(Commands, MoveOpensTheWindowAndGoesOn)
{
    const auto outcome = Client::Chat::RunTheChatCommand(Context(), "/mover", false);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_EQ(overlayMode, Client::Ui::MoveOverlayMode);
    overlayMode = 0;
    ASSERT_TRUE(Client::Chat::RunTheChatCommand(Context(), "/MOVE", false).HasValue());
    EXPECT_EQ(overlayMode, Client::Ui::MoveOverlayMode);
}

// 0x56efdc: "/N " stores the rest in its macro and plays a sound; a command a macro cannot store goes out as chat.
TEST_F(Commands, TheNumbersKeepTheMacros)
{
    const auto first = Client::Chat::RunTheChatCommand(Context(), "/1 hola", false);
    ASSERT_TRUE(first.HasValue());
    EXPECT_TRUE(first.Value().handled);
    EXPECT_TRUE(first.Value().sound);
    EXPECT_EQ(macros[0], "hola");

    ASSERT_TRUE(Client::Chat::RunTheChatCommand(Context(), "/0 chau", false).HasValue());
    EXPECT_EQ(macros[9], "chau");

    const auto forbidden = Client::Chat::RunTheChatCommand(Context(), "/2 /grupo", false);
    ASSERT_TRUE(forbidden.HasValue());
    EXPECT_FALSE(forbidden.Value().handled);
    EXPECT_EQ(macros[1], "");
    EXPECT_TRUE(Client::Chat::IsAForbiddenMacro(texts, "/3 /pt"));
    EXPECT_FALSE(Client::Chat::IsAForbiddenMacro(texts, "/3 /mover"));
}

// From a macro (or with the logout requested), only the names tail: "/1 x" changes nothing.
TEST_F(Commands, FromAMacroOnlyTheNamesAreLookedAt)
{
    const auto outcome = Client::Chat::RunTheChatCommand(Context(), "/1 x", true);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_EQ(macros[0], "");
    map = 18;
    logoutRequested = true;
    const auto logout = Client::Chat::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_TRUE(logout.HasValue());
    EXPECT_TRUE(logout.Value().notices.empty());
}

// 0x56f02e: /filter (text 753 or the literal) sets the words and warns with 755.
TEST_F(Commands, TheFilterCommandSetsTheWordsAndWarns)
{
    const auto outcome = Client::Chat::RunTheChatCommand(Context(), "/filtro uno", false);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1u);
    EXPECT_EQ(outcome.Value().notices[0].type, Client::Ui::SystemMessage);
    EXPECT_EQ(log.filter[0], "uno");
}

// 0x576894..0x576952: "/" and an item's name (with +0x24) turns the set label off, opens the item's help (mode 3 with
// the item in 0x77f3b0c), plays a sound and sends nothing.
TEST_F(Commands, AnItemNameOpensItsHelp)
{
    constexpr std::array<std::size_t, 1> Named{7};
    const auto table =
        TableWith<Client::Tables::ItemRecordSize>(16, Named, "Espadin", 0x24, std::byte{1}, std::byte{0});
    items = &table;
    setLabel = 4;
    const auto outcome = Client::Chat::RunTheChatCommand(Context(), "/Espadin", false);
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().sound);
    EXPECT_EQ(overlayMode, Client::Ui::ItemHelpMode);
    EXPECT_EQ(helpItem, 7);
    EXPECT_EQ(setLabel, 0);
}

// 0x40ebd0: "/" and the name of a used set turns its label on (+0x9305 at its index + 1) and sets 0x77f3b08 to 0;
// another text with "/" turns it off if some set is used, and one without "/" leaves it. It is never a command
// (0x576912).
TEST(SetCommands, TheNameOfASetShowsItsLabel)
{
    std::vector<std::byte> plain(Client::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::string_view name = "Delta";
    const std::size_t offset = 2 * Client::Tables::ItemSetOptionRecordSize;
    for (std::size_t i = 0; i < name.size(); ++i)
        plain[offset + i] = static_cast<std::byte>(name[i]);
    plain[offset + name.size()] = std::byte{0};
    plain[offset + 0x3c] = std::byte{4};
    const Client::Tables::ItemSetTables tables = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0}), plain);
    const Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    ChatLogState log;
    std::array<std::string, MacroCount> macros{};
    std::int32_t overlayMode = 3;
    std::int32_t helpItem = 0;
    std::uint8_t label = 9;
    const ChatCommandContext context{.texts = texts,
                                     .items = nullptr,
                                     .setOptions = &tables.options,
                                     .map = 0,
                                     .logoutRequested = false,
                                     .warehouseOpen = false,
                                     .log = log,
                                     .macros = macros,
                                     .overlayMode = overlayMode,
                                     .helpItem = helpItem,
                                     .setLabel = &label};
    const auto shown = Client::Chat::RunTheChatCommand(context, "/Delta", false);
    ASSERT_TRUE(shown.HasValue()) << shown.Error();
    EXPECT_FALSE(shown.Value().handled);
    EXPECT_FALSE(shown.Value().sound);
    EXPECT_EQ(label, 3);
    EXPECT_EQ(overlayMode, 0);

    overlayMode = 3;
    ASSERT_TRUE(Client::Chat::RunTheChatCommand(context, "/Delt", false).HasValue());
    EXPECT_EQ(label, 0);
    EXPECT_EQ(overlayMode, 3);
    label = 5;
    ASSERT_TRUE(Client::Chat::RunTheChatCommand(context, "Delta", false).HasValue());
    EXPECT_EQ(label, 5);
}
} // namespace
