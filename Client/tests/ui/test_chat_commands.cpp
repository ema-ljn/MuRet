#include "UI/ChatCommands.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/RecordTable.h"
#include "Data/Text/TextTable.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"

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

// Los comandos del chat (0x56c440; CL8 hito 3): las compuertas, /move, /filter, /1../0 y los cortes. Los textos son
// inventados (support/ChatTexts.h).
namespace
{
using Client::UI::ChatCommandContext;
using Client::UI::ChatCommandNotice;
using Client::UI::ChatLogState;
using Client::UI::MacroCount;

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

    const Client::Data::Tables::ItemTable* items = nullptr;
    const Client::Data::Tables::ItemSetOptionTable* setOptions = nullptr;

    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::int32_t map = 0;
    bool logoutRequested = false;
    bool warehouseOpen = false;
    ChatLogState log;
    std::array<std::string, MacroCount> macros{};
    std::int32_t overlayMode = 0;
    std::int32_t helpItem = 0;
    std::uint8_t setLabel = 0;
};

// Una tabla de `count` registros de `Size` bytes con `name` en los lugares de `named` y `flag` en el byte `at` de
// cada uno, cifrada con XOR3 como la lee RecordTable.
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
        Client::Core::Xor3(record);
    }
    return Client::Data::Tables::RecordTable<Size>(bytes);
}

TEST_F(Commands, AnythingElseIsNotACommand)
{
    const auto outcome = Client::UI::RunTheChatCommand(Context(), "hola a todos", false);
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_FALSE(outcome.Value().sound);
}

// Cada comando social empieza con el aviso de Chaos Castle (y sale como chat); afuera, lo que sigue es de su ola.
TEST_F(Commands, TheSocialCommandsStopInChaosCastle)
{
    map = 18;
    for (const std::string_view command : {"/comerciar", "/TRADE algo", "/puesto", "/retar", "/clan", "/grupo"})
    {
        const auto outcome = Client::UI::RunTheChatCommand(Context(), command, false);
        ASSERT_TRUE(outcome.HasValue()) << command;
        EXPECT_FALSE(outcome.Value().handled);
        EXPECT_EQ(outcome.Value().notices,
                  (std::vector<ChatCommandNotice>{{Client::UI::ChaosCastleCommandText, Client::UI::SystemMessage}}));
    }
    map = 0;
    const auto trade = Client::UI::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_FALSE(trade.HasValue());
    EXPECT_NE(trade.Error().find("0x56c5d0"), std::string::npos);
    // Con el almacén abierto, el comercio no se mira.
    warehouseOpen = true;
    const auto skipped = Client::UI::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_TRUE(skipped.HasValue());
    EXPECT_FALSE(skipped.Value().handled);
}

// 0x56c5d0..0x56c690: /trade lee el nivel del registro propio; por debajo de 0x6c8dc8 (6), el aviso 478 y 1. Con él,
// el pedido (Scenes::RequestATrade) da la salida; sin pedido, corta.
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
    const auto low = Client::UI::RunTheChatCommand(context, "/trade", false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(low.Value().handled);
    EXPECT_EQ(low.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::UI::TradeLevelText, Client::UI::SystemMessage}}));
    EXPECT_EQ(reads, 1);
    level = 6;
    const auto unwired = Client::UI::RunTheChatCommand(context, "/comerciar", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x56c68b"), std::string::npos);
    int asked = 0;
    context.requestATrade = [&asked]() -> Client::Core::Expected<Client::UI::ChatCommandOutcome, std::string>
    {
        ++asked;
        return Client::UI::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    const auto enough = Client::UI::RunTheChatCommand(context, "/comerciar", false);
    ASSERT_TRUE(enough.HasValue()) << enough.Error();
    EXPECT_TRUE(enough.Value().handled);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(reads, 3);
}

// 0x5766ce..0x576890: /personalshop. Con el entrenador abierto, el aviso 1121 y sale como chat; por debajo del 6, el
// 1123 armado con el 6 y 1; desde el 6, Open(0x800) y 1. Cada vez que llega al nivel lo lee una vez.
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
    const auto trainer = Client::UI::RunTheChatCommand(context, "/personalshop", false);
    ASSERT_TRUE(trainer.HasValue()) << trainer.Error();
    EXPECT_FALSE(trainer.Value().handled);
    EXPECT_EQ(trainer.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::UI::ShopTrainerText, Client::UI::SystemMessage}}));
    EXPECT_EQ(reads, 0);
    context.trainerOpen = false;
    const auto low = Client::UI::RunTheChatCommand(context, "/puesto", false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(low.Value().handled);
    EXPECT_EQ(low.Value().notices, (std::vector<ChatCommandNotice>{{.text = Client::UI::ShopLevelText,
                                                                    .type = Client::UI::SystemMessage,
                                                                    .formatted = "desde el nivel 6"}}));
    level = 6;
    const auto unwired = Client::UI::RunTheChatCommand(context, "/PersonalShop", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x576822"), std::string::npos);
    context.openThePersonalShop = [&opened]() -> Client::Core::Expected<void, std::string>
    {
        ++opened;
        return {};
    };
    const auto enough = Client::UI::RunTheChatCommand(context, "/puesto", false);
    ASSERT_TRUE(enough.HasValue()) << enough.Error();
    EXPECT_TRUE(enough.Value().handled);
    EXPECT_TRUE(enough.Value().notices.empty());
    EXPECT_EQ(opened, 1);
    EXPECT_EQ(reads, 3);
}

// 0x574bd6..0x57694d: /purchase (o el 1118) en cualquier lugar del texto. En Chaos Castle, el aviso 1150 y sale como
// chat; fuera, sscanf("%s %s") y el pedido recibe la segunda palabra, o nada si no la hay (la pila sin inicializar).
TEST_F(Commands, PurchaseScansTheNameForTheRequest)
{
    std::vector<std::optional<std::string>> names;
    ChatCommandContext context = Context();
    const auto unwired = Client::UI::RunTheChatCommand(context, "/purchase", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x574c0c"), std::string::npos);
    context.requestShopItems = [&names](std::optional<std::string_view> name)
        -> Client::Core::Expected<Client::UI::ChatCommandOutcome, std::string>
    {
        names.emplace_back(name ? std::optional<std::string>(*name) : std::nullopt);
        return Client::UI::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    for (const std::string_view command :
         {"/purchase", "/adquirir Brezal", "  /purchase\t Zorzal  extra", "mira /adquirir", "/adquirir \t "})
    {
        const auto outcome = Client::UI::RunTheChatCommand(context, command, false);
        ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
        EXPECT_TRUE(outcome.Value().handled);
    }
    EXPECT_EQ(names,
              (std::vector<std::optional<std::string>>{std::nullopt, "Brezal", "Zorzal", "/adquirir", std::nullopt}));
    // Una primera palabra de 0x48 bytes pisa el buffer del nombre: MuRet no lo modela.
    const auto overflow = Client::UI::RunTheChatCommand(context, std::string(0x48, 'a') + "/purchase", false);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x574c92"), std::string::npos);
    EXPECT_EQ(names.size(), 5U);
    context.map = 23;
    const auto inside = Client::UI::RunTheChatCommand(context, "/purchase Brezal", false);
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_FALSE(inside.Value().handled);
    EXPECT_EQ(inside.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::UI::ChaosCastleCommandText, Client::UI::SystemMessage}}));
    EXPECT_EQ(names.size(), 5U);
}

// 0x56f109..0x56f13f: /party (o /pt) en Chaos Castle avisa 1150 y sale como chat; fuera, el pedido da la salida.
TEST_F(Commands, PartyAsksOutsideChaosCastle)
{
    int asked = 0;
    ChatCommandContext context = Context();
    context.requestAParty = [&asked]() -> Client::Core::Expected<Client::UI::ChatCommandOutcome, std::string>
    {
        ++asked;
        return Client::UI::ChatCommandOutcome{.handled = true, .notices = {}};
    };
    const auto outside = Client::UI::RunTheChatCommand(context, "/PT", false);
    ASSERT_TRUE(outside.HasValue()) << outside.Error();
    EXPECT_TRUE(outside.Value().handled);
    EXPECT_EQ(asked, 1);
    map = 18;
    const auto inside = Client::UI::RunTheChatCommand(Context(), "/grupo", false);
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_FALSE(inside.Value().handled);
    EXPECT_EQ(inside.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::UI::ChaosCastleCommandText, Client::UI::SystemMessage}}));
}

// 0x56d816 y 0x56ea5f: con el duelo en curso, /duelstart avisa 915 y sigue la cadena; /duelend manda el AB y sigue.
// Sin duelo, /duelend solo sigue y /duelstart pide el duelo (Scenes::RequestADuel) y también sigue.
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
    const auto busy = Client::UI::RunTheChatCommand(context, "/retar", false);
    ASSERT_TRUE(busy.HasValue()) << busy.Error();
    EXPECT_FALSE(busy.Value().handled);
    EXPECT_EQ(busy.Value().notices,
              (std::vector<ChatCommandNotice>{{Client::UI::DuelBusyText, Client::UI::ErrorMessage}}));
    EXPECT_EQ(ended, 0);
    const auto end = Client::UI::RunTheChatCommand(context, "/DUELEND", false);
    ASSERT_TRUE(end.HasValue()) << end.Error();
    EXPECT_FALSE(end.Value().handled);
    EXPECT_EQ(ended, 1);
    ASSERT_TRUE(Client::UI::RunTheChatCommand(context, "/noretar", false).HasValue());
    EXPECT_EQ(ended, 2);

    context.duelActive = false;
    ASSERT_TRUE(Client::UI::RunTheChatCommand(context, "/noretar", false).HasValue());
    EXPECT_EQ(ended, 2);
    const auto unwired = Client::UI::RunTheChatCommand(context, "/duelstart", false);
    ASSERT_FALSE(unwired.HasValue());
    EXPECT_NE(unwired.Error().find("0x56d823"), std::string::npos);
    int asked = 0;
    context.requestADuel = [&asked]() -> Client::Core::Expected<void, std::string>
    {
        ++asked;
        return {};
    };
    const auto start = Client::UI::RunTheChatCommand(context, "/duelstart", false);
    ASSERT_TRUE(start.HasValue()) << start.Error();
    EXPECT_FALSE(start.Value().handled);
    EXPECT_TRUE(start.Value().notices.empty());
    EXPECT_EQ(asked, 1);
    const auto guild = Client::UI::RunTheChatCommand(context, "/guild", false);
    ASSERT_FALSE(guild.HasValue());
    EXPECT_NE(guild.Error().find("hito 8"), std::string::npos);
}

// 0x56d624: /move abre la ventana y sigue como chat.
TEST_F(Commands, MoveOpensTheWindowAndGoesOn)
{
    const auto outcome = Client::UI::RunTheChatCommand(Context(), "/mover", false);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_EQ(overlayMode, Client::UI::MoveOverlayMode);
    overlayMode = 0;
    ASSERT_TRUE(Client::UI::RunTheChatCommand(Context(), "/MOVE", false).HasValue());
    EXPECT_EQ(overlayMode, Client::UI::MoveOverlayMode);
}

// 0x56efdc: "/N " guarda lo que sigue en su macro y suena; un comando que una macro no puede guardar sale como chat.
TEST_F(Commands, TheNumbersKeepTheMacros)
{
    const auto first = Client::UI::RunTheChatCommand(Context(), "/1 hola", false);
    ASSERT_TRUE(first.HasValue());
    EXPECT_TRUE(first.Value().handled);
    EXPECT_TRUE(first.Value().sound);
    EXPECT_EQ(macros[0], "hola");

    ASSERT_TRUE(Client::UI::RunTheChatCommand(Context(), "/0 chau", false).HasValue());
    EXPECT_EQ(macros[9], "chau");

    const auto forbidden = Client::UI::RunTheChatCommand(Context(), "/2 /grupo", false);
    ASSERT_TRUE(forbidden.HasValue());
    EXPECT_FALSE(forbidden.Value().handled);
    EXPECT_EQ(macros[1], "");
    EXPECT_TRUE(Client::UI::IsAForbiddenMacro(texts, "/3 /pt"));
    EXPECT_FALSE(Client::UI::IsAForbiddenMacro(texts, "/3 /mover"));
}

// Desde una macro (o con la salida pedida), solo la cola de los nombres: "/1 x" no cambia nada.
TEST_F(Commands, FromAMacroOnlyTheNamesAreLookedAt)
{
    const auto outcome = Client::UI::RunTheChatCommand(Context(), "/1 x", true);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    EXPECT_EQ(macros[0], "");
    map = 18;
    logoutRequested = true;
    const auto logout = Client::UI::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_TRUE(logout.HasValue());
    EXPECT_TRUE(logout.Value().notices.empty());
}

// 0x56f02e: /filter (el texto 753 o el literal) pone las palabras y avisa con el 755.
TEST_F(Commands, TheFilterCommandSetsTheWordsAndWarns)
{
    const auto outcome = Client::UI::RunTheChatCommand(Context(), "/filtro uno", false);
    ASSERT_TRUE(outcome.HasValue());
    EXPECT_FALSE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1u);
    EXPECT_EQ(outcome.Value().notices[0].type, Client::UI::SystemMessage);
    EXPECT_EQ(log.filter[0], "uno");
}

// 0x576894..0x576952: "/" y el nombre de un ítem (con +0x24) apaga el cartel del conjunto, abre la ayuda del ítem (el
// modo 3 con el ítem en 0x77f3b0c), suena y no manda nada.
TEST_F(Commands, AnItemNameOpensItsHelp)
{
    constexpr std::array<std::size_t, 1> Named{7};
    const auto table =
        TableWith<Client::Data::Tables::ItemRecordSize>(16, Named, "Espadin", 0x24, std::byte{1}, std::byte{0});
    items = &table;
    setLabel = 4;
    const auto outcome = Client::UI::RunTheChatCommand(Context(), "/Espadin", false);
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().sound);
    EXPECT_EQ(overlayMode, Client::UI::ItemHelpMode);
    EXPECT_EQ(helpItem, 7);
    EXPECT_EQ(setLabel, 0);
}

// 0x40ebd0: "/" y el nombre de un conjunto usado prende su cartel (+0x9305 en su índice + 1) y pone 0x77f3b08 en 0;
// otro texto con "/" lo apaga si hay algún conjunto usado, y uno sin "/" no lo toca. Nunca es un comando (0x576912).
TEST(SetCommands, TheNameOfASetShowsItsLabel)
{
    std::vector<std::byte> plain(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::string_view name = "Delta";
    const std::size_t offset = 2 * Client::Data::Tables::ItemSetOptionRecordSize;
    for (std::size_t i = 0; i < name.size(); ++i)
        plain[offset + i] = static_cast<std::byte>(name[i]);
    plain[offset + name.size()] = std::byte{0};
    plain[offset + 0x3c] = std::byte{4};
    const Client::Data::Tables::ItemSetTables tables = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0}), plain);
    const Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
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
    const auto shown = Client::UI::RunTheChatCommand(context, "/Delta", false);
    ASSERT_TRUE(shown.HasValue()) << shown.Error();
    EXPECT_FALSE(shown.Value().handled);
    EXPECT_FALSE(shown.Value().sound);
    EXPECT_EQ(label, 3);
    EXPECT_EQ(overlayMode, 0);

    overlayMode = 3;
    ASSERT_TRUE(Client::UI::RunTheChatCommand(context, "/Delt", false).HasValue());
    EXPECT_EQ(label, 0);
    EXPECT_EQ(overlayMode, 3);
    label = 5;
    ASSERT_TRUE(Client::UI::RunTheChatCommand(context, "Delta", false).HasValue());
    EXPECT_EQ(label, 5);
}
} // namespace
