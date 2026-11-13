#include "UI/LetterModel.h"
#include "UI/LetterWindows.h"

#include "UI/FriendModel.h"
#include "UI/OldWindows.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Las cartas de [0x576351c], las salas de [0x5763520] y lo que el chat con un amigo guarda (CL9 hito 10; 06 § HUD y
// ventanas, "Las cartas y el chat con un amigo"). Los nombres y los textos son inventados.
namespace
{
using Client::UI::LetterModel;
using Client::UI::OldWindow;
using Client::UI::OldWindowTexts;

std::vector<std::uint16_t> IndicesOf(const LetterModel& model)
{
    std::vector<std::uint16_t> indices;
    for (const auto& letter : model.letters)
        indices.push_back(letter.index);
    return indices;
}

// 0x49eb60: una carta con un número que ya está no entra; los textos se cortan en 10, 0x20, 0x10 y 0x10.
TEST(LetterModelTest, ALetterEntersOnceWithItsTextsCut)
{
    LetterModel model;
    Client::UI::AddALetter(model, 4, "Calandriaaaa", std::string(0x30, 's'), "2026/11/13 extra", "10:20:30 y algo mas",
                           0);
    Client::UI::AddALetter(model, 4, "Otro", "Otro", "", "", 1);
    ASSERT_EQ(model.letters.size(), 1u);
    EXPECT_EQ(model.letters[0].name, "Calandriaa");
    EXPECT_EQ(model.letters[0].subject, std::string(0x20, 's'));
    EXPECT_EQ(model.letters[0].date, "2026/11/13 extra");
    EXPECT_EQ(model.letters[0].time, "10:20:30 y algo ");
    EXPECT_EQ(model.letters[0].type, 0);
}

// 0x49f870 con 2 (0x49f7f0: la fecha y, con la fecha igual, la hora) y −1 (el que estaba); 0x49c190 compara con signo.
TEST(LetterModelTest, TheSortKeepsItsModeAndComparesWithSign)
{
    LetterModel model;
    Client::UI::AddALetter(model, 1, "Tero", "a", "2026/11/14", "08:00", 0);
    Client::UI::AddALetter(model, 2, "Tero", "b", "2026/11/13", "09:00", 0);
    Client::UI::AddALetter(model, 3, "Tero", "c", "2026/11/13", "07:00", 0);
    Client::UI::SortTheLetters(model, Client::UI::ByDateLetters);
    EXPECT_EQ(IndicesOf(model), (std::vector<std::uint16_t>{3, 2, 1}));
    Client::UI::AddALetter(model, 4, "Tero", "d", "2026/11/12", "07:00", 0);
    Client::UI::SortTheLetters(model, Client::UI::SameLetters);
    EXPECT_EQ(model.sortMode, Client::UI::ByDateLetters);
    EXPECT_EQ(IndicesOf(model), (std::vector<std::uint16_t>{4, 3, 2, 1}));
    bool same = false;
    EXPECT_TRUE(Client::UI::TextComesAfter("b", "a", &same));
    EXPECT_FALSE(same);
    EXPECT_FALSE(Client::UI::TextComesAfter("\xe1", "a"));
    EXPECT_FALSE(Client::UI::TextComesAfter("ab", "abc", &same));
    EXPECT_TRUE(same);
}

// 0x4a03c0 no pisa una copia; 0x4a0500 la suelta y 0x49f260 suelta todo.
TEST(LetterModelTest, TheCopyOfALetterIsKeptOnce)
{
    LetterModel model;
    Client::UI::KeepTheLetter(model, 7, {std::byte{1}});
    Client::UI::KeepTheLetter(model, 7, {std::byte{2}});
    ASSERT_NE(Client::UI::KeptLetter(model, 7), nullptr);
    EXPECT_EQ(*Client::UI::KeptLetter(model, 7), std::vector<std::byte>{std::byte{1}});
    Client::UI::AddALetter(model, 7, "Tero", "a", "", "", 0);
    Client::UI::ClearTheLetters(model);
    EXPECT_TRUE(model.letters.empty());
    EXPECT_EQ(Client::UI::KeptLetter(model, 7), nullptr);
}

// 0x49e280: desde el cursor, el primero libre, con vuelta después de 0xff; con los 0x100 ocupados, ninguno.
TEST(ChatRoomsTest, ASlotIsTakenFromTheCursorUntilNoneIsLeft)
{
    Client::UI::ChatRooms rooms;
    rooms.cursor = 0xff;
    EXPECT_EQ(Client::UI::TakeAChatSlot(rooms, 10), std::optional<std::uint8_t>(std::uint8_t{0xff}));
    EXPECT_EQ(Client::UI::TakeAChatSlot(rooms, 11), std::optional<std::uint8_t>(std::uint8_t{0}));
    for (std::uint32_t room = 12; room < 12 + 0xfe; ++room)
        ASSERT_TRUE(Client::UI::TakeAChatSlot(rooms, room).has_value());
    EXPECT_EQ(Client::UI::TakeAChatSlot(rooms, 999), std::nullopt);
    EXPECT_EQ(rooms.slots.at(0xff), 10u);
}

// 0x495530: el amigo del chat es el primer miembro que no es el héroe, con menos de 3; con 3 o más, la sala; 0x495630
// pone y saca el 995 adelante del título y apaga o prende el EDIT.
TEST(ChatWindowTest, TheFriendOfAChatAndItsClosedTitle)
{
    const OldWindowTexts texts{.notice = {}, .question = {}, .yes = {}, .no = {}, .lookup = [](std::size_t index) {
                                   return index == Client::UI::ChatClosedText ? std::string("[x] ") : std::string();
                               }};
    OldWindow chat;
    chat.kind = Client::UI::OldWindowKind::ChatWindow;
    chat.frame.title = "Chat Tordo";
    chat.chat.members.push_back(Client::UI::FriendEntry{.selected = 0, .name = "Heroico", .flag = 0, .state = 0});
    std::int32_t kind = -1;
    EXPECT_EQ(Client::UI::ChatFriend(chat, "Heroico", kind), "");
    EXPECT_EQ(kind, 0);
    chat.chat.members.push_back(Client::UI::FriendEntry{.selected = 0, .name = "Tordo", .flag = 1, .state = 0});
    EXPECT_EQ(Client::UI::ChatFriend(chat, "Heroico", kind), "Tordo");
    EXPECT_EQ(kind, 1);
    chat.chat.members.push_back(Client::UI::FriendEntry{.selected = 0, .name = "Zorzal", .flag = 2, .state = 0});
    EXPECT_EQ(Client::UI::ChatFriend(chat, "Heroico", kind), "");
    EXPECT_EQ(kind, 2);

    Client::UI::CloseTheChat(chat, 1, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("[x] Chat Tordo"));
    EXPECT_EQ(chat.chat.edit.base.state, 6);
    Client::UI::CloseTheChat(chat, 1, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("[x] Chat Tordo"));
    Client::UI::CloseTheChat(chat, 0, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("Chat Tordo"));
    EXPECT_EQ(chat.chat.edit.base.state, 0);

    // 0x493f80: el renglón lleva el nombre del miembro con ese número (0x461530); con 0xff, ninguno. Con menos de 20
    // bytes no hace falta medir; con el nombre y el texto vacíos, 0x45f6f0 no agrega nada.
    Client::UI::OldWindowManager manager;
    ASSERT_TRUE(Client::UI::AddAChatLine(manager, chat, 1, "hola", 3, 0, nullptr).HasValue());
    ASSERT_TRUE(Client::UI::AddAChatLine(manager, chat, 0xff, "aviso", 1, 0, nullptr).HasValue());
    ASSERT_TRUE(Client::UI::AddAChatLine(manager, chat, 0xff, "", 1, 0, nullptr).HasValue());
    ASSERT_EQ(chat.chat.lineRows.size(), 2u);
    EXPECT_EQ(chat.chat.lineRows[0].name, "Tordo");
    EXPECT_EQ(chat.chat.lineRows[1].name, "");
    ASSERT_EQ(chat.chat.wrappedRows.size(), 2u);
    EXPECT_EQ(chat.chat.wrappedRows[0].text, "aviso");
    EXPECT_EQ(chat.chat.wrappedRows[1].text, "hola");
    EXPECT_EQ(chat.chat.lines.fixedCountValue, 2);
    // Uno largo sin la medida corta.
    const auto unmeasured =
        Client::UI::AddAChatLine(manager, chat, 1, "un texto inventado bastante largo", 3, 0, nullptr);
    ASSERT_FALSE(unmeasured.HasValue());
    EXPECT_NE(unmeasured.Error().find("0x4601a8"), std::string::npos);
}
} // namespace
