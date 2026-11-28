#include "Network/Xor32Packet.h"
#include "Chat/GameChat.h"

#include "Input/Key.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/TextTable.h"
#include "Input/TextInputEvent.h"
#include "Protocol/Outgoing.h"
#include "Models/ModelTextureSlots.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Assets/FaunaModels.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Platform/IClipboard.h"
#include "Ui/MessageArea.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Chat/WhisperSenders.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/FakeTextureStore.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"
#include "Social/SocialState.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The chat input in the game (CL8 milestone 3; 06 § El chat): Enter, what is typed, the C1 00 and the C1 02, and the
// macros. The texts and the names are made up.
namespace
{
using Client::Chat::GameChatContext;
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

// 0x554a40: the own record, opened and closed once.
constexpr int WhisperRandoms = 3848;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class FakeClipboard final : public Client::Platform::IClipboard
{
public:
    [[nodiscard]] std::optional<std::string> ReadText() override
    {
        ++reads;
        return readFails ? std::nullopt : contents;
    }

    [[nodiscard]] bool WriteText(std::string_view text) override
    {
        ++writes;
        if (writeFails)
            return false;
        contents = text;
        return true;
    }

    std::optional<std::string> contents;
    int reads = 0;
    int writes = 0;
    bool readFails = false;
    bool writeFails = false;
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// A packet in the clear: the type, the length, the code, the ten-byte name up to its '\0' and the text up to its own.
struct Said
{
    std::uint8_t opcode = 0;
    std::string name;
    std::string text;
};

Said Decode(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Xor32Decode(sent.packet);
    const auto bytes = plain.Bytes();
    Said said;
    said.opcode = std::to_integer<std::uint8_t>(bytes[2]);
    for (std::size_t i = 3; i < 13 && bytes[i] != std::byte{0}; ++i)
        said.name.push_back(static_cast<char>(bytes[i]));
    for (std::size_t i = 13; i < bytes.size() && bytes[i] != std::byte{0}; ++i)
        said.text.push_back(static_cast<char>(bytes[i]));
    return said;
}

class GameChat : public ::testing::Test
{
protected:
    GameChat()
    {
        SetTheHeroName("Heroe");
        world.OwnCharacter().level = 10;
        world.Hero().type = Client::Tables::PlayerSkeletonModel;
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    }

    void SetTheHeroName(std::string_view name)
    {
        auto& hero = world.Hero();
        hero.name.fill(std::byte{0});
        for (std::size_t i = 0; i < name.size() && i < hero.name.size(); ++i)
            hero.name[i] = static_cast<std::byte>(name[i]);
    }

    GameChatContext Context()
    {
        return GameChatContext{.world = world,
                               .random = random,
                               .texts = &texts,
                               .items = nullptr,
                               .setOptions = nullptr,
                               .filter = filter,
                               .measure = &letters,
                               .screen = {640, 480},
                               .send = [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); },
                               .clipboard = &clipboard,
                               .models = &models,
                               .faunaModels = &faunaModels};
    }

    Client::Core::Expected<void, std::string> Type(std::string_view typed)
    {
        return Client::Chat::TypeInTheGameChat(Context(), typed);
    }

    Client::Ui::ChatInputState& Input()
    {
        return world.Runtime().chat.input;
    }

    Client::Ui::OldEdit& FocusAnOldEdit()
    {
        auto& manager = world.Runtime().oldWindows;
        Client::Ui::OldWindow box;
        box.kind = Client::Ui::OldWindowKind::AddFriendBox;
        box.base.id = 10;
        box.add.edit.base.id = 11;
        box.add.edit.base.parent = box.base.id;
        auto [window, inserted] = manager.windows.emplace(box.base.id, std::move(box));
        EXPECT_TRUE(inserted);
        manager.focusedEdit = window->second.add.edit.base.id;
        Input().focus = Client::Ui::ChatFocus::OldWindow;
        return window->second.add.edit;
    }

    // The fauna models keep the backend until they are destroyed: it must outlive them, even if an ASSERT fails.
    Client::Tests::FakeTextureStore m_faunaStore;
    Client::Assets::FaunaModels faunaModels;
    WorldState world;
    CrtRandom random;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::vector<std::string> filter;
    LetterMeasure letters;
    std::vector<Client::Protocol::Outgoing> sent;
    Client::FrontEnd::SingleModelLibrary models;
    FakeClipboard clipboard;
};

// A C1 05 18 [direction] [action] in the clear, with the XOR32.
bool IsTheAction(const Client::Protocol::Outgoing& sent, std::uint8_t direction, std::uint8_t action)
{
    return Client::Tests::SameBytes(Client::Network::Xor32Decode(sent.packet).Bytes(),
                                    Client::Tests::Bytes({0xc1, 5, 0x18, direction, action}));
}

// 0x4b5468..0x4b5554: with the field empty, Enter opens it (the EDITs in view and the focus on the message one) or
// closes it.
TEST_F(GameChat, EnterOpensAndClosesTheEmptyField)
{
    const auto before = random.State();
    ASSERT_TRUE(Type("abc\r").HasValue());
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(Input().shown);
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Message);
    EXPECT_EQ(world.Runtime().ui.focusedList, Client::Ui::ChatMessageEditId);
    // What was there before opening was dropped by the game window.
    EXPECT_EQ(Input().message.text.Value(), "");

    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_FALSE(Input().shown);
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Game);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), before);
}

// The Enter cases with the zen and personal shop dialogs, in Lorencia and in Devias.
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// 0x4b54e1: the zen dialog of the trade and the warehouse.
constexpr std::int32_t MoneyDialog = 0x73;

class GameChatOnTheMaps : public GameChat, public ::testing::WithParamInterface<std::int32_t>
{
protected:
    GameChatOnTheMaps()
    {
        world.Runtime().map = GetParam();
    }
};

// 0x4b5515..0x4b554d: opening the chat leaves the field's length at 0x2a and turns off the zen mode (0x77f3b6a).
TEST_P(GameChatOnTheMaps, OpeningTheChatLeavesTheMoneyMode)
{
    world.Runtime().social.moneyField = 1;
    world.Runtime().social.dialogFieldLimit = 8;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(world.Runtime().social.moneyField, 0);
    EXPECT_EQ(world.Runtime().social.dialogFieldLimit, 0x2a);
}

// 0x4b54e1..0x4b5513: with the field closed, the EDIT of [0x5763504] empty (what is read) and the dialog 0x73, Enter
// does not open the chat: it sets the EDIT's text to 0 and the focus on the game window (the 0x73's draw gives it
// back, 0x6308b8).
TEST_P(GameChatOnTheMaps, TheMoneyDialogEmptiesItsEdit)
{
    world.Runtime().dialogs.current = MoneyDialog;
    Input().focus = Client::Ui::ChatFocus::Dialog;
    const auto before = random.State();
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "");
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Game);
    EXPECT_EQ(world.Runtime().dialogs.current, MoneyDialog);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), before);
}

// 0x4b5397..0x4b5435: with the field closed, what is read is the dialog's EDIT; with the zen mode, through atoi.
TEST_P(GameChatOnTheMaps, TheDialogEditIsReadWithTheFieldClosed)
{
    world.Runtime().dialogs.current = MoneyDialog;
    world.Runtime().social.moneyField = 1;
    Input().dialogEdit.text.SetText("250");
    Input().dialogEdit.base.state = Client::Ui::ShownWindow;
    Input().focus = Client::Ui::ChatFocus::Dialog;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_EQ(world.Runtime().social.typedMoney, 250);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "");
    EXPECT_EQ(Input().dialogEdit.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Game);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// 0x4b5563..0x4b55ae: with the zen mode, what was typed goes through atoi to 0x77f3b6c (the blanks, the sign and the
// digits up to the first one that is not) and is not said; then, the EDIT of [0x5763504] empty and hidden, and the
// jump to 0x4b8886, which closes the field.
TEST_P(GameChatOnTheMaps, TheTypedMoneyGoesThroughAtoiAndIsNotSaid)
{
    for (const auto& [typed, money] : {std::pair<std::string_view, std::int32_t>{" 1234\r", 1234},
                                       std::pair<std::string_view, std::int32_t>{"-12zen\r", -12}})
    {
        Client::Ui::ClearTheChatFields(Input());
        Client::Ui::ResetTheTextBuffers(Input(), false);
        world.Runtime().ui.chatOpen = false;
        ASSERT_TRUE(Type("\r").HasValue());
        world.Runtime().social.moneyField = 1;
        Input().dialogEdit.text.SetText("7");
        Input().dialogEdit.base.state = Client::Ui::ShownWindow;
        const auto before = random.State();
        ASSERT_TRUE(Type(typed).HasValue());
        EXPECT_EQ(world.Runtime().social.typedMoney, money) << typed;
        EXPECT_EQ(Input().dialogEdit.text.Value(), "") << typed;
        EXPECT_EQ(Input().dialogEdit.base.state, Client::Ui::HiddenWindow) << typed;
        EXPECT_FALSE(world.Runtime().ui.chatOpen) << typed;
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(random.State(), before);
    }
}

// 0x4b55b3..0x4b560f: with 0x78aba60 at 1, what was typed is the personal shop's title and is not said: if 0x5ebbd0
// allows it, it goes to 0x78aba64; if not, the notice 1130 of type 1 without a name.
TEST_P(GameChatOnTheMaps, TheShopTitleModeKeepsOrRefusesTheTitle)
{
    auto& shop = world.Runtime().social.personalShop;
    ASSERT_TRUE(Type("\r").HasValue());
    shop.dialogMode = 1;
    ASSERT_TRUE(Type("Tienda inventada\r").HasValue());
    EXPECT_EQ(shop.ownTitle, "Tienda inventada");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());

    // Two blanks in a row at the end: 0x5ebbd0 does not allow it and the title stays.
    ASSERT_TRUE(Type("\rOtra  \r").HasValue());
    EXPECT_EQ(shop.ownTitle, "Tienda inventada");
    const auto& entries = world.Runtime().chat.log.entries;
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::Ui::ChatEntry& entry)
                            { return entry.text == "aviso titulo" && entry.type == Client::Ui::SystemMessage; }));
    EXPECT_TRUE(sent.empty());
}

// 0x56d776: "/no ver puestos" turns off the map's signs (+0x14 of 0x7245c8) with the notice 1139 of type 2, and then
// goes out as chat; "/ver puestos" turns them on with the 1138 of type 1.
TEST_P(GameChatOnTheMaps, TheSignCommandsReachTheShopSigns)
{
    auto& shops = world.Runtime().social.personalShop;
    const auto& entries = world.Runtime().chat.log.entries;
    ASSERT_TRUE(Type("\r/no ver puestos\r").HasValue());
    EXPECT_EQ(shops.signsShown, 0);
    ASSERT_FALSE(entries.empty());
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::Ui::ChatEntry& entry)
                            { return entry.text == "aviso sin carteles" && entry.type == Client::Ui::ErrorMessage; }));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "/no ver puestos");

    Input().brake = 0;
    ASSERT_TRUE(Type("\r/ver puestos\r").HasValue());
    EXPECT_EQ(shops.signsShown, 1);
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::Ui::ChatEntry& entry)
                            { return entry.text == "aviso carteles" && entry.type == Client::Ui::SystemMessage; }));
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameChatOnTheMaps, ::testing::Values(Lorencia, Devias));

// The chat: the C1 00 with the hero's name, the brake, the history, the old area at the end and the field closed. It
// draws no rand().
TEST_F(GameChat, EnterSaysWhatWasTyped)
{
    world.Runtime().chat.area.firstShown = 0;
    ASSERT_TRUE(Type("\r").HasValue());
    const auto before = random.State();
    const auto said = Type("hox\bla\r");
    ASSERT_TRUE(said.HasValue()) << said.Error();

    ASSERT_EQ(sent.size(), 1u);
    const Said chat = Decode(sent[0]);
    EXPECT_EQ(chat.opcode, 0x00);
    EXPECT_EQ(chat.name, "Heroe");
    EXPECT_EQ(chat.text, "hola");
    EXPECT_EQ(random.State(), before);
    EXPECT_EQ(Input().brake, 0x46);
    EXPECT_EQ(Input().history.said.front(), "hola");
    EXPECT_EQ(world.Runtime().chat.area.firstShown, Client::Ui::FirstShownRow);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_FALSE(Input().shown);
    EXPECT_EQ(Input().message.text.Value(), "");
    EXPECT_EQ(Input().messageLength, 0);
}

TEST_F(GameChat, MessageAndWhisperFieldsUseXpWindows1252BytesOnTheWire)
{
    const std::string messageBytes("\x80???e\xb4", 6);
    const std::string whisperBytes("\x80"
                                   "abc",
                                   4);
    ASSERT_TRUE(Type("\r").HasValue());

    ASSERT_TRUE(Type("€漢😊é\t€abc").HasValue());
    EXPECT_EQ(Input().message.text.Value().size(), messageBytes.size());
    EXPECT_TRUE(Input().message.text.Value() == messageBytes);
    EXPECT_EQ(Input().whisper.text.Value().size(), whisperBytes.size());
    EXPECT_TRUE(Input().whisper.text.Value() == whisperBytes);

    ASSERT_TRUE(Type("\r").HasValue());

    ASSERT_EQ(sent.size(), 1u);
    const Said whisper = Decode(sent.front());
    EXPECT_EQ(whisper.opcode, 0x02);
    EXPECT_TRUE(whisper.name == whisperBytes);
    EXPECT_TRUE(whisper.text == messageBytes);
}

// 0x4691e0 → 0x467fe0 → 0x467b10: the message and the recipient use the same EDIT; the navigation keys go to the
// control's previous procedure, not to the game's WndProc, which only understands Tab/Enter/Backspace.
TEST_F(GameChat, OrderedInputEventsMoveAndEditBothChatCarets)
{
    ASSERT_TRUE(Type("\r").HasValue());
    for (const auto field : {Client::Ui::MessageField, Client::Ui::WhisperField})
    {
        Client::Ui::FocusTheChatField(Input(), field);
        const std::array events{
            Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "abcd"},
            Client::Input::TextInputEvent{
                .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Home, .text = {}},
            Client::Input::TextInputEvent{
                .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Right, .text = {}},
            Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "x"},
        };

        ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), events).HasValue());

        const auto* edit = Client::Ui::ChatEditWithFocus(Input());
        ASSERT_NE(edit, nullptr);
        EXPECT_EQ(edit->text.Value(), "axbcd");
        EXPECT_EQ(edit->text.Cursor(), 2u);
    }
    EXPECT_TRUE(sent.empty());
}

TEST_F(GameChat, OrderedChatClipboardAndDeleteUseTheSharedXpEdit)
{
    ASSERT_TRUE(Type("\r").HasValue());
    Input().message.text.SetText(std::string("a\xe9z", 3));
    const std::array select{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Home, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Right, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Right,
                                      .text = {},
                                      .shift = true},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), select).HasValue());
    EXPECT_EQ(clipboard.contents, std::optional<std::string>("é"));
    EXPECT_EQ(Input().message.text.Value(), "az");
    EXPECT_EQ(clipboard.writes, 2);

    clipboard.contents = "€";
    const std::array paste{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent.front()).opcode, 0x00);
    EXPECT_EQ(Decode(sent.front()).text, std::string("a\x80", 2));
    EXPECT_EQ(clipboard.reads, 1);
}

TEST_F(GameChat, TabKeepsBothChatTextsAndSelectsTheNewFieldBeforeWhispering)
{
    ASSERT_TRUE(Type("\rmensaje\tDestino").HasValue());
    const std::array tab{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Tab, .text = {}}};
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), tab).HasValue());
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Message);
    EXPECT_EQ(Input().message.text.Selected(), "mensaje");
    EXPECT_EQ(Input().whisper.text.Value(), "Destino");

    const std::array edited{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "nuevo"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Tab, .text = {}},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), edited).HasValue());
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Whisper);
    EXPECT_EQ(Input().whisper.text.Selected(), "Destino");
    EXPECT_EQ(world.Runtime().ui.focusedList, Client::Ui::ChatWhisperEditId);
    EXPECT_EQ(Input().message.text.Value(), "nuevo");
    ASSERT_TRUE(Type("\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent.front()).opcode, 0x02);
    EXPECT_EQ(Decode(sent.front()).name, "Destino");
    EXPECT_EQ(Decode(sent.front()).text, "nuevo");
}

TEST_F(GameChat, ChatPreeditCannotSendOrChangeFocusBeforeCommittedText)
{
    ASSERT_TRUE(Type("\rab").HasValue());
    const std::array preedit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition,
                                      .text = "preview",
                                      .compositionStart = 1,
                                      .compositionLength = 2},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Tab, .text = {}},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), preedit).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Message);
    EXPECT_EQ(Input().message.text.Value(), "ab");
    EXPECT_EQ(Input().message.compositionText, "preview");
    EXPECT_EQ(Input().message.compositionStart, 1);
    EXPECT_EQ(Input().message.compositionLength, 2);

    const std::array commit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "é"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), commit).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent.front()).text, std::string("ab\xe9", 3));
    EXPECT_TRUE(Input().message.compositionText.empty());
}

TEST_F(GameChat, OrderedInputEventsEditTheFocusedOldWindowField)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "ac"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Left, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "b"},
    };

    const auto result = Client::Chat::TypeTheGameInput(Context(), events);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_TRUE(sent.empty());
}

TEST_F(GameChat, ShiftMovementSelectsTextThatDeleteRemoves)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcdef");
    const std::array events{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Home, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Right,
                                      .text = {},
                                      .shift = true},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Right,
                                      .text = {},
                                      .shift = true},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}},
    };

    const auto result = Client::Chat::TypeTheGameInput(Context(), events);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(edit.text.Value(), "cdef");
    EXPECT_EQ(edit.text.Cursor(), 0u);
    EXPECT_FALSE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditTypingUsesXpWindows1252Bytes)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "€Ａ漢"}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_EQ(edit.text.Size(), 3u);
    EXPECT_TRUE(edit.text.Value() == std::string("\x80"
                                                 "A?",
                                                 3));
}

TEST_F(GameChat, OldEditClipboardCopiesCutsAndPastesInXpEncoding)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText(std::string("a\xe9z", 3));
    edit.text.Select(1, 2);
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), copy).HasValue());
    EXPECT_TRUE(clipboard.contents == std::optional<std::string>("é"));
    EXPECT_TRUE(edit.text.HasSelection());

    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cut).HasValue());
    EXPECT_TRUE(edit.text.Value() == "az");
    EXPECT_TRUE(clipboard.contents == std::optional<std::string>("é"));

    clipboard.contents = "€";
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == std::string("a\x80"
                                                 "z",
                                                 3));
    EXPECT_EQ(clipboard.reads, 1);
    EXPECT_EQ(clipboard.writes, 2);
}

TEST_F(GameChat, OldEditClipboardErrorsKeepSelectionAndEmptyPasteReplacesIt)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.draws = 8;
    clipboard.readFails = true;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.draws, 0);

    clipboard.readFails = false;
    clipboard.contents = std::string{};
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == "ad");
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.reads, 2);
}

TEST_F(GameChat, OldEditCutKeepsTheSelectionWhenClipboardWriteFails)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.draws = 8;
    clipboard.writeFails = true;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 1);
    EXPECT_EQ(edit.draws, 0);
}

TEST_F(GameChat, OldEditCtrlBackspaceInsertsTheNativeDeleteByte)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    const std::array events{Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                                          .key = Client::Input::Key::Backspace,
                                                          .text = {},
                                                          .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_EQ(edit.text.Size(), 3u);
    EXPECT_TRUE(edit.text.Value() == std::string("ab\x7f", 3));
}

TEST_F(GameChat, OldEditCtrlAIsTheNativeNoOp)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array selectAll{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::A, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), selectAll).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.text.SelectionBegin(), 1u);
    EXPECT_EQ(edit.text.SelectionEnd(), 3u);
    EXPECT_EQ(edit.draws, 0);
}

TEST_F(GameChat, OldEditCtrlCWithoutSelectionStillResetsTheCaretBlink)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.draws = 8;
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), copy).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlXWithoutSelectionStillResetsTheCaretBlink)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.draws = 8;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlVReadFailureStillResetsTheCaretBlink)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.draws = 8;
    clipboard.readFails = true;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditCtrlCCancelsNothingAndResetsBlinkDuringComposition)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), copy).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlXPreservesSelectionAndResetsBlinkDuringComposition)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlVDoesNotReadAndResetsBlinkDuringComposition)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), paste).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.reads, 0);
}

TEST_F(GameChat, OldEditCtrlBackspaceDuringCompositionStillResetsTheCaretBlink)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.SetCursor(2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array backspace{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Backspace, .text = {}}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), backspace).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_EQ(edit.compositionText, "preedit");
}

TEST_F(GameChat, LegacyBackspaceDuringCompositionStillResetsTheCaretBlink)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.SetCursor(2);
    edit.compositionText = "preedit";
    edit.draws = 8;

    ASSERT_TRUE(Type("\b").HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_EQ(edit.compositionText, "preedit");
}

TEST_F(GameChat, FocusLossCancellationAllowsBackspaceAndEnterAfterRefocus)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    edit.compositionText = "preedit";
    const std::array cancel{Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition,
                                                          .text = {},
                                                          .compositionStart = 0,
                                                          .compositionLength = 0}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cancel).HasValue());
    ASSERT_TRUE(edit.compositionText.empty());
    const std::array keys{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Backspace, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Value(), "a");
    const auto& parentQueue = world.Runtime().oldWindows.windows.at(10).base.queue;
    ASSERT_EQ(parentQueue.size(), 1u);
    EXPECT_EQ(parentQueue.front().type, Client::Ui::EnterMessage);
}

TEST_F(GameChat, CtrlWordMovementUsesTheXpByteBoundaries)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    const std::array keys{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::End, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Left,
                                      .text = {},
                                      .ctrl = true},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Right,
                                      .text = {},
                                      .ctrl = true},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Left,
                                      .text = {},
                                      .ctrl = true},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Cursor(), 6u);
}

TEST_F(GameChat, CtrlDeleteRemovesOnlyToTheEndOfTheXpLine)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    edit.text.SetCursor(3);
    const std::array erase{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), erase).HasValue());

    EXPECT_EQ(edit.text.Value(), "ab ");
    EXPECT_EQ(edit.text.Cursor(), 3u);
}

TEST_F(GameChat, ShiftCtrlMovementKeepsTheActiveWordCaretAndAnchor)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    const std::array keys{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::End, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Left,
                                      .text = {},
                                      .shift = true,
                                      .ctrl = true},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                      .key = Client::Input::Key::Left,
                                      .text = {},
                                      .shift = true,
                                      .ctrl = true},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Cursor(), 3u);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.text.SelectionBegin(), 3u);
    EXPECT_EQ(edit.text.SelectionEnd(), 8u);
}

TEST_F(GameChat, CtrlDeleteWithSelectionDeletesOnlyTheSelection)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    edit.text.Select(1, 3);
    const std::array erase{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), erase).HasValue());

    EXPECT_EQ(edit.text.Value(), "acd ef");
    EXPECT_EQ(edit.text.Cursor(), 1u);
    EXPECT_FALSE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditUpDownTabAndEnterResetBlinkWithoutChangingTextDuringComposition)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.SetCursor(1);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array events{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Up, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Down, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Tab, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abc");
    EXPECT_EQ(edit.text.Cursor(), 1u);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_TRUE(world.Runtime().oldWindows.queue.empty());
    EXPECT_TRUE(sent.empty());
}

TEST_F(GameChat, CompositionIsSeparateAndTextInputCommitsItOnce)
{
    Client::Ui::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    edit.text.SetCursor(1);
    const std::array preedit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition,
                                      .text = "preview",
                                      .compositionStart = 2,
                                      .compositionLength = 3},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), preedit).HasValue());

    EXPECT_EQ(edit.text.Value(), "ab");
    EXPECT_EQ(edit.compositionText, "preview");
    EXPECT_EQ(edit.compositionStart, 2);
    EXPECT_EQ(edit.compositionLength, 3);

    const std::array cancel{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition, .text = {}},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), cancel).HasValue());
    EXPECT_TRUE(edit.compositionText.empty());
    EXPECT_EQ(edit.text.Value(), "ab");

    const std::array commit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition, .text = "preview"},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "é"},
    };
    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), commit).HasValue());
    EXPECT_EQ(edit.text.Value(), std::string("a\xe9"
                                             "b",
                                             3));
    EXPECT_TRUE(edit.compositionText.empty());
}

TEST_F(GameChat, OrderedInputEventsSendTheGameChatOnce)
{
    ASSERT_TRUE(Type("\r").HasValue());
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "hola"},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "\r"},
    };

    const auto result = Client::Chat::TypeTheGameInput(Context(), events);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent.front()).text, "hola");
}

TEST_F(GameChat, OrderedEnterKeyUsesTheLegacyGameChatExactlyOnce)
{
    ASSERT_TRUE(Type("\r").HasValue());
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "hola"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };

    ASSERT_TRUE(Client::Chat::TypeTheGameInput(Context(), events).HasValue());

    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Decode(sent.front()).text == "hola");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
}

// 0x4b56cc and 0x576eb0: an emote in what was said, with the hero standing, animates it and sends two equal C1 05 18
// before the C1 00 (Q-C776), with its angle's direction. It draws no rand().
TEST_F(GameChat, AnEmoteSendsItsActionsBeforeTheChat)
{
    auto& hero = world.Hero();
    hero.animation.action = 1;
    hero.angles.z = 100.0f;
    ASSERT_TRUE(Type("\r").HasValue());
    const auto before = random.State();
    const auto said = Type("hola ^^\r");
    ASSERT_TRUE(said.HasValue()) << said.Error();

    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(IsTheAction(sent[0], 3, 0x8e));
    EXPECT_TRUE(IsTheAction(sent[1], 3, 0x8e));
    EXPECT_EQ(Decode(sent[2]).opcode, 0x00);
    EXPECT_EQ(Decode(sent[2]).text, "hola ^^");
    EXPECT_EQ(hero.animation.action, 0xaf);
    EXPECT_EQ(random.State(), before);
}

// The transformed one's model is really loaded in fauna; the base provider only has Player.
// 0x576abf→0x4ea030 must query that model even before the first draw binds WorldModels.
TEST_F(GameChat, ATransformedHeroUsesItsLoadedFaunaModelBeforeSendingTheChat)
{
    Client::Tests::TemporaryFolder folder{"emote-fauna"};
    Client::Data::Models::Model monster = Client::Tests::SampleModel();
    const auto action = monster.actions.front();
    monster.actions.assign(8, action);
    for (auto& bone : monster.bones)
        if (!bone.dummy)
        {
            const auto keys = bone.actions.front();
            bone.actions.assign(8, keys);
        }
    monster.meshes.front().textureName = "emote.tga";
    Client::Tests::WriteSceneFile(folder.Path(), "Monster/Monster10.bmd",
                                  Client::Tests::PlainBmd(Client::Tests::BmdContent(monster), 0x0a));
    Client::Tests::WriteSceneFile(folder.Path(), "Monster/emote.OZT", Client::Tests::PlainOzt(2, 2));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Models::ModelTextureSlots slots{{0x41c}};
    Client::Assets::FaunaModelLoadContext load{folder.Path(), m_faunaStore, log, slots};
    const auto loaded = faunaModels.LoadMonster(9, 0, load);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    auto& hero = world.Hero();
    hero.type = 0x15b;
    hero.animation.action = 1;
    hero.angles.z = 100.0f;
    ASSERT_EQ(models.SkeletonOf(Client::Models::ModelId{hero.type}), nullptr);
    ASSERT_NE(faunaModels.SkeletonOf(Client::Models::ModelId{hero.type}), nullptr);
    const auto before = random.State();
    const auto said = Type("\rhola ^^\r");
    ASSERT_TRUE(said.HasValue()) << said.Error();
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(IsTheAction(sent[0], 3, 0x8e));
    EXPECT_TRUE(IsTheAction(sent[1], 3, 0x8e));
    EXPECT_EQ(Decode(sent[2]).text, "hola ^^");
    EXPECT_EQ(hero.animation.action, 1); // The eight-action model does not contain 0xaf; 0x4ea030 does not change it.
    EXPECT_EQ(random.State(), before);
}

// Walking, only the case's C1 18 and the hero is not animated; mounted outside the safe zone (the CL8 gate), none.
TEST_F(GameChat, AWalkingHeroSendsOnlyTheCaseAndAMountedOneNone)
{
    auto& hero = world.Hero();
    hero.animation.action = 0x0f;
    ASSERT_TRUE(Type("\rbueno;\r").HasValue());
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(IsTheAction(sent[0], 1, 0x8b));
    EXPECT_EQ(Decode(sent[1]).opcode, 0x00);
    EXPECT_EQ(hero.animation.action, 0x0f);

    sent.clear();
    Input().brake = 0;
    hero.pet.model = 0x1c15;
    hero.safeZone = 0;
    hero.animation.action = 1;
    ASSERT_TRUE(Type("\rotra ^^\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).opcode, 0x00);
    EXPECT_EQ(hero.animation.action, 1);
}

// 0x5767940: with the brake above 0x32 nothing goes out; with less, the same as before does not go out.
TEST_F(GameChat, TheBrakeStopsTheChat)
{
    ASSERT_TRUE(Type("\runo\r").HasValue());
    ASSERT_TRUE(Type("\rdos\r").HasValue());
    EXPECT_EQ(sent.size(), 1u);
    Input().brake = 0x32;
    ASSERT_TRUE(Type("\runo\r").HasValue());
    EXPECT_EQ(sent.size(), 1u);
    ASSERT_TRUE(Type("\rdos\r").HasValue());
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(Decode(sent[1]).text, "dos");
    Input().brake = 0;
    ASSERT_TRUE(Type("\rdos\r").HasValue());
    EXPECT_EQ(sent.size(), 3u);
}

// The whisper: with the name of the field next to it and the F3 mode, 0x554a40 opens and closes the own record (3848
// rand()), and with the level it sends the C1 02 and puts it in the log with the hero's name and type 0.
TEST_F(GameChat, TheWhisperGoesToTheNameOfTheSecondField)
{
    ASSERT_TRUE(Type("\rpsst\tAlguien").HasValue());
    const CrtRandom expected = Advanced(random, WhisperRandoms);
    const auto said = Type("\r");
    ASSERT_TRUE(said.HasValue()) << said.Error();

    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    const Said whisper = Decode(sent[0]);
    EXPECT_EQ(whisper.opcode, 0x02);
    EXPECT_EQ(whisper.name, "Alguien");
    EXPECT_EQ(whisper.text, "psst");
    EXPECT_EQ(world.Runtime().chat.lastWhisperTarget, "Alguien");
    const auto& entries = world.Runtime().chat.log.entries;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.front().name, "Heroe");
    EXPECT_EQ(entries.front().text, "psst");
    EXPECT_EQ(entries.front().type, Client::Ui::WhisperMessage);
    EXPECT_EQ(Input().brake, 0);
}

// Below level 6 and without the name in the list of 0x77997f8: the notice 479, without a packet.
TEST_F(GameChat, ALowLevelCannotWhisper)
{
    world.OwnCharacter().level = 5;
    ASSERT_TRUE(Type("\rpsst\tAlguien").HasValue());
    const CrtRandom expected = Advanced(random, WhisperRandoms);
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(sent.empty());
    ASSERT_FALSE(world.Runtime().chat.log.entries.empty());
    EXPECT_EQ(world.Runtime().chat.log.entries.front().text, "aviso nivel");
    EXPECT_EQ(world.Runtime().chat.log.entries.front().type, Client::Ui::SystemMessage);
}

// With the name among those who whispered (0x77997f8, which the incoming 02 fills), the low level does not stop it.
TEST_F(GameChat, ALowLevelMayAnswerWhoWhispered)
{
    world.OwnCharacter().level = 5;
    Client::Chat::RememberTheWhisperSender(world.Runtime().chat.whisperSenders, 5, "Alguien");
    ASSERT_TRUE(Type("\rpsst\tAlguien\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).opcode, 0x02);
}

// Without the F3 mode, or with '/' in front, it goes to the chat even if there is a name.
TEST_F(GameChat, WithoutTheWhisperModeItGoesToTheChat)
{
    Input().whisperMode = false;
    ASSERT_TRUE(Type("\rpsst\tAlguien\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).opcode, 0x00);
}

// 0x4b5a4f..0x4b5b00: /whisper off and on (the texts 264 and 265) change 0x77f3b98 and notify, without a packet.
TEST_F(GameChat, TheWhisperSwitchesWarnWithoutSending)
{
    ASSERT_TRUE(Type("\r/callar\r").HasValue());
    EXPECT_TRUE(world.Runtime().chat.whisperBlocked);
    EXPECT_EQ(world.Runtime().chat.log.entries.front().text, "aviso callar");
    Input().brake = 0;
    ASSERT_TRUE(Type("\r/escuchar\r").HasValue());
    EXPECT_FALSE(world.Runtime().chat.whisperBlocked);
    EXPECT_EQ(world.Runtime().chat.log.entries.front().text, "aviso escuchar");
    EXPECT_TRUE(sent.empty());
}

// Dying, /move does not go out; the rest does.
TEST_F(GameChat, ADyingHeroDoesNotSayMove)
{
    world.Hero().dying = 1;
    ASSERT_TRUE(Type("\r/mover\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().panels.overlayMode, Client::Ui::MoveOverlayMode);
}

// 0x622b40 with 1: what has a Filter.bmd word goes out as the text 570.
TEST_F(GameChat, AFilteredWordSaysTheDots)
{
    filter = {"feo"};
    ASSERT_TRUE(Type("\rmuy f eo\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "...");
}

// With "webzen" in the hero's name only what starts with '/' or with '!' goes out.
TEST_F(GameChat, AWebzenNameOnlySaysCommands)
{
    SetTheHeroName("webzenA");
    ASSERT_TRUE(Type("\rhola\r").HasValue());
    EXPECT_TRUE(sent.empty());
    Input().brake = 0;
    ASSERT_TRUE(Type("\r!hola\r").HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// 0x4b5269: in Chaos Castle, outside the safe zone, Enter does nothing; nor with the dialog 0x7d.
TEST_F(GameChat, ChaosCastleAndSomeDialogsBlockEnter)
{
    world.Runtime().map = 18;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    world.Runtime().map = 0;
    world.Runtime().dialogs.current = 0x7d;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
}

// CL9 milestone 11: with the focus on the EDIT of [0x5763504] (the serial number input), the subclass 0x467b10
// filters what is typed with +0x4c at 2 (digits and letters, lowercase to uppercase), the erasing is the EDIT's and
// the Tab and the Enter go to the game window: with 0x78abb00 at 3 the Enter only sets 0x5763588 (0x4b52f6). Nothing
// reaches the chat fields.
TEST_F(GameChat, TheSerialEditFiltersWhatIsTyped)
{
    world.Runtime().panels.field78abb00 = 3;
    world.Runtime().events.serialEntry = true;
    Input().focus = Client::Ui::ChatFocus::Dialog;
    Input().dialogEdit.filter = Client::Ui::UppercaseLettersAndDigitsEdit;
    Input().dialogEdit.text.SetLimit(12);
    ASSERT_TRUE(Type("ab-1 c\bz9\r\t").HasValue());
    EXPECT_EQ(Input().dialogEdit.text.Value(), "AB1Z9");
    EXPECT_EQ(Input().message.text.Value(), "");
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::Dialog);
    EXPECT_TRUE(world.Runtime().ui.enterPressed);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// 0x4b540a: with the field closed, Enter reads the text of the EDIT of [0x5763504] and, without the zen mode or the
// title one, says it; the jump to 0x4b8886 does not empty the EDIT, and it goes out again with the next Enter.
TEST_F(GameChat, WithTheFieldClosedEnterSaysTheSerialEdit)
{
    Input().dialogEdit.text.SetText("AB12");
    ASSERT_TRUE(Type("\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "AB12");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "AB12");
}

// 0x4b5616 and 0x4b8888: with 0x77f3b6b set and 0x78abb00 at another value (a 94 of another type over the input,
// Q-C1346), Enter sets the chosen field to 0 and nothing more: it does not say, does not empty the buffer and does not
// close the field.
TEST_F(GameChat, AStuckSerialEntrySwallowsEnter)
{
    world.Runtime().panels.field78abb00 = 1;
    world.Runtime().events.serialEntry = true;
    ASSERT_TRUE(Type("\rhola").HasValue());
    ASSERT_TRUE(world.Runtime().ui.chatOpen);
    Input().field = 1;
    Input().focus = Client::Ui::ChatFocus::Message;
    const auto before = random.State();
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().field, Client::Ui::MessageField);
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(std::string_view(Input().messageBuffer.data()), "hola");
    EXPECT_EQ(Input().message.text.Value(), "hola");
    EXPECT_EQ(random.State(), before);
}

// A command that is handled (/1) sends nothing and does not bring back the old area, but closes the field.
TEST_F(GameChat, AHandledCommandOnlyClosesTheField)
{
    world.Runtime().chat.area.firstShown = 0;
    ASSERT_TRUE(Type("\r/1 hola\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().macros[0], "hola");
    EXPECT_EQ(world.Runtime().chat.area.firstShown, 0);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(world.Runtime().lastSoundId, Client::Ui::ChatButtonSound);
}

// 0x56c68b..0x56cdfc from the chat: /trade with the selected one next to it sends the C1 05 36 and the notice 475
// goes out built with its name (0x69fd9b) in the log.
TEST_F(GameChat, TradeWithTheChosenSaysWhoWasAsked)
{
    auto& runtime = world.Runtime();
    runtime.drawingEnabled = true;
    auto& other = world.Actors()[runtime.hero + 1];
    other.alive = true;
    other.byte88 = 1;
    other.type = 0x20a;
    other.key = 0x0456;
    for (std::size_t i = 0; i < 6; ++i)
        other.name[i] = static_cast<std::byte>("Brezal"[i]);
    other.cellX = 1;
    runtime.selectedTargets[2] = static_cast<std::int32_t>(runtime.hero + 1);
    ASSERT_TRUE(Type("\r/comerciar\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Client::Network::Xor32Decode(sent[0].packet).Bytes()[2], std::byte{0x36});
    EXPECT_EQ(runtime.chat.log.entries.front().text, "pediste comerciar con Brezal");
}

// 0x5a119f..0x5a2b47: each pressed macro goes out as chat and leaves the macros' brake at 0x64, even if the chat
// brake stops it.
TEST_F(GameChat, TheMacrosSayTheirTextAndLock)
{
    Input().macros[0] = "uno";
    Input().macros[9] = "diez";
    std::array<bool, Client::Tables::MacroCount> pressed{};
    pressed[0] = true;
    pressed[9] = true;
    const auto before = random.State();
    const auto said = Client::Chat::SayTheMacros(Context(), pressed);
    ASSERT_TRUE(said.HasValue()) << said.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "uno");
    EXPECT_EQ(world.Runtime().hud.macroTime, Client::Ui::MacroLockSteps);
    EXPECT_EQ(random.State(), before);
}

// From a macro (0x56c440 with 1), "/1 …" stores nothing: it goes out as chat and brakes.
TEST_F(GameChat, FromAMacroTheNumbersAreNotCommands)
{
    Input().macros[0] = "/1x";
    std::array<bool, Client::Tables::MacroCount> pressed{};
    pressed[0] = true;
    ASSERT_TRUE(Client::Chat::SayTheMacros(Context(), pressed).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "/1x");
    EXPECT_EQ(Input().macros[0], "/1x");
    EXPECT_EQ(world.Runtime().hud.macroTime, Client::Ui::MacroLockSteps);
}
} // namespace
