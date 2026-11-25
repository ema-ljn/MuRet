#include "UI/ChatInput.h"

#include "UI/OldWindowBase.h"
#include "UI/OldEditInput.h"
#include "Platform/IClipboard.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// La entrada del chat del juego (CL8 hito 3; 06 § El chat): los dos EDIT, sus buffers, el historial y los nombres
// del susurro. Los textos y los nombres son inventados.
namespace
{
using namespace Client::Platform;
using namespace Client::UI;

ChatInputState OpenInput()
{
    ChatInputState input;
    ShowTheChatFields(input, 0);
    return input;
}

TEST(ChatInputTest, TheBufferIsACStringCopy)
{
    ChatBuffer buffer{};
    CopyIntoTheBuffer(buffer, "abcdef");
    CopyIntoTheBuffer(buffer, std::string_view("xy\0z", 4));
    EXPECT_EQ(CStringOf(buffer), "xy");
    // strcpy no toca lo de detrás del '\0'.
    EXPECT_EQ(buffer[3], 'd');
    const std::string longer(ChatBufferSize + 5, 'q');
    CopyIntoTheBuffer(buffer, longer);
    EXPECT_EQ(CStringOf(buffer).size(), ChatBufferSize - 1);
}

// 0x553570: el segundo buffer solo con `second`.
TEST(ChatInputTest, ResettingTheBuffersLeavesTheWhisperWithoutSecond)
{
    ChatInputState input;
    input.field = WhisperField;
    CopyIntoTheBuffer(input.messageBuffer, "uno");
    CopyIntoTheBuffer(input.whisperBuffer, "Alguien");
    input.messageLength = 3;
    input.whisperLength = 7;

    ResetTheTextBuffers(input, false);
    EXPECT_EQ(input.field, MessageField);
    EXPECT_EQ(CStringOf(input.messageBuffer), "");
    EXPECT_EQ(input.messageLength, 0);
    EXPECT_EQ(CStringOf(input.whisperBuffer), "Alguien");

    ResetTheTextBuffers(input, true);
    EXPECT_EQ(CStringOf(input.whisperBuffer), "");
    EXPECT_EQ(input.whisperLength, 0);
}

// 0x5535bd..0x5535cf: con 0x6c7b8c en 1, el texto del EDIT de [0x5763504] queda vacío, sin tocar su estado ni el foco.
TEST(ChatInputTest, ResettingTheBuffersEmptiesTheDialogEdit)
{
    ChatInputState input;
    input.dialogEdit.text.SetText("1234");
    input.dialogEdit.base.state = ShownWindow;
    input.focus = ChatFocus::Dialog;
    ResetTheTextBuffers(input, false);
    EXPECT_EQ(input.dialogEdit.text.Value(), "");
    EXPECT_EQ(input.dialogEdit.base.state, ShownWindow);
    EXPECT_EQ(input.focus, ChatFocus::Dialog);
}

// 0x469450: 3 oculta y devuelve el foco a la ventana del juego; 0 muestra y da el foco al del mensaje; 1 solo muestra.
TEST(ChatInputTest, ShowingAndHidingMovesTheFocus)
{
    ChatInputState input;
    ShowTheChatFields(input, 1);
    EXPECT_TRUE(input.shown);
    EXPECT_EQ(input.focus, ChatFocus::Game);
    ShowTheChatFields(input, 0);
    EXPECT_EQ(input.focus, ChatFocus::Message);
    ShowTheChatFields(input, 3);
    EXPECT_FALSE(input.shown);
    EXPECT_EQ(input.focus, ChatFocus::Game);
}

// 0x469450(3) oculta sólo los dos del chat: el EDIT del diálogo, el del número o el de una ventana vieja conservan el
// foco (el campo del nombre del gremio no lo pierde cuando un paquete esconde el chat).
TEST(ChatInputTest, HidingTheChatKeepsTheFocusOfAnotherEdit)
{
    for (const ChatFocus other : {ChatFocus::Dialog, ChatFocus::NumberField, ChatFocus::OldWindow})
    {
        ChatInputState input;
        ShowTheChatFields(input, 1);
        input.focus = other;
        ShowTheChatFields(input, 3);
        EXPECT_FALSE(input.shown);
        EXPECT_EQ(input.focus, other);
    }
    ChatInputState whisper;
    ShowTheChatFields(whisper, 1);
    whisper.focus = ChatFocus::Whisper;
    ShowTheChatFields(whisper, 3);
    EXPECT_EQ(whisper.focus, ChatFocus::Game);
}

// 0x469370 no hace nada con los EDIT ocultos; con ellos a la vista, el foco y todo elegido.
TEST(ChatInputTest, FocusingAFieldSelectsAllOfIt)
{
    ChatInputState hidden;
    FocusTheChatField(hidden, WhisperField);
    EXPECT_EQ(hidden.focus, ChatFocus::Game);

    ChatInputState input = OpenInput();
    TypeInTheChat(input, "Nadie");
    FocusTheChatField(input, WhisperField);
    EXPECT_EQ(input.focus, ChatFocus::Whisper);
    TypeInTheChat(input, "Otro");
    FocusTheChatField(input, MessageField);
    EXPECT_TRUE(input.message.text.AllSelected());
    TypeInTheChat(input, "x");
    EXPECT_EQ(input.message.text.Value(), "x");
    EXPECT_EQ(input.whisper.text.Value(), "Otro");

    // El clic deja el cursor al final.
    ClickTheChatField(input, WhisperField);
    EXPECT_EQ(input.focus, ChatFocus::Whisper);
    TypeInTheChat(input, "s");
    EXPECT_EQ(input.whisper.text.Value(), "Otros");
}

// EM_LIMITTEXT: 0x32 bytes en el mensaje y 10 en el susurro; sin un EDIT con el foco, nada.
TEST(ChatInputTest, TypingStopsAtTheLimitsOfTheEdits)
{
    ChatInputState closed;
    TypeInTheChat(closed, "abc");
    EXPECT_EQ(closed.message.text.Value(), "");

    ChatInputState input = OpenInput();
    TypeInTheChat(input, std::string(ChatMessageLimit + 4, 'm'));
    EXPECT_EQ(input.message.text.Value().size(), ChatMessageLimit);
    BackspaceInTheChat(input);
    EXPECT_EQ(input.message.text.Value().size(), ChatMessageLimit - 1);
    FocusTheChatField(input, WhisperField);
    TypeInTheChat(input, "NombreLargoDeMas");
    EXPECT_EQ(input.whisper.text.Value(), "NombreLarg");
}

TEST(ChatInputTest, MessageAndWhisperStoreXpWindows1252Bytes)
{
    ChatInputState input = OpenInput();
    TypeInTheChat(input, "€漢😊é");
    const std::string expected("\x80???e\xb4", 6);

    EXPECT_EQ(input.message.text.Value().size(), expected.size());
    EXPECT_TRUE(input.message.text.Value() == expected);

    input.focus = ChatFocus::Whisper;
    TypeInTheChat(input, "€漢😊é");

    EXPECT_EQ(input.whisper.text.Value().size(), expected.size());
    EXPECT_TRUE(input.whisper.text.Value() == expected);
}

TEST(ChatInputTest, MessageAndWhisperLimitsCountEncodedBytesAndReplaceSelection)
{
    ChatInputState input = OpenInput();
    input.message.text.SetText(std::string(ChatMessageLimit - 1, 'm'));
    TypeInTheChat(input, "€x");
    ASSERT_EQ(input.message.text.Value().size(), ChatMessageLimit);
    EXPECT_EQ(static_cast<unsigned char>(input.message.text.Value().back()), 0x80U);

    input.whisper.text.SetText(std::string(ChatWhisperLimit - 1, 'w'));
    input.focus = ChatFocus::Whisper;
    TypeInTheChat(input, "€x");
    ASSERT_EQ(input.whisper.text.Value().size(), ChatWhisperLimit);
    EXPECT_EQ(static_cast<unsigned char>(input.whisper.text.Value().back()), 0x80U);

    input.message.text.SetText("antiguo");
    input.message.text.SelectAll();
    input.focus = ChatFocus::Message;
    TypeInTheChat(input, "€");
    EXPECT_TRUE(input.message.text.Value() == std::string("\x80", 1));

    input.whisper.text.SetText("anterior");
    input.whisper.text.SelectAll();
    input.focus = ChatFocus::Whisper;
    TypeInTheChat(input, "€");
    EXPECT_TRUE(input.whisper.text.Value() == std::string("\x80", 1));
}

// 0x4693e0 y 0x4694c0: los EDIT a los buffers, y un texto de 0x100 bytes o más no se pone.
TEST(ChatInputTest, TheFieldsGoToTheBuffersAndBack)
{
    ChatInputState input = OpenInput();
    TypeInTheChat(input, "hola");
    FocusTheChatField(input, WhisperField);
    TypeInTheChat(input, "Alguien");
    ReadTheChatFields(input);
    EXPECT_EQ(CStringOf(input.messageBuffer), "hola");
    EXPECT_EQ(CStringOf(input.whisperBuffer), "Alguien");

    SetTheChatFields(input, "chau", std::nullopt);
    EXPECT_EQ(input.message.text.Value(), "chau");
    EXPECT_EQ(input.whisper.text.Value(), "Alguien");
    SetTheChatFields(input, std::string(ChatBufferSize, 'x'), "");
    EXPECT_EQ(input.message.text.Value(), "chau");
    EXPECT_EQ(input.whisper.text.Value(), "");
}

// 0x469540 y 0x469790: lo más nuevo adelante, diez como mucho; subir guarda lo que se escribía y bajar lo devuelve.
TEST(ChatInputTest, TheHistoryKeepsTheTenNewestAndWhatWasBeingTyped)
{
    ChatInputState input = OpenInput();
    EXPECT_FALSE(RecallFromTheHistory(input, -1));
    RememberTheMessage(input, "");
    EXPECT_TRUE(input.history.said.empty());
    for (int said = 0; said < 12; ++said)
        RememberTheMessage(input, "frase" + std::to_string(said));
    ASSERT_EQ(input.history.said.size(), ChatHistoryLimit);
    EXPECT_EQ(input.history.said.front(), "frase11");
    EXPECT_EQ(input.history.said.back(), "frase2");

    TypeInTheChat(input, "a medias");
    // El primero solo guarda lo que se escribía y muestra el más nuevo.
    EXPECT_TRUE(RecallFromTheHistory(input, -1));
    EXPECT_EQ(input.message.text.Value(), "frase11");
    EXPECT_TRUE(RecallFromTheHistory(input, -1));
    EXPECT_EQ(input.message.text.Value(), "frase10");
    // Más arriba del más viejo, queda en él.
    EXPECT_TRUE(RecallFromTheHistory(input, -20));
    EXPECT_EQ(input.message.text.Value(), "frase2");
    EXPECT_TRUE(RecallFromTheHistory(input, 8));
    EXPECT_EQ(input.message.text.Value(), "frase10");
    EXPECT_TRUE(RecallFromTheHistory(input, 1));
    EXPECT_EQ(input.message.text.Value(), "frase11");
    EXPECT_TRUE(RecallFromTheHistory(input, 1));
    EXPECT_EQ(input.message.text.Value(), "a medias");
    EXPECT_FALSE(input.history.saved);
    // Sin nada guardado, bajar desde el más nuevo no cambia el campo, pero suena.
    EXPECT_TRUE(RecallFromTheHistory(input, 1));
    EXPECT_EQ(input.message.text.Value(), "a medias");
}

// 0x4b4f12: Tab con el campo abierto pasa al siguiente; con uno solo y vacío, invierte 0x77f3b69.
TEST(ChatInputTest, TabMovesBetweenTheFields)
{
    ChatInputState input = OpenInput();
    EXPECT_FALSE(PressTabInTheChat(input, false));
    EXPECT_EQ(input.field, MessageField);

    EXPECT_TRUE(PressTabInTheChat(input, true));
    EXPECT_EQ(input.field, WhisperField);
    EXPECT_EQ(input.focus, ChatFocus::Whisper);
    EXPECT_TRUE(PressTabInTheChat(input, true));
    EXPECT_EQ(input.field, MessageField);
    EXPECT_EQ(input.focus, ChatFocus::Message);

    input.fieldCount = 1;
    EXPECT_FALSE(PressTabInTheChat(input, true));
    EXPECT_TRUE(input.otherInput);
    input.messageLength = 2;
    EXPECT_FALSE(PressTabInTheChat(input, true));
    EXPECT_TRUE(input.otherInput);
}

// 0x5a01ac y 0x5a02f8: el nombre de ahora queda en su lugar (11 bytes), el lugar se corre y el nuevo pasa al EDIT
// y al buffer; fuera de 0..4, nada.
TEST(ChatInputTest, TheArrowsWalkTheWhisperNames)
{
    ChatInputState input = OpenInput();
    PutTheWhisperName(input, "Alguien");
    EXPECT_EQ(input.whisperLength, 7);
    // Q-C530: el EDIT no cambia.
    EXPECT_EQ(input.whisper.text.Value(), "");

    EXPECT_TRUE(StepTheWhisperName(input, -1));
    EXPECT_EQ(input.whisperNameAt, 1);
    EXPECT_EQ(CStringOf(input.whisperNames[2]), "Alguien");
    EXPECT_EQ(CStringOf(input.whisperBuffer), "");
    EXPECT_EQ(input.whisperLength, 0);

    EXPECT_TRUE(StepTheWhisperName(input, 1));
    EXPECT_EQ(input.whisperNameAt, 2);
    EXPECT_EQ(input.whisper.text.Value(), "Alguien");
    EXPECT_EQ(CStringOf(input.whisperBuffer), "Alguien");
    EXPECT_EQ(input.whisperLength, 7);
    // Con el campo del mensaje, el foco no se mueve.
    EXPECT_EQ(input.focus, ChatFocus::Message);

    EXPECT_TRUE(StepTheWhisperName(input, 1));
    EXPECT_TRUE(StepTheWhisperName(input, 1));
    EXPECT_FALSE(StepTheWhisperName(input, 1));
    EXPECT_EQ(input.whisperNameAt, LastWhisperNameSlot);
}

// [0x5763508] (CL9 hito 8): oculto, con ES_PASSWORD y el tope 9 de WinMain (0x4bbad7); a la vista toma el foco y lo
// tecleado pasa por el filtro de la subclase (0x467bd1): con el bit 0 (el 9 del diálogo 0x7d) solo los dígitos.
// GetText llega al buffer y SetText(0) lo vacía; 0x553570 también (con o sin el segundo buffer).
TEST(ChatInputTest, TheNumberFieldTakesOnlyDigits)
{
    ChatInputState input;
    EXPECT_EQ(input.numberField.base.state, HiddenWindow);
    EXPECT_EQ(input.numberField.base.id, NumberFieldId);
    EXPECT_TRUE(input.numberField.text.Masked());
    FocusTheNumberField(input);
    EXPECT_EQ(input.focus, ChatFocus::Game);

    ShowTheNumberField(input, 0);
    FocusTheNumberField(input);
    EXPECT_EQ(input.focus, ChatFocus::NumberField);
    input.numberField.filter = 9;
    TypeInTheChat(input, "12a-B 3456789");
    EXPECT_EQ(input.numberField.text.Value(), "123456789");
    EXPECT_EQ(input.numberField.text.Shown(), "*********");
    BackspaceInTheChat(input);
    EXPECT_EQ(input.numberField.text.Value(), "12345678");
    EXPECT_EQ(input.message.text.Value(), "");

    ReadTheNumberField(input, input.messageBuffer);
    EXPECT_EQ(CStringOf(input.messageBuffer), "12345678");
    ClearTheNumberField(input);
    EXPECT_EQ(input.numberField.text.Value(), "");

    TypeInTheChat(input, "77");
    ResetTheTextBuffers(input, false);
    EXPECT_EQ(input.numberField.text.Value(), "");

    // 0x468260 con 3: oculto, el foco vuelve al juego.
    ShowTheNumberField(input, 3);
    EXPECT_EQ(input.focus, ChatFocus::Game);
    TypeInTheChat(input, "5");
    EXPECT_EQ(input.numberField.text.Value(), "");
}

// El filtro con el bit 1 (sin el 0): los dígitos y las letras, las minúsculas en mayúsculas; sin bits, todo pasa.
TEST(ChatInputTest, TheLettersFilterRaisesTheLowercase)
{
    ChatInputState input;
    ShowTheDialogField(input, 0);
    FocusTheDialogField(input);
    ASSERT_EQ(input.focus, ChatFocus::Dialog);
    input.dialogEdit.filter = 2;
    TypeInTheChat(input, "ab-C 9_z");
    EXPECT_EQ(input.dialogEdit.text.Value(), "ABC9Z");
    input.dialogEdit.filter = 0;
    TypeInTheChat(input, "-x ");
    EXPECT_EQ(input.dialogEdit.text.Value(), "ABC9Z-x ");
    EXPECT_EQ(input.dialogEdit.base.id, DialogFieldId);
}

// 0x469320: el campo 0, los EDIT vacíos y el historial también.
TEST(ChatInputTest, ResettingTheInputEmptiesTheFieldsAndTheHistory)
{
    ChatInputState input = OpenInput();
    TypeInTheChat(input, "algo");
    RememberTheMessage(input, "dicho");
    input.field = WhisperField;

    ResetTheChatInput(input);

    EXPECT_EQ(input.field, MessageField);
    EXPECT_EQ(input.message.text.Value(), "");
    EXPECT_TRUE(input.history.said.empty());
}

class Clipboard final : public IClipboard
{
public:
    std::optional<std::string> ReadText() override
    {
        return content;
    }
    bool WriteText(std::string_view text) override
    {
        if (failWrite)
            return false;
        content = std::string(text);
        return true;
    }
    std::optional<std::string> content{};
    bool failWrite = false;
};

TEST(ChatInputTest, TheChatFieldsAreTheSameSingleLineEditAsTheDialogs)
{
    ChatInputState input;
    EXPECT_EQ(input.message.base.x, 193);
    EXPECT_EQ(input.message.base.y, 422);
    EXPECT_EQ(input.message.base.width, 180);
    EXPECT_EQ(input.message.base.height, 14);
    EXPECT_EQ(input.whisper.base.x, 376);
    EXPECT_EQ(input.whisper.base.width, 50);
    EXPECT_EQ(input.message.multiline, 0);
    EXPECT_EQ(input.message.filter, 0);
    EXPECT_EQ(input.message.textColor, 0xffd2e6ffu);
    EXPECT_EQ(input.message.backgroundColor, 0xff000000u);
    ShowTheChatFields(input, 0);
    EXPECT_EQ(input.message.base.state, ShownWindow);
    EXPECT_EQ(input.whisper.base.state, ShownWindow);
    ShowTheChatFields(input, 3);
    EXPECT_EQ(input.message.base.state, HiddenWindow);
    EXPECT_EQ(input.whisper.base.state, HiddenWindow);
}

TEST(ChatInputTest, BothChatFieldsShareTheEditCaretInsteadOfAppendingAtTheEnd)
{
    for (const auto focus : {ChatFocus::Message, ChatFocus::Whisper})
    {
        ChatInputState input = OpenInput();
        input.focus = focus;
        TypeInTheChat(input, "abcd");
        OldEdit* edit = ChatEditWithFocus(input);
        ASSERT_NE(edit, nullptr);
        ApplyKeyToTheEdit(*edit, OldEditKey::Home, false, false);
        ApplyKeyToTheEdit(*edit, OldEditKey::Right, false, false);
        TypeInTheChat(input, "x");
        EXPECT_EQ(edit->text.Value(), "axbcd");
        EXPECT_EQ(edit->text.Cursor(), 2u);
    }
}

TEST(ChatInputTest, ChatSelectionAndClipboardUseTheSharedWesternEditBehavior)
{
    ChatInputState input = OpenInput();
    TypeInTheChat(input, "uno dos");
    OldEdit& edit = *ChatEditWithFocus(input);
    Clipboard clipboard;
    ApplyKeyToTheEdit(edit, OldEditKey::Home, false, true);
    ApplyKeyToTheEdit(edit, OldEditKey::Right, false, true);
    ApplyKeyToTheEdit(edit, OldEditKey::End, true, false);
    EXPECT_EQ(edit.text.Selected(), "dos");
    ApplyKeyToTheEdit(edit, OldEditKey::CtrlA, false, true);
    EXPECT_EQ(edit.text.Selected(), "dos"); // El EDIT XP deja Ctrl+A sin efecto.
    clipboard.failWrite = true;
    ApplyKeyToTheEdit(edit, OldEditKey::Cut, false, true, &clipboard);
    EXPECT_EQ(edit.text.Value(), "uno dos");
    clipboard.failWrite = false;
    ApplyKeyToTheEdit(edit, OldEditKey::Cut, false, true, &clipboard);
    EXPECT_EQ(clipboard.content, std::optional<std::string>{"dos"});
    EXPECT_EQ(edit.text.Value(), "uno ");
    clipboard.content = "€";
    ApplyKeyToTheEdit(edit, OldEditKey::Paste, false, true, &clipboard);
    EXPECT_EQ(edit.text.Value(), std::string("uno \x80", 5));
    ApplyKeyToTheEdit(edit, OldEditKey::Left, false, false);
    ApplyKeyToTheEdit(edit, OldEditKey::Delete, false, false);
    EXPECT_EQ(edit.text.Value(), "uno ");
}

TEST(ChatInputTest, TheWhisperPasteRespectsItsByteLimitAndSingleLineProfile)
{
    ChatInputState input = OpenInput();
    input.focus = ChatFocus::Whisper;
    Clipboard clipboard;
    clipboard.content = "é123456789XYZ\rsegunda";
    ApplyKeyToTheEdit(*ChatEditWithFocus(input), OldEditKey::Paste, false, true, &clipboard);
    EXPECT_EQ(input.whisper.text.Value(), std::string("\xe9"
                                                      "123456789",
                                                      10));
    EXPECT_EQ(input.message.text.Size(), 0u);
}

// 0x4682f1..0x4682f7: tomar el foco deja el id del EDIT en la lista que recibe las flechas; ocultarlo no la limpia.
TEST(ChatInputTest, TakingChatFocusReplacesThePreviousListsArrowTarget)
{
    ChatInputState input;
    std::int32_t arrowTarget = 123;
    ShowTheChatFields(input, 1, &arrowTarget);
    EXPECT_EQ(arrowTarget, 123);
    ShowTheChatFields(input, 0, &arrowTarget);
    EXPECT_EQ(arrowTarget, 1);
    ClickTheChatField(input, WhisperField, &arrowTarget);
    EXPECT_EQ(arrowTarget, 2);
    ShowTheChatFields(input, 3, &arrowTarget);
    EXPECT_EQ(arrowTarget, 2);
    EXPECT_EQ(input.focus, ChatFocus::Game);
    FocusTheChatField(input, MessageField, &arrowTarget);
    EXPECT_EQ(arrowTarget, 2); // 0x469370: con los dos ocultos, no llama al EDIT.
    ShowTheChatFields(input, 1, &arrowTarget);
    FocusTheChatField(input, MessageField, &arrowTarget);
    EXPECT_EQ(arrowTarget, 1);
    EXPECT_TRUE(PressTabInTheChat(input, true, &arrowTarget));
    EXPECT_EQ(arrowTarget, 2);
    EXPECT_TRUE(PressTabInTheChat(input, true, &arrowTarget));
    EXPECT_EQ(arrowTarget, 1);
}

TEST(ChatInputTest, PreeditAndCommittedChatTextRemainSeparate)
{
    ChatInputState input = OpenInput();
    TypeInTheChat(input, "ab");
    OldEdit& edit = *ChatEditWithFocus(input);
    edit.draws = 15;
    SetTheEditComposition(edit, "preview", 1, 2);
    ApplyKeyToTheEdit(edit, OldEditKey::Left, false, false);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "ab");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    TypeInTheChat(input, "é");
    EXPECT_EQ(edit.text.Value(), std::string("ab\xe9", 3));
    EXPECT_TRUE(edit.compositionText.empty());
    EXPECT_EQ(edit.compositionStart, 0);
    EXPECT_EQ(edit.compositionLength, 0);
}
} // namespace
