#pragma once

#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/TextField.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

// La entrada del chat del juego (CL8 hito 3; 06 § El chat). Con 0x6c7b8c en 1 (.data), el chat escribe en dos EDIT
// de Windows del objeto [0x57634f8] (0x330 bytes, vtable 0x6ba0f0, armado por 0x4691e0): el del mensaje en +4 y el
// del susurro en +0xf0 (la clase 0x4674c0, vtable 0x6b9550), con el historial de lo dicho en +0x2e4. El WndProc
// (0x4af4e0) y el paso (0x59ff10) comparten con él los globales del campo: los buffers 0x7798bf8 (el mensaje) y
// 0x7798cf8 (el nombre del susurro) con sus largos (0x7758590 y 0x7758594), el campo elegido (0x77f3b70), los
// nombres del susurro (0x77f31cc), los frenos del chat (0x5767940 y 0x57676e4), el modo de F3 (0x6c8dd0) y las
// macros (0x77f1db0). /whisper off y on (0x77f3b98), el último destinatario (0x57678c4) y los que susurraron
// (0x77997f8) los comparte con el chat que llega y están en ChatRuntime. *Estático*.
//
// Los EDIT se emulan como el login (TextField, Latin-1 como el EDIT de Occidente): lo tecleado entra al final, una
// selección de todo se reemplaza con lo primero que entra, y no hay cursor en el medio ni IME (decisión 9 del plan
// de la CL8). Lo que el jugador escribe no va al log.
namespace Client::UI
{
// 0x4691e0: EM_LIMITTEXT del mensaje (0x32) y del susurro (10).
inline constexpr std::size_t ChatMessageLimit = 0x32;
inline constexpr std::size_t ChatWhisperLimit = 10;
// 0x7798bf8 y 0x7798cf8: dos de los diez buffers de 0x100 que vacía 0x553570.
inline constexpr std::size_t ChatBufferSize = 0x100;
// 0x469a80(0): el historial guarda los diez más nuevos.
inline constexpr std::size_t ChatHistoryLimit = 10;
// 0x77f31cc..0x77f36cc: cinco nombres de 0x100; 0x6c8ea8 (.data 2) es el de ahora y va de 0 a 4.
inline constexpr std::size_t WhisperNameSlots = 5;
inline constexpr std::int32_t FirstWhisperNameSlot = 2;
inline constexpr std::int32_t LastWhisperNameSlot = 4;
// 0x5a01bc y 0x5a0244: strncpy(…, 0xb), el nombre y su '\0'.
inline constexpr std::size_t WhisperNameCopy = 0xb;
// 0x77f1db0..0x77f27b0: diez macros de 0x100, la de Alt + 1 primero y la de Alt + 0 al final.
inline constexpr std::size_t MacroCount = 10;
// 0x5a1ea1: lo que deja en 0x77f3b74 (HudState::macroTime) una macro que llegó al freno.
inline constexpr std::int32_t MacroLockSteps = 0x64;
// 0x77f3b70: el campo elegido. Tab lo corre entre 0 y 0x6c8e6c − 1; los impares son el del susurro.
inline constexpr std::int32_t MessageField = 0;
inline constexpr std::int32_t WhisperField = 1;
// 0x6c8e6c: cuántos campos recorre Tab, 2 desde el primer paso del juego y al abrir.
inline constexpr std::int32_t ChatFieldCount = 2;
// [0x5763504] y [0x5763508]: los topes de los dos EDIT de los diálogos antes de que alguien les ponga otro: WinMain los
// arma con +0x2c([0x5763544], 0x32, 0) (0x4bbaaa) y +0x2c([0x5763544], 9, 1) (0x4bbad7), y 0x467fe0 le pasa el segundo
// argumento a la vtable + 0x3c (EM_LIMITTEXT, 0x46811e) y con el tercero en 1 prende ES_PASSWORD y +0xb0 (0x468005).
// La ventana 2 del gremio le pone 8 al primero en cada dibujo de la página 1 (0x655202) y el diálogo 0x7d, 7 al
// segundo (0x630b2b). *Estático*.
inline constexpr std::size_t DialogFieldLimit = 0x32;
inline constexpr std::size_t NumberFieldLimit = 9;
// +0x40 de los dos (el contador 0x5762eb0, 0x459ef0): WinMain arma antes los seis EDIT de [0x57634f8], [0x57634fc] y
// [0x5763500], que se llevan del 1 al 6, y después el registro del chat (UI::ChatLogId, 9). *Inferido*, como ese.
inline constexpr std::int32_t DialogFieldId = 7;
inline constexpr std::int32_t NumberFieldId = 8;
// 0x469790(10000): volver al más nuevo.
inline constexpr std::int32_t NewestInTheHistory = 10000;

// El EDIT de los diálogos, oculto como lo deja WinMain (+4(3) en 0x4bbabc); lo demás, como lo deja 0x4674c0.
[[nodiscard]] inline OldEdit DialogFieldEdit()
{
    OldEdit edit;
    edit.base.id = DialogFieldId;
    edit.base.state = HiddenWindow;
    edit.text = TextField{DialogFieldLimit, false, TextField::Charset::Latin1};
    return edit;
}

// El segundo EDIT de los diálogos, con ES_PASSWORD (TextField::Masked, +0xb0) y oculto (+4(3) en 0x4bbae9); lo demás,
// como lo deja 0x4674c0. WinMain les pone a los dos la fuente normal (+0x34 con [0x5763554], 0x4bbb42 y 0x4bbb59).
[[nodiscard]] inline OldEdit NumberFieldEdit()
{
    OldEdit edit;
    edit.base.id = NumberFieldId;
    edit.base.state = HiddenWindow;
    edit.text = TextField{NumberFieldLimit, true, TextField::Charset::Latin1};
    return edit;
}

// Qué ventana tiene el foco del teclado (GetFocus()): la del juego ([0x5763544]) o uno de los dos EDIT.
enum class ChatFocus : std::uint8_t
{
    Game,
    Message,
    Whisper,
    // El EDIT de una ventana vieja (la caja de agregar un amigo, 0x4682a0): OldWindowManager::focusedEdit.
    OldWindow,
    // El EDIT de los diálogos, [0x5763504] (el nombre del gremio nuevo de la ventana 2, CL9 hito 8).
    DialogField,
    // El segundo, [0x5763508] (el número del diálogo 0x7d del juego, CL9 hito 8).
    NumberField,
};

using ChatBuffer = std::array<char, ChatBufferSize>;

// El historial: el deque<char*> de +0x2e4 (lo más nuevo adelante), el iterador de +0x310 (0, el primero; `said`
// entero, el final), y lo que se estaba escribiendo antes de subir (+0x1dc y el buffer de +0x1e0).
struct ChatHistory
{
    std::deque<std::string> said;
    std::size_t at = 0;
    bool saved = false;
    std::string typing;
};

struct ChatInputState
{
    TextField message{ChatMessageLimit, false, TextField::Charset::Latin1};
    TextField whisper{ChatWhisperLimit, false, TextField::Charset::Latin1};
    // +0x48 de los dos EDIT: con 3 ocultos (ShowWindow(SW_HIDE), 0x468260). 0x469450 los pone juntos.
    bool shown = false;
    ChatFocus focus = ChatFocus::Game;
    std::int32_t field = MessageField;
    std::int32_t fieldCount = ChatFieldCount;
    ChatBuffer messageBuffer{};
    ChatBuffer whisperBuffer{};
    std::int32_t messageLength = 0;
    std::int32_t whisperLength = 0;
    ChatHistory history;
    std::array<ChatBuffer, WhisperNameSlots> whisperNames{};
    std::int32_t whisperNameAt = FirstWhisperNameSlot;
    // 0x77f3b69: el campo de texto de otra entrada (la del diálogo de los paneles, hito 6). Con 1, Enter no abre ni
    // cierra y 0x5aa9a0 dibuja la caja. MuRet no lo prende.
    bool otherInput = false;
    // [0x5763504]: el EDIT de los diálogos (la clase 0x4674c0 que arma WinMain en 0x4bb344), que en el juego usa la
    // ventana 2 del gremio para el nombre (0x655130 lo muestra, lo ubica y lo dibuja con 0x468320; 0x654f80 lo lee y
    // lo oculta). Latin-1 como los del chat; lo tecleado entra con el foco en ChatFocus::DialogField. Lo que se ve de
    // él (+0x4c, los colores, el lugar, los dibujos y el estado de +0x48) es el de las ventanas viejas (OldEdit).
    OldEdit dialogField = DialogFieldEdit();
    // +0x48 del EDIT de los diálogos a la vista (0x468260 con otro que 3), lo mismo que `dialogField.base.state`.
    bool dialogFieldShown = false;
    // [0x5763508]: el segundo EDIT de los diálogos (la clase 0x4674c0 que arma WinMain en 0x4bb395), con ES_PASSWORD y
    // el filtro que le pone su dibujo. En el juego lo usa el diálogo 0x7d (0x630af0 lo muestra, lo ubica, le da el foco
    // y lo dibuja; el sí de 0x62a878 lo lee y lo vacía, y nadie lo oculta: Q-C1295). Lo tecleado entra con el foco en
    // ChatFocus::NumberField; a la vista, con su +0x48 (`numberField.base.state`).
    OldEdit numberField = NumberFieldEdit();
    // 0x77f3b68: el campo del diálogo prendido (el 01 del Guild Master, 0x4e3450, y el botón Crear, 0x655d70; lo apaga
    // 0x6570b0). Con él, Enter no es del chat (0x4b5115) y no hay atajos de teclado (0x6578e0).
    bool dialogFieldOn = false;
    // 0x6c8dd0 (.data 1): F3 y el botón de la caja. Con 0, el nombre del susurro no cuenta y todo va al chat.
    bool whisperMode = true;
    // 0x5767940 y 0x57676e4: el freno del chat. Cada mensaje lo pone en 0x46 y 0x637d2c lo baja uno por paso; con
    // más de 0x32 no sale nada, y con más de 0 no sale lo mismo que el anterior.
    std::int32_t brake = 0;
    std::string lastSaid;
    // 0x77f1db0: las macros, como las dejó Data\Macro.txt (0x61c1c0) y las cambia /1../0 (0x56f0b0).
    std::array<std::string, MacroCount> macros{};
};

// El texto de un buffer hasta su '\0'.
[[nodiscard]] std::string_view CStringOf(const ChatBuffer& buffer);
// strcpy: `text` (hasta su '\0') y el '\0', sin tocar lo que queda detrás. Lo que no entra se corta.
void CopyIntoTheBuffer(ChatBuffer& buffer, std::string_view text);

// 0x553570(segundo): el campo elegido en 0, los buffers 0 y, con `second`, 1 en ceros y sus largos en 0, y SetText(0)
// en los dos EDIT de los diálogos ([0x5763504] y [0x5763508]). Los otros ocho buffers, sus largos, las marcas de
// 0x77f31c0 y 0x6c8e74 no los tiene MuRet.
void ResetTheTextBuffers(ChatInputState& input, bool second);

// 0x469420: SetText(0) en los dos EDIT.
void ClearTheChatFields(ChatInputState& input);

// 0x469370(campo): con los EDIT a la vista, el foco al del susurro (impar) o al del mensaje, con todo elegido
// (EM_SETSEL(0, −1)).
void FocusTheChatField(ChatInputState& input, std::int32_t field);

// 0x4682a0(1) sobre el EDIT del campo `field` (el clic en él, 0x468d50): el foco y el cursor al final, sin nada
// elegido (EM_SETSEL(−2, −1)). El original también deja el id del EDIT en 0x5762ec0, que MuRet no lee.
void ClickTheChatField(ChatInputState& input, std::int32_t field);

// 0x469450(estado): 3 los oculta; otro los muestra, y 0 además le da el foco al del mensaje (0x4682a0 con 0: sin
// cambiar lo elegido). Al ocultarlos, el foco del teclado vuelve a la ventana del juego (*inferido*: MuRet no tiene
// un EDIT oculto que reciba teclas).
void ShowTheChatFields(ChatInputState& input, std::int32_t state);

// 0x469320: el campo elegido en 0, el foco al del mensaje (0x469370), los dos vacíos (0x469420) y el historial
// vacío (0x469a80(1)).
void ResetTheChatInput(ChatInputState& input);

// 0x4693e0(0x7798bf8, 0x7798cf8): GetText de los dos EDIT a sus buffers, hasta 0xff bytes.
void ReadTheChatFields(ChatInputState& input);

// 0x4694c0(a, texto a, b, texto b): SetText en el del mensaje y en el del susurro, cada uno si viene. 0x467ee0 no
// pone un texto de 0x100 bytes o más.
void SetTheChatFields(ChatInputState& input, std::optional<std::string_view> message,
                      std::optional<std::string_view> whisper);

// 0x469540 (la vtable + 8): lo dicho al principio del historial si no está vacío, el iterador al principio, sin lo
// guardado, y 0x469a80(0).
void RememberTheMessage(ChatInputState& input, std::string_view said);

// 0x469a80(todo): con `all` lo vacía; si no, deja los ChatHistoryLimit más nuevos.
void TrimTheHistory(ChatHistory& history, bool all);

// 0x469790(n) (la vtable + 0xc): con n < 0 sube a lo más viejo (guardando lo que se escribía la primera vez), con
// n > 0 baja a lo más nuevo (y del primero, a lo guardado), y con 10000 vuelve al primero. Pone el texto en el EDIT
// del mensaje con el cursor al final. Con el historial vacío no hace nada; el original pide además el contexto del
// IME de la ventana del EDIT y su modo sin el 0x40, que MuRet da por buenos (08, P-C85). Devuelve si sonó el 0x19.
bool RecallFromTheHistory(ChatInputState& input, std::int32_t steps);

// 0x4b4f12..0x4b4fb2: Tab en la ventana del juego (el WM_CHAR 9 que le pasa la subclase 0x467b10). Con el campo del
// chat abierto y dos o más campos, pasa al que sigue (0x469370 en el chat; en el EDIT del login, [0x57634fc], que en
// el juego está oculto, nada); con uno, si su largo es 0, invierte 0x77f3b69. Devuelve si sonó el 0x19.
bool PressTabInTheChat(ChatInputState& input, bool chatOpen);

// 0x468260(estado) sobre el EDIT de los diálogos (la vtable + 4): 3 lo oculta, y el foco del teclado vuelve a la
// ventana del juego si lo tenía (*inferido*, como ShowTheChatFields); otro lo muestra.
void ShowTheDialogField(ChatInputState& input, std::int32_t state);

// 0x4682a0(0) sobre el EDIT de los diálogos (la vtable + 0x30): el foco, sin cambiar lo elegido. Oculto, nada (SetFocus
// sobre una ventana oculta no la toma, *inferido*).
void FocusTheDialogField(ChatInputState& input);

// 0x467ec0(buffer, 0xff) sobre el EDIT de los diálogos (la vtable + 0x50): GetText, hasta 0xff bytes y el '\0'.
void ReadTheDialogField(const ChatInputState& input, ChatBuffer& buffer);

// Lo mismo sobre el segundo EDIT de los diálogos ([0x5763508]): 0x468260 (+4), 0x4682a0(0) (+0x30) y 0x467ec0(buffer,
// 0xff) (+0x50), y 0x467ee0(0) (+0x4c, SetText con nada).
void ShowTheNumberField(ChatInputState& input, std::int32_t state);
void FocusTheNumberField(ChatInputState& input);
void ReadTheNumberField(const ChatInputState& input, ChatBuffer& buffer);
void ClearTheNumberField(ChatInputState& input);

// Lo tecleado al EDIT con el foco, como los WM_CHAR que Windows le da: los bytes van al final (o reemplazan lo
// elegido) y BackspaceChar borra. Sin un EDIT con el foco, la ventana del juego los descarta (0x4b88ee). En los dos
// EDIT de los diálogos pasa antes por el filtro de su subclase (0x467bd1..0x467c64, OldEdit::filter): con el bit 0,
// solo los dígitos; si no, con el bit 1, los dígitos y las letras, las minúsculas en mayúsculas. Lo demás se lo traga
// (devuelve 0 sin llamar al WndProc del EDIT). Los de control que el filtro 1 deja (3, 0x16 y 0x18: copiar, pegar con
// 0x467a90 y cortar) no llegan como texto a MuRet.
void TypeInTheChat(ChatInputState& input, std::string_view typed);
void BackspaceInTheChat(ChatInputState& input);

// 0x5a01ac..0x5a026f (arriba, −1) y 0x5a02f8..0x5a03bd (abajo, +1): el nombre del buffer del susurro queda en el
// lugar de ahora, el buffer se vacía, el lugar se corre (de 0 a LastWhisperNameSlot), el EDIT del susurro toma el
// nombre del lugar nuevo (y el foco con todo elegido si el campo es el impar) y el buffer también. Fuera de los
// bordes, nada. Devuelve si sonó el 0x19.
bool StepTheWhisperName(ChatInputState& input, std::int32_t direction);

// 0x59ffe0..0x5a0083: strcpy del nombre al buffer del susurro y su largo; el EDIT no cambia (Q-C530).
void PutTheWhisperName(ChatInputState& input, std::string_view name);
} // namespace Client::UI
