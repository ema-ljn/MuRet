#include "Network/Xor32Packet.h"
#include "Scenes/GameChat.h"

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
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "Platform/IClipboard.h"
#include "UI/MessageArea.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "UI/WhisperSenders.h"

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

// La entrada del chat en el juego (CL8 hito 3; 06 § El chat): Enter, lo tecleado, el C1 00 y el C1 02, y las macros.
// Los textos y los nombres son inventados.
namespace
{
using Client::Crt::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::GameChatContext;
using Client::Scenes::WorldState;

// 0x554a40: el registro propio, abierto y cerrado una vez.
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

// Un paquete en claro: el tipo, el largo, el código, el nombre de diez bytes hasta su '\0' y el texto hasta el suyo.
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
                               .models = &models};
    }

    Client::Core::Expected<void, std::string> Type(std::string_view typed)
    {
        return Client::Scenes::TypeInTheGameChat(Context(), typed);
    }

    Client::UI::ChatInputState& Input()
    {
        return world.Runtime().chat.input;
    }

    Client::UI::OldEdit& FocusAnOldEdit()
    {
        auto& manager = world.Runtime().oldWindows;
        Client::UI::OldWindow box;
        box.kind = Client::UI::OldWindowKind::AddFriendBox;
        box.base.id = 10;
        box.add.edit.base.id = 11;
        box.add.edit.base.parent = box.base.id;
        auto [window, inserted] = manager.windows.emplace(box.base.id, std::move(box));
        EXPECT_TRUE(inserted);
        manager.focusedEdit = window->second.add.edit.base.id;
        Input().focus = Client::UI::ChatFocus::OldWindow;
        return window->second.add.edit;
    }

    // Fauna conserva el backend hasta destruirse: debe vivir más que WorldState, incluso si falla un ASSERT.
    Client::Tests::FakeTextureStore m_faunaStore;
    WorldState world;
    CrtRandom random;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::vector<std::string> filter;
    LetterMeasure letters;
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Scenes::SingleModelLibrary models;
    FakeClipboard clipboard;
};

// Un C1 05 18 [direction] [action] en claro, con el XOR32.
bool IsTheAction(const Client::Protocol::Outgoing& sent, std::uint8_t direction, std::uint8_t action)
{
    return Client::Tests::SameBytes(Client::Network::Xor32Decode(sent.packet).Bytes(),
                                    Client::Tests::Bytes({0xc1, 5, 0x18, direction, action}));
}

// 0x4b5468..0x4b5554: con el campo vacío, Enter lo abre (los EDIT a la vista y el foco en el del mensaje) o lo cierra.
TEST_F(GameChat, EnterOpensAndClosesTheEmptyField)
{
    const auto before = random.State();
    ASSERT_TRUE(Type("abc\r").HasValue());
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(Input().shown);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Message);
    EXPECT_EQ(world.Runtime().ui.focusedList, Client::UI::ChatMessageEditId);
    // Lo de antes de abrir lo descartó la ventana del juego.
    EXPECT_EQ(Input().message.text.Value(), "");

    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_FALSE(Input().shown);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Game);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), before);
}

// Los casos de Enter con los diálogos del zen y de la tienda personal, en Lorencia y en Devias.
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// 0x4b54e1: el diálogo del zen del comercio y del almacén.
constexpr std::int32_t MoneyDialog = 0x73;

class GameChatOnTheMaps : public GameChat, public ::testing::WithParamInterface<std::int32_t>
{
protected:
    GameChatOnTheMaps()
    {
        world.Runtime().map = GetParam();
    }
};

// 0x4b5515..0x4b554d: abrir el chat deja el largo del campo en 0x2a y apaga el modo del zen (0x77f3b6a).
TEST_P(GameChatOnTheMaps, OpeningTheChatLeavesTheMoneyMode)
{
    world.Runtime().social.moneyField = 1;
    world.Runtime().social.dialogFieldLimit = 8;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(world.Runtime().social.moneyField, 0);
    EXPECT_EQ(world.Runtime().social.dialogFieldLimit, 0x2a);
}

// 0x4b54e1..0x4b5513: con el campo cerrado, el EDIT de [0x5763504] vacío (lo que se lee) y el diálogo 0x73, Enter no
// abre el chat: le pone el texto en 0 al EDIT y el foco en la ventana del juego (el dibujo del 0x73 se lo devuelve,
// 0x6308b8).
TEST_P(GameChatOnTheMaps, TheMoneyDialogEmptiesItsEdit)
{
    world.Runtime().dialogs.current = MoneyDialog;
    Input().focus = Client::UI::ChatFocus::Dialog;
    const auto before = random.State();
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "");
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Game);
    EXPECT_EQ(world.Runtime().dialogs.current, MoneyDialog);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), before);
}

// 0x4b5397..0x4b5435: con el campo cerrado, lo que se lee es el EDIT del diálogo; con el modo del zen va con atoi.
TEST_P(GameChatOnTheMaps, TheDialogEditIsReadWithTheFieldClosed)
{
    world.Runtime().dialogs.current = MoneyDialog;
    world.Runtime().social.moneyField = 1;
    Input().dialogEdit.text.SetText("250");
    Input().dialogEdit.base.state = Client::UI::ShownWindow;
    Input().focus = Client::UI::ChatFocus::Dialog;
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_EQ(world.Runtime().social.typedMoney, 250);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "");
    EXPECT_EQ(Input().dialogEdit.base.state, Client::UI::HiddenWindow);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Game);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// 0x4b5563..0x4b55ae: con el modo del zen, lo escrito va con atoi a 0x77f3b6c (los blancos, el signo y las cifras hasta
// la primera que no lo es) y no se dice; después, el EDIT de [0x5763504] vacío y oculto, y el salto a 0x4b8886, que
// cierra el campo.
TEST_P(GameChatOnTheMaps, TheTypedMoneyGoesThroughAtoiAndIsNotSaid)
{
    for (const auto& [typed, money] : {std::pair<std::string_view, std::int32_t>{" 1234\r", 1234},
                                       std::pair<std::string_view, std::int32_t>{"-12zen\r", -12}})
    {
        Client::UI::ClearTheChatFields(Input());
        Client::UI::ResetTheTextBuffers(Input(), false);
        world.Runtime().ui.chatOpen = false;
        ASSERT_TRUE(Type("\r").HasValue());
        world.Runtime().social.moneyField = 1;
        Input().dialogEdit.text.SetText("7");
        Input().dialogEdit.base.state = Client::UI::ShownWindow;
        const auto before = random.State();
        ASSERT_TRUE(Type(typed).HasValue());
        EXPECT_EQ(world.Runtime().social.typedMoney, money) << typed;
        EXPECT_EQ(Input().dialogEdit.text.Value(), "") << typed;
        EXPECT_EQ(Input().dialogEdit.base.state, Client::UI::HiddenWindow) << typed;
        EXPECT_FALSE(world.Runtime().ui.chatOpen) << typed;
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(random.State(), before);
    }
}

// 0x4b55b3..0x4b560f: con 0x78aba60 en 1, lo escrito es el título de la tienda personal y no se dice: si 0x5ebbd0 lo
// deja va a 0x78aba64; si no, el aviso 1130 del tipo 1 sin nombre.
TEST_P(GameChatOnTheMaps, TheShopTitleModeKeepsOrRefusesTheTitle)
{
    auto& shop = world.Runtime().social.personalShop;
    ASSERT_TRUE(Type("\r").HasValue());
    shop.dialogMode = 1;
    ASSERT_TRUE(Type("Tienda inventada\r").HasValue());
    EXPECT_EQ(shop.ownTitle, "Tienda inventada");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());

    // Dos blancos seguidos al final: 0x5ebbd0 no lo deja y el título queda.
    ASSERT_TRUE(Type("\rOtra  \r").HasValue());
    EXPECT_EQ(shop.ownTitle, "Tienda inventada");
    const auto& entries = world.Runtime().chat.log.entries;
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::UI::ChatEntry& entry)
                            { return entry.text == "aviso titulo" && entry.type == Client::UI::SystemMessage; }));
    EXPECT_TRUE(sent.empty());
}

// 0x56d776: "/no ver puestos" apaga los carteles del mapa (+0x14 de 0x7245c8) con el aviso 1139 del tipo 2, y después
// sale como chat; "/ver puestos" los prende con el 1138 del tipo 1.
TEST_P(GameChatOnTheMaps, TheSignCommandsReachTheShopSigns)
{
    auto& shops = world.Runtime().social.personalShop;
    const auto& entries = world.Runtime().chat.log.entries;
    ASSERT_TRUE(Type("\r/no ver puestos\r").HasValue());
    EXPECT_EQ(shops.signsShown, 0);
    ASSERT_FALSE(entries.empty());
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::UI::ChatEntry& entry)
                            { return entry.text == "aviso sin carteles" && entry.type == Client::UI::ErrorMessage; }));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "/no ver puestos");

    Input().brake = 0;
    ASSERT_TRUE(Type("\r/ver puestos\r").HasValue());
    EXPECT_EQ(shops.signsShown, 1);
    EXPECT_TRUE(
        std::ranges::any_of(entries, [](const Client::UI::ChatEntry& entry)
                            { return entry.text == "aviso carteles" && entry.type == Client::UI::SystemMessage; }));
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameChatOnTheMaps, ::testing::Values(Lorencia, Devias));

// El chat: el C1 00 con el nombre del héroe, el freno, el historial, el área vieja al final y el campo cerrado. No
// saca ningún rand().
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
    EXPECT_EQ(world.Runtime().chat.area.firstShown, Client::UI::FirstShownRow);
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

// 0x4691e0 → 0x467fe0 → 0x467b10: mensaje y destinatario usan el mismo EDIT; las teclas de navegación
// pasan al procedimiento anterior del control, no al WndProc del juego que sólo entiende Tab/Enter/Backspace.
TEST_F(GameChat, OrderedInputEventsMoveAndEditBothChatCarets)
{
    ASSERT_TRUE(Type("\r").HasValue());
    for (const auto field : {Client::UI::MessageField, Client::UI::WhisperField})
    {
        Client::UI::FocusTheChatField(Input(), field);
        const std::array events{
            Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "abcd"},
            Client::Input::TextInputEvent{
                .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Home, .text = {}},
            Client::Input::TextInputEvent{
                .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Right, .text = {}},
            Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "x"},
        };

        ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), events).HasValue());

        const auto* edit = Client::UI::ChatEditWithFocus(Input());
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
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), select).HasValue());
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
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());
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
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), tab).HasValue());
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Message);
    EXPECT_EQ(Input().message.text.Selected(), "mensaje");
    EXPECT_EQ(Input().whisper.text.Value(), "Destino");

    const std::array edited{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "nuevo"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Tab, .text = {}},
    };
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), edited).HasValue());
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Whisper);
    EXPECT_EQ(Input().whisper.text.Selected(), "Destino");
    EXPECT_EQ(world.Runtime().ui.focusedList, Client::UI::ChatWhisperEditId);
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
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), preedit).HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Message);
    EXPECT_EQ(Input().message.text.Value(), "ab");
    EXPECT_EQ(Input().message.compositionText, "preview");
    EXPECT_EQ(Input().message.compositionStart, 1);
    EXPECT_EQ(Input().message.compositionLength, 2);

    const std::array commit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "é"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), commit).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent.front()).text, std::string("ab\xe9", 3));
    EXPECT_TRUE(Input().message.compositionText.empty());
}

TEST_F(GameChat, OrderedInputEventsEditTheFocusedOldWindowField)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "ac"},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Left, .text = {}},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "b"},
    };

    const auto result = Client::Scenes::TypeTheGameInput(Context(), events);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_TRUE(sent.empty());
}

TEST_F(GameChat, ShiftMovementSelectsTextThatDeleteRemoves)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
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

    const auto result = Client::Scenes::TypeTheGameInput(Context(), events);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(edit.text.Value(), "cdef");
    EXPECT_EQ(edit.text.Cursor(), 0u);
    EXPECT_FALSE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditTypingUsesXpWindows1252Bytes)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    const std::array events{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "€Ａ漢"}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_EQ(edit.text.Size(), 3u);
    EXPECT_TRUE(edit.text.Value() == std::string("\x80"
                                                 "A?",
                                                 3));
}

TEST_F(GameChat, OldEditClipboardCopiesCutsAndPastesInXpEncoding)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText(std::string("a\xe9z", 3));
    edit.text.Select(1, 2);
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), copy).HasValue());
    EXPECT_TRUE(clipboard.contents == std::optional<std::string>("é"));
    EXPECT_TRUE(edit.text.HasSelection());

    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cut).HasValue());
    EXPECT_TRUE(edit.text.Value() == "az");
    EXPECT_TRUE(clipboard.contents == std::optional<std::string>("é"));

    clipboard.contents = "€";
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == std::string("a\x80"
                                                 "z",
                                                 3));
    EXPECT_EQ(clipboard.reads, 1);
    EXPECT_EQ(clipboard.writes, 2);
}

TEST_F(GameChat, OldEditClipboardErrorsKeepSelectionAndEmptyPasteReplacesIt)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.draws = 8;
    clipboard.readFails = true;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.draws, 0);

    clipboard.readFails = false;
    clipboard.contents = std::string{};
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());
    EXPECT_TRUE(edit.text.Value() == "ad");
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.reads, 2);
}

TEST_F(GameChat, OldEditCutKeepsTheSelectionWhenClipboardWriteFails)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.draws = 8;
    clipboard.writeFails = true;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 1);
    EXPECT_EQ(edit.draws, 0);
}

TEST_F(GameChat, OldEditCtrlBackspaceInsertsTheNativeDeleteByte)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    const std::array events{Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Key,
                                                          .key = Client::Input::Key::Backspace,
                                                          .text = {},
                                                          .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_EQ(edit.text.Size(), 3u);
    EXPECT_TRUE(edit.text.Value() == std::string("ab\x7f", 3));
}

TEST_F(GameChat, OldEditCtrlAIsTheNativeNoOp)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array selectAll{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::A, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), selectAll).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abcd");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.text.SelectionBegin(), 1u);
    EXPECT_EQ(edit.text.SelectionEnd(), 3u);
    EXPECT_EQ(edit.draws, 0);
}

TEST_F(GameChat, OldEditCtrlCWithoutSelectionStillResetsTheCaretBlink)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.draws = 8;
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), copy).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_FALSE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlXWithoutSelectionStillResetsTheCaretBlink)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.draws = 8;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlVReadFailureStillResetsTheCaretBlink)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.draws = 8;
    clipboard.readFails = true;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditCtrlCCancelsNothingAndResetsBlinkDuringComposition)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array copy{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::C, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), copy).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlXPreservesSelectionAndResetsBlinkDuringComposition)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array cut{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::X, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cut).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.writes, 0);
}

TEST_F(GameChat, OldEditCtrlVDoesNotReadAndResetsBlinkDuringComposition)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.Select(1, 2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array paste{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::V, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), paste).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(clipboard.reads, 0);
}

TEST_F(GameChat, OldEditCtrlBackspaceDuringCompositionStillResetsTheCaretBlink)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("abc");
    edit.text.SetCursor(2);
    edit.compositionText = "preedit";
    edit.draws = 8;
    const std::array backspace{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Backspace, .text = {}}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), backspace).HasValue());

    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_EQ(edit.compositionText, "preedit");
}

TEST_F(GameChat, LegacyBackspaceDuringCompositionStillResetsTheCaretBlink)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
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
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    edit.compositionText = "preedit";
    const std::array cancel{Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition,
                                                          .text = {},
                                                          .compositionStart = 0,
                                                          .compositionLength = 0}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cancel).HasValue());
    ASSERT_TRUE(edit.compositionText.empty());
    const std::array keys{
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Backspace, .text = {}},
        Client::Input::TextInputEvent{
            .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Enter, .text = {}},
    };

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Value(), "a");
    const auto& parentQueue = world.Runtime().oldWindows.windows.at(10).base.queue;
    ASSERT_EQ(parentQueue.size(), 1u);
    EXPECT_EQ(parentQueue.front().type, Client::UI::EnterMessage);
}

TEST_F(GameChat, CtrlWordMovementUsesTheXpByteBoundaries)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
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

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Cursor(), 6u);
}

TEST_F(GameChat, CtrlDeleteRemovesOnlyToTheEndOfTheXpLine)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    edit.text.SetCursor(3);
    const std::array erase{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), erase).HasValue());

    EXPECT_EQ(edit.text.Value(), "ab ");
    EXPECT_EQ(edit.text.Cursor(), 3u);
}

TEST_F(GameChat, ShiftCtrlMovementKeepsTheActiveWordCaretAndAnchor)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
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

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), keys).HasValue());

    EXPECT_EQ(edit.text.Cursor(), 3u);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(edit.text.SelectionBegin(), 3u);
    EXPECT_EQ(edit.text.SelectionEnd(), 8u);
}

TEST_F(GameChat, CtrlDeleteWithSelectionDeletesOnlyTheSelection)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab cd ef");
    edit.text.Select(1, 3);
    const std::array erase{Client::Input::TextInputEvent{
        .kind = Client::Input::TextInputEvent::Kind::Key, .key = Client::Input::Key::Delete, .text = {}, .ctrl = true}};

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), erase).HasValue());

    EXPECT_EQ(edit.text.Value(), "acd ef");
    EXPECT_EQ(edit.text.Cursor(), 1u);
    EXPECT_FALSE(edit.text.HasSelection());
}

TEST_F(GameChat, OldEditUpDownTabAndEnterResetBlinkWithoutChangingTextDuringComposition)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
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

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), events).HasValue());

    EXPECT_TRUE(edit.text.Value() == "abc");
    EXPECT_EQ(edit.text.Cursor(), 1u);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_TRUE(world.Runtime().oldWindows.queue.empty());
    EXPECT_TRUE(sent.empty());
}

TEST_F(GameChat, CompositionIsSeparateAndTextInputCommitsItOnce)
{
    Client::UI::OldEdit& edit = FocusAnOldEdit();
    edit.text.SetText("ab");
    edit.text.SetCursor(1);
    const std::array preedit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition,
                                      .text = "preview",
                                      .compositionStart = 2,
                                      .compositionLength = 3},
    };

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), preedit).HasValue());

    EXPECT_EQ(edit.text.Value(), "ab");
    EXPECT_EQ(edit.compositionText, "preview");
    EXPECT_EQ(edit.compositionStart, 2);
    EXPECT_EQ(edit.compositionLength, 3);

    const std::array cancel{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition, .text = {}},
    };
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), cancel).HasValue());
    EXPECT_TRUE(edit.compositionText.empty());
    EXPECT_EQ(edit.text.Value(), "ab");

    const std::array commit{
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Composition, .text = "preview"},
        Client::Input::TextInputEvent{.kind = Client::Input::TextInputEvent::Kind::Text, .text = "é"},
    };
    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), commit).HasValue());
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

    const auto result = Client::Scenes::TypeTheGameInput(Context(), events);

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

    ASSERT_TRUE(Client::Scenes::TypeTheGameInput(Context(), events).HasValue());

    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(Decode(sent.front()).text == "hola");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
}

// 0x4b56cc y 0x576eb0: un emote en lo dicho, con el héroe parado, lo anima y manda dos C1 05 18 iguales antes del
// C1 00 (Q-C776), con la dirección de su ángulo. No saca ningún rand().
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

// El modelo del transformado se carga realmente en fauna; el proveedor base sólo tiene Player.
// 0x576abf→0x4ea030 debe consultar ese modelo incluso antes de que el primer dibujo ligue WorldModels.
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
    Client::Scenes::FaunaModelLoadContext load{folder.Path(), m_faunaStore, log, slots};
    const auto loaded = world.FaunaResources().LoadMonster(9, 0, load);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    auto& hero = world.Hero();
    hero.type = 0x15b;
    hero.animation.action = 1;
    hero.angles.z = 100.0f;
    ASSERT_EQ(models.SkeletonOf(hero.type), nullptr);
    ASSERT_NE(world.FaunaResources().SkeletonOf(hero.type), nullptr);
    const auto before = random.State();
    const auto said = Type("\rhola ^^\r");
    ASSERT_TRUE(said.HasValue()) << said.Error();
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(IsTheAction(sent[0], 3, 0x8e));
    EXPECT_TRUE(IsTheAction(sent[1], 3, 0x8e));
    EXPECT_EQ(Decode(sent[2]).text, "hola ^^");
    EXPECT_EQ(hero.animation.action, 1); // El modelo de ocho acciones no contiene 0xaf; 0x4ea030 no la cambia.
    EXPECT_EQ(random.State(), before);
}

// Caminando, solo el C1 18 del caso y el héroe no se anima; montado fuera de la zona segura (la compuerta de la CL8),
// ninguno.
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

// 0x5767940: con el freno arriba de 0x32 no sale nada; con menos, no sale lo mismo que antes.
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

// El susurro: con el nombre del campo de al lado y el modo de F3, 0x554a40 abre y cierra el registro propio (3.848
// rand()), y con el nivel manda el C1 02 y lo pone en el registro con el nombre del héroe y el tipo 0.
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
    EXPECT_EQ(entries.front().type, Client::UI::WhisperMessage);
    // El susurro no frena el chat.
    EXPECT_EQ(Input().brake, 0);
}

// Por debajo del nivel 6 y sin el nombre en la lista de 0x77997f8: el aviso 479, sin paquete.
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
    EXPECT_EQ(world.Runtime().chat.log.entries.front().type, Client::UI::SystemMessage);
}

// Con el nombre entre los que susurraron (0x77997f8, que llena el 02 que llega), el nivel bajo no frena.
TEST_F(GameChat, ALowLevelMayAnswerWhoWhispered)
{
    world.OwnCharacter().level = 5;
    Client::UI::RememberTheWhisperSender(world.Runtime().chat.whisperSenders, 5, "Alguien");
    ASSERT_TRUE(Type("\rpsst\tAlguien\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).opcode, 0x02);
}

// Sin el modo de F3, o con '/' adelante, va al chat aunque haya un nombre.
TEST_F(GameChat, WithoutTheWhisperModeItGoesToTheChat)
{
    Input().whisperMode = false;
    ASSERT_TRUE(Type("\rpsst\tAlguien\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).opcode, 0x00);
}

// 0x4b5a4f..0x4b5b00: /whisper off y on (los textos 264 y 265) cambian 0x77f3b98 y avisan, sin paquete.
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

// Muriéndose, /move no sale; lo demás sí.
TEST_F(GameChat, ADyingHeroDoesNotSayMove)
{
    world.Hero().dying = 1;
    ASSERT_TRUE(Type("\r/mover\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().panels.overlayMode, Client::UI::MoveOverlayMode);
}

// 0x622b40 con 1: lo que tiene una palabra de Filter.bmd sale como el texto 570.
TEST_F(GameChat, AFilteredWordSaysTheDots)
{
    filter = {"feo"};
    ASSERT_TRUE(Type("\rmuy f eo\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "...");
}

// Con "webzen" en el nombre del héroe solo sale lo que empieza con '/' o con '!'.
TEST_F(GameChat, AWebzenNameOnlySaysCommands)
{
    SetTheHeroName("webzenA");
    ASSERT_TRUE(Type("\rhola\r").HasValue());
    EXPECT_TRUE(sent.empty());
    Input().brake = 0;
    ASSERT_TRUE(Type("\r!hola\r").HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// 0x4b5269: en Chaos Castle, fuera de la zona segura, Enter no hace nada; tampoco con el diálogo 0x7d.
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

// CL9 hito 11: con el foco en el EDIT de [0x5763504] (la entrada del número de serie), la subclase 0x467b10 filtra lo
// tecleado con +0x4c en 2 (cifras y letras, las minúsculas en mayúscula), el borrar es del EDIT y el Tab y el Enter van
// a la ventana del juego: con 0x78abb00 en 3 el Enter solo prende 0x5763588 (0x4b52f6). Nada llega a los campos del
// chat.
TEST_F(GameChat, TheSerialEditFiltersWhatIsTyped)
{
    world.Runtime().panels.field78abb00 = 3;
    world.Runtime().events.serialEntry = true;
    Input().focus = Client::UI::ChatFocus::Dialog;
    Input().dialogEdit.filter = Client::UI::UppercaseLettersAndDigitsEdit;
    Input().dialogEdit.text.SetLimit(12);
    ASSERT_TRUE(Type("ab-1 c\bz9\r\t").HasValue());
    EXPECT_EQ(Input().dialogEdit.text.Value(), "AB1Z9");
    EXPECT_EQ(Input().message.text.Value(), "");
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Dialog);
    EXPECT_TRUE(world.Runtime().ui.enterPressed);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// 0x4b540a: con el campo cerrado, Enter lee el texto del EDIT de [0x5763504] y, sin el modo del zen ni el del título,
// lo dice; el salto a 0x4b8886 no vacía el EDIT, y sale otra vez con el Enter siguiente.
TEST_F(GameChat, WithTheFieldClosedEnterSaysTheSerialEdit)
{
    Input().dialogEdit.text.SetText("AB12");
    ASSERT_TRUE(Type("\r").HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "AB12");
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(Input().dialogEdit.text.Value(), "AB12");
}

// 0x4b5616 y 0x4b8888: con 0x77f3b6b prendido y 0x78abb00 en otro valor (un 94 de otro tipo sobre la entrada, Q-C1346),
// Enter pone el campo elegido en 0 y nada más: no dice, no vacía el buffer y no cierra el campo.
TEST_F(GameChat, AStuckSerialEntrySwallowsEnter)
{
    world.Runtime().panels.field78abb00 = 1;
    world.Runtime().events.serialEntry = true;
    ASSERT_TRUE(Type("\rhola").HasValue());
    ASSERT_TRUE(world.Runtime().ui.chatOpen);
    Input().field = 1;
    Input().focus = Client::UI::ChatFocus::Message;
    const auto before = random.State();
    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().field, Client::UI::MessageField);
    EXPECT_TRUE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(std::string_view(Input().messageBuffer.data()), "hola");
    EXPECT_EQ(Input().message.text.Value(), "hola");
    EXPECT_EQ(random.State(), before);
}

// Un comando que se atiende (/1) no manda nada ni vuelve el área vieja, pero cierra el campo.
TEST_F(GameChat, AHandledCommandOnlyClosesTheField)
{
    world.Runtime().chat.area.firstShown = 0;
    ASSERT_TRUE(Type("\r/1 hola\r").HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Input().macros[0], "hola");
    EXPECT_EQ(world.Runtime().chat.area.firstShown, 0);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_EQ(world.Runtime().lastSoundId, Client::UI::ChatButtonSound);
}

// 0x56c68b..0x56cdfc desde el chat: /trade con el elegido al lado manda el C1 05 36 y el aviso 475 sale armado con su
// nombre (0x69fd9b) en el registro.
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

// 0x5a119f..0x5a2b47: cada macro apretada sale como chat y deja el freno de las macros en 0x64, aunque el freno del
// chat la pare.
TEST_F(GameChat, TheMacrosSayTheirTextAndLock)
{
    Input().macros[0] = "uno";
    Input().macros[9] = "diez";
    std::array<bool, Client::UI::MacroCount> pressed{};
    pressed[0] = true;
    pressed[9] = true;
    const auto before = random.State();
    const auto said = Client::Scenes::SayTheMacros(Context(), pressed);
    ASSERT_TRUE(said.HasValue()) << said.Error();
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "uno");
    EXPECT_EQ(world.Runtime().hud.macroTime, Client::UI::MacroLockSteps);
    EXPECT_EQ(random.State(), before);
}

// Desde una macro (0x56c440 con 1), "/1 …" no guarda nada: sale como chat y frena.
TEST_F(GameChat, FromAMacroTheNumbersAreNotCommands)
{
    Input().macros[0] = "/1x";
    std::array<bool, Client::UI::MacroCount> pressed{};
    pressed[0] = true;
    ASSERT_TRUE(Client::Scenes::SayTheMacros(Context(), pressed).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(Decode(sent[0]).text, "/1x");
    EXPECT_EQ(Input().macros[0], "/1x");
    EXPECT_EQ(world.Runtime().hud.macroTime, Client::UI::MacroLockSteps);
}
} // namespace
