#include "Scenes/GameChat.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/LoginModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/OldControls.h"
#include "UI/PanelRegistry.h"
#include "UI/WhisperSenders.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/PlayerActors.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La entrada del chat en el juego (CL8 hito 3; 06 § El chat): Enter, lo tecleado, el C1 00 y el C1 02, y las macros.
// Los textos y los nombres son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
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
    const auto plain = Client::Network::Crypto::Xor32Decode(sent.packet);
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
        world.Actors()[world.Runtime().hero].type = Client::Data::Models::PlayerSkeletonModel;
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    }

    void SetTheHeroName(std::string_view name)
    {
        auto& hero = world.Actors()[world.Runtime().hero];
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

    WorldState world;
    CrtRandom random;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::vector<std::string> filter;
    LetterMeasure letters;
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Scenes::SingleModelLibrary models;
};

// Un C1 05 18 [direction] [action] en claro, con el XOR32.
bool IsTheAction(const Client::Protocol::Outgoing& sent, std::uint8_t direction, std::uint8_t action)
{
    return Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(sent.packet).Bytes(),
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
    // Lo de antes de abrir lo descartó la ventana del juego.
    EXPECT_EQ(Input().message.Value(), "");

    ASSERT_TRUE(Type("\r").HasValue());
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_FALSE(Input().shown);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Game);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(random.State(), before);
}

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
    EXPECT_EQ(Input().message.Value(), "");
    EXPECT_EQ(Input().messageLength, 0);
}

// 0x4b56cc y 0x576eb0: un emote en lo dicho, con el héroe parado, lo anima y manda dos C1 05 18 iguales antes del
// C1 00 (Q-C776), con la dirección de su ángulo. No saca ningún rand().
TEST_F(GameChat, AnEmoteSendsItsActionsBeforeTheChat)
{
    auto& hero = world.Actors()[world.Runtime().hero];
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

// Caminando, solo el C1 18 del caso y el héroe no se anima; montado fuera de la zona segura (la compuerta de la CL8),
// ninguno.
TEST_F(GameChat, AWalkingHeroSendsOnlyTheCaseAndAMountedOneNone)
{
    auto& hero = world.Actors()[world.Runtime().hero];
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
    world.Actors()[world.Runtime().hero].dying = 1;
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
    EXPECT_EQ(Input().message.Value(), "");
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::Dialog);
    EXPECT_TRUE(world.Runtime().ui.enterPressed);
    EXPECT_FALSE(world.Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());
}

// 0x4b540a: con el campo cerrado, Enter lee el texto del EDIT de [0x5763504]; lo que dejó la X de la ventana de
// 0x78abb00 (que no lo vacía, Q-C1345) sale como chat, y sale otra vez con el Enter siguiente.
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
    EXPECT_EQ(Input().message.Value(), "hola");
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
