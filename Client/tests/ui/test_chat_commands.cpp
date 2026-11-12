#include "UI/ChatCommands.h"

#include "Data/Tables/ItemSetTables.h"
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
                                  .items = nullptr,
                                  .setOptions = nullptr,
                                  .map = map,
                                  .logoutRequested = logoutRequested,
                                  .warehouseOpen = warehouseOpen,
                                  .log = log,
                                  .macros = macros,
                                  .overlayMode = overlayMode};
    }

    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::int32_t map = 0;
    bool logoutRequested = false;
    bool warehouseOpen = false;
    ChatLogState log;
    std::array<std::string, MacroCount> macros{};
    std::int32_t overlayMode = 0;
};

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
    EXPECT_NE(trade.Error().find("0x56c54d"), std::string::npos);
    // Con el almacén abierto, el comercio no se mira.
    warehouseOpen = true;
    const auto skipped = Client::UI::RunTheChatCommand(Context(), "/comerciar", false);
    ASSERT_TRUE(skipped.HasValue());
    EXPECT_FALSE(skipped.Value().handled);
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

// 0x576eb0: un texto de un emote en lo dicho corta (CL9.1); sin ninguno, nada.
TEST_F(Commands, AnEmoteIsCut)
{
    EXPECT_TRUE(Client::UI::CheckTheEmotes(texts, "hola").HasValue());
    const auto emote = Client::UI::CheckTheEmotes(texts, "ja #e300 ja");
    ASSERT_FALSE(emote.HasValue());
    EXPECT_NE(emote.Error().find("CL9.1"), std::string::npos);
    EXPECT_FALSE(Client::UI::CheckTheEmotes(texts, "#e783").HasValue());
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
                                     .setLabel = &label};
    const auto shown = Client::UI::RunTheChatCommand(context, "/Delta", false);
    ASSERT_TRUE(shown.HasValue()) << shown.Error();
    EXPECT_FALSE(shown.Value().handled);
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
