#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Messenger/FriendModel.h"
#include "Ui/OldWindows.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The letters at [0x576351c], the rooms at [0x5763520] and what the chat with a friend keeps (CL9 milestone 10; 06 §
// HUD y ventanas, "Las cartas y el chat con un amigo"). The names and texts are made up.
namespace
{
using Client::Messenger::LetterModel;
using Client::Ui::OldWindow;
using Client::Ui::OldWindowTexts;

// 10 columns per letter, to count 0x459c70's rows by hand.
class TenWide final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 10, 13};
    }
};

std::vector<std::uint16_t> IndicesOf(const LetterModel& model)
{
    std::vector<std::uint16_t> indices;
    for (const auto& letter : model.letters)
        indices.push_back(letter.index);
    return indices;
}

// 0x49eb60: a letter with a number already present does not enter; the texts are cut at 10, 0x20, 0x10 and 0x10.
TEST(LetterModelTest, ALetterEntersOnceWithItsTextsCut)
{
    LetterModel model;
    Client::Messenger::AddALetter(model, 4, "Calandriaaaa", std::string(0x30, 's'), "2026/11/13 extra",
                                  "10:20:30 y algo mas", 0);
    Client::Messenger::AddALetter(model, 4, "Otro", "Otro", "", "", 1);
    ASSERT_EQ(model.letters.size(), 1u);
    EXPECT_EQ(model.letters[0].name, "Calandriaa");
    EXPECT_EQ(model.letters[0].subject, std::string(0x20, 's'));
    EXPECT_EQ(model.letters[0].date, "2026/11/13 extra");
    EXPECT_EQ(model.letters[0].time, "10:20:30 y algo ");
    EXPECT_EQ(model.letters[0].type, 0);
}

// 0x49f870 with 2 (0x49f7f0: the date and, with the same date, the time) and −1 (the previous one); 0x49c190 compares
// signed.
TEST(LetterModelTest, TheSortKeepsItsModeAndComparesWithSign)
{
    LetterModel model;
    Client::Messenger::AddALetter(model, 1, "Tero", "a", "2026/11/14", "08:00", 0);
    Client::Messenger::AddALetter(model, 2, "Tero", "b", "2026/11/13", "09:00", 0);
    Client::Messenger::AddALetter(model, 3, "Tero", "c", "2026/11/13", "07:00", 0);
    Client::Messenger::SortTheLetters(model, Client::Messenger::ByDateLetters);
    EXPECT_EQ(IndicesOf(model), (std::vector<std::uint16_t>{3, 2, 1}));
    Client::Messenger::AddALetter(model, 4, "Tero", "d", "2026/11/12", "07:00", 0);
    Client::Messenger::SortTheLetters(model, Client::Messenger::SameLetters);
    EXPECT_EQ(model.sortMode, Client::Messenger::ByDateLetters);
    EXPECT_EQ(IndicesOf(model), (std::vector<std::uint16_t>{4, 3, 2, 1}));
    bool same = false;
    EXPECT_TRUE(Client::Messenger::TextComesAfter("b", "a", &same));
    EXPECT_FALSE(same);
    EXPECT_FALSE(Client::Messenger::TextComesAfter("\xe1", "a"));
    EXPECT_FALSE(Client::Messenger::TextComesAfter("ab", "abc", &same));
    EXPECT_TRUE(same);
}

// 0x4a03c0 does not overwrite a copy; 0x4a0500 releases it and 0x49f260 releases everything.
TEST(LetterModelTest, TheCopyOfALetterIsKeptOnce)
{
    LetterModel model;
    Client::Messenger::KeepTheLetter(model, 7, {std::byte{1}});
    Client::Messenger::KeepTheLetter(model, 7, {std::byte{2}});
    ASSERT_NE(Client::Messenger::KeptLetter(model, 7), nullptr);
    EXPECT_EQ(*Client::Messenger::KeptLetter(model, 7), std::vector<std::byte>{std::byte{1}});
    Client::Messenger::AddALetter(model, 7, "Tero", "a", "", "", 0);
    Client::Messenger::ClearTheLetters(model);
    EXPECT_TRUE(model.letters.empty());
    EXPECT_EQ(Client::Messenger::KeptLetter(model, 7), nullptr);
}

// 0x49e280: from the cursor, the first free one, wrapping after 0xff; with the 0x100 taken, none.
TEST(ChatRoomsTest, ASlotIsTakenFromTheCursorUntilNoneIsLeft)
{
    Client::Messenger::ChatRooms rooms;
    rooms.cursor = 0xff;
    EXPECT_EQ(Client::Messenger::TakeAChatSlot(rooms, 10), std::optional<std::uint8_t>(std::uint8_t{0xff}));
    EXPECT_EQ(Client::Messenger::TakeAChatSlot(rooms, 11), std::optional<std::uint8_t>(std::uint8_t{0}));
    for (std::uint32_t room = 12; room < 12 + 0xfe; ++room)
        ASSERT_TRUE(Client::Messenger::TakeAChatSlot(rooms, room).has_value());
    EXPECT_EQ(Client::Messenger::TakeAChatSlot(rooms, 999), std::nullopt);
    EXPECT_EQ(rooms.slots.at(0xff), 10u);
}

// 0x495530: the chat's friend is the first member that is not the hero, with fewer than 3; with 3 or more, the room;
// 0x495630 puts and removes the 995 in front of the title and the EDIT's +0xb4 mark (the vtable + 0x58), without
// touching its state.
TEST(ChatWindowTest, TheFriendOfAChatAndItsClosedTitle)
{
    const OldWindowTexts texts{.notice = {}, .question = {}, .yes = {}, .no = {}, .lookup = [](std::size_t index) {
                                   return index == Client::Messenger::ChatClosedText ? std::string("[x] ")
                                                                                     : std::string();
                               }};
    OldWindow chat;
    chat.kind = Client::Ui::OldWindowKind::ChatWindow;
    chat.frame.title = "Chat Tordo";
    chat.chat.members.push_back(
        Client::Messenger::FriendEntry{.selected = 0, .name = "Heroico", .flag = 0, .state = 0});
    std::int32_t kind = -1;
    EXPECT_EQ(Client::Messenger::ChatFriend(chat, "Heroico", kind), "");
    EXPECT_EQ(kind, 0);
    chat.chat.members.push_back(Client::Messenger::FriendEntry{.selected = 0, .name = "Tordo", .flag = 1, .state = 0});
    EXPECT_EQ(Client::Messenger::ChatFriend(chat, "Heroico", kind), "Tordo");
    EXPECT_EQ(kind, 1);
    chat.chat.members.push_back(Client::Messenger::FriendEntry{.selected = 0, .name = "Zorzal", .flag = 2, .state = 0});
    EXPECT_EQ(Client::Messenger::ChatFriend(chat, "Heroico", kind), "");
    EXPECT_EQ(kind, 2);

    Client::Messenger::CloseTheChat(chat, 1, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("[x] Chat Tordo"));
    EXPECT_EQ(chat.chat.edit.closed, 1);
    EXPECT_EQ(chat.chat.edit.base.state, 0);
    Client::Messenger::CloseTheChat(chat, 1, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("[x] Chat Tordo"));
    Client::Messenger::CloseTheChat(chat, 0, texts);
    EXPECT_EQ(chat.frame.title, std::optional<std::string>("Chat Tordo"));
    EXPECT_EQ(chat.chat.edit.closed, 0);
    EXPECT_EQ(chat.chat.edit.base.state, 0);

    // 0x493f80: the row carries the name of the member with that number (0x461530); with 0xff, none. With fewer than 20
    // bytes there is no need to measure; with the name and the text empty, 0x45f6f0 adds nothing.
    Client::Ui::OldWindowManager manager;
    ASSERT_TRUE(Client::Messenger::AddAChatLine(manager, chat, 1, "hola", 3, 0, nullptr).HasValue());
    ASSERT_TRUE(Client::Messenger::AddAChatLine(manager, chat, 0xff, "aviso", 1, 0, nullptr).HasValue());
    ASSERT_TRUE(Client::Messenger::AddAChatLine(manager, chat, 0xff, "", 1, 0, nullptr).HasValue());
    ASSERT_EQ(chat.chat.lineRows.size(), 2u);
    EXPECT_EQ(chat.chat.lineRows[0].name, "Tordo");
    EXPECT_EQ(chat.chat.lineRows[1].name, "");
    ASSERT_EQ(chat.chat.wrappedRows.size(), 2u);
    EXPECT_EQ(chat.chat.wrappedRows[0].text, "aviso");
    EXPECT_EQ(chat.chat.wrappedRows[1].text, "hola");
    EXPECT_EQ(chat.chat.lines.fixedCountValue, 2);
    // A long one without the measure stops.
    const auto unmeasured =
        Client::Messenger::AddAChatLine(manager, chat, 1, "un texto inventado bastante largo", 3, 0, nullptr);
    ASSERT_FALSE(unmeasured.HasValue());
    EXPECT_NE(unmeasured.Error().find("0x4601a8"), std::string::npos);
}

// Q-C1012: with too much text, 0x459c70 gives one more than the rows it asked for. 0x460308 reads the 11 from EBP −
// 0x14 and 0x465450 the 0x51 after 0x5762e34; MuRet uses the ones it asked for and drops the extra one. With 40 wide
// (the list minus 0x1e) and 10 columns per letter: the chat's first row carries the indent of 5 (no name) and 3 fit;
// the others, 4.
TEST(ChatWindowTest, TheTextThatDoesNotFitIsLeftOut)
{
    const TenWide letters;
    const Client::Ui::OldWindowMeasure measure{.measure = letters,
                                               .dcFont = Client::Render::Text::FontKind::Normal,
                                               .screen = Client::Core::PixelSize{640, 480}};
    Client::Ui::OldWindowManager manager;

    OldWindow chat;
    chat.kind = Client::Ui::OldWindowKind::ChatWindow;
    chat.chat.lines.base.width = 0x1e + 40;
    ASSERT_TRUE(Client::Messenger::AddAChatLine(manager, chat, 0xff, std::string(100, 'W'), 1, 0, &measure).HasValue());
    ASSERT_EQ(chat.chat.wrappedRows.size(), 10u);
    EXPECT_EQ(chat.chat.wrappedRows.back().text, "WWW");
    EXPECT_EQ(chat.chat.wrappedRows.front().text, "WWWW");
    EXPECT_EQ(chat.chat.lines.fixedCountValue, 10);

    OldWindow read;
    read.kind = Client::Ui::OldWindowKind::LetterReadWindow;
    read.letter.lines.base.width = 0x1e + 40;
    ASSERT_TRUE(Client::Messenger::SetTheLetter(manager, read, Client::Messenger::LetterEntry{}, std::string(400, 'W'),
                                                &measure)
                    .HasValue());
    ASSERT_EQ(read.letter.wrappedRows.size(), 0x50u);
    EXPECT_EQ(read.letter.wrappedRows.back(), "WWWW");
    EXPECT_EQ(read.letter.lines.fixedCountValue, 0x50);
}
} // namespace
