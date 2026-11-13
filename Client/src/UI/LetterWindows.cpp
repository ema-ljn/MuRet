#include "UI/LetterWindows.h"

#include "Core/Expected.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/LetterModel.h"
#include "UI/OldControls.h"
#include "UI/OldEditLines.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowSteps.h"
#include "UI/OldWindows.h"
#include "UI/SprintfFormat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La vtable de las ventanas de los casos 1, 2, 6 y 7 de 0x48ee60 (CL9 hito 10): el chat con un amigo (0x6b9dc8), leer
// una carta (0x6b9c58) y escribirla (0x6b9bfc); la pestaña de las cartas (0x4a11b0) y los botones 3 y 4 de la de los
// amigos (0x49d0b0). Los dibujos (0x493fc0, 0x499d60, 0x498480 y los renglones de F5, 0x4a5750) están en
// UI/LetterWindowsDraw, y el de la pestaña de las cartas (0x4a0a30), en UI/FriendWindowsDraw; el del retrato (0x497c10)
// es de Scenes (P-C212).
namespace Client::UI
{
namespace
{
// Los mensajes de la cola (06 § Las ventanas viejas).
constexpr std::int32_t RaisedType = 6;
constexpr std::int32_t EnterType = 7;
constexpr std::int32_t PlaceType = 8;
constexpr std::int32_t SizeType = 9;
constexpr std::int32_t CommandType = 10;
constexpr std::int32_t AnswerType = 12;
constexpr std::int32_t ChosenRowType = 13;
constexpr std::int32_t ToTheEndType = 14;
// Los estados de 0x45a9b0.
constexpr std::int32_t HiddenState = 3;
// 0x41ed10.
constexpr std::int32_t ButtonSound = 0x19;

// Text.bmd.
constexpr std::size_t InviteButtonText = 993;
constexpr std::size_t HideInvitesText = 996;
constexpr std::size_t InviteFullText = 1074;
constexpr std::size_t FilteredText = 570;
constexpr std::size_t ToText = 1000;
constexpr std::array<std::size_t, 4> WriteButtonTexts{1001, 1002, 1003, 1004};
constexpr std::array<std::int32_t, 4> WriteButtonCommands{1, 2, 4, 5};
constexpr std::array<std::int32_t, 4> WriteButtonX{0xc, 0x3f, 0xfa, 0x12d};
constexpr std::size_t NoNameText = 1006;
constexpr std::size_t NoSubjectText = 1007;
constexpr std::size_t NoBodyText = 1008;
constexpr std::size_t DiscardText = 1009;
constexpr std::array<std::size_t, 5> ReadButtonTexts{1010, 1011, 1002, 1012, 1013};
constexpr std::array<std::int32_t, 5> ReadButtonCommands{1, 2, 3, 5, 6};
constexpr std::array<std::int32_t, 5> ReadButtonX{2, 0x35, 0xba, 0x68, 0x91};
constexpr std::array<std::int32_t, 5> ReadButtonWidth{0x32, 0x32, 0x32, 0x28, 0x28};
constexpr std::size_t ReplyText = 1016;
constexpr std::size_t ReallyDeleteLettersText = 1017;
constexpr std::size_t NoneCheckedText = 1031;
constexpr std::size_t WriteTitleText = 1071;
// 0x49d0b0 y 0x4a11b0: sprintf(1071, 1000), el costo de una carta.
constexpr std::int32_t LetterCost = 1000;

// Lo que pone 0x493220 (el chat), 0x4999d0 (leer) y 0x497f50 (escribir).
constexpr std::int32_t WindowPlace = 0x32;
constexpr std::int32_t ChatWidth = 0xfa;
constexpr std::int32_t ChatHeight = 0xaa;
constexpr std::int32_t ChatMinHeight = 0x96;
constexpr std::int32_t ChatEditFilter = 4;
constexpr std::size_t ChatEditLimit = 100;
constexpr std::int32_t ChatInvitesWidth = 0x14a;
// 0x494d1a: el chat que crece no pasa del borde derecho de 640.
constexpr std::int32_t ScreenWidth = 0x280;
// +0x4c de las de las cartas: 0x1b (sin la esquina para el tamaño ni el botón de estirar).
constexpr std::int32_t LetterFlags = 0x1b;
constexpr std::int32_t ReadHeight = 0xb6;
constexpr std::int32_t WriteHeight = 0xd8;
constexpr std::int32_t LetterMinWidth = 0x172;
constexpr std::int32_t PortraitWidth = 0x78;
constexpr std::int32_t ReadPortraitMode = 2;
constexpr std::int32_t WriteMinHeight = 0x96;
constexpr std::size_t ToLimit = 10;
constexpr std::size_t SubjectLimit = 0x20;
constexpr std::size_t BodyLimit = 1000;
// 0x497f50: la acción 0x7a y el ángulo 90 (0x42b40000).
constexpr std::int32_t WritePortraitAction = 0x7a;
constexpr float WritePortraitAngle = 90.0f;
// El 4 de la ventana de escribir y su 5: la acción del retrato, una para atrás y una para adelante (0x497910).
constexpr std::int32_t PreviousAction = -1;
constexpr std::int32_t NextAction = 1;
// 0x4a2070: el alto de los encabezados de la lista de las cartas y el ancho de la marca de todas.
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t LetterMarkColumn = 0xa;
// 0x461600: hasta 3 nombres y después "...".
constexpr std::int32_t TitleNames = 3;
constexpr std::string_view TitleSeparator = ", ";
constexpr std::string_view TitleMore = "...";

Core::Expected<void, std::string> Cut(std::string what)
{
    return Core::Unexpected(std::move(what));
}

std::string TextOf(const OldWindowStepView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

Core::Expected<void, std::string> Send(const OldWindowStepView& view, FriendSend packet, std::string_view where)
{
    if (!view.send)
        return Core::Unexpected(std::string(where) + ": el paquete sin el cable");
    view.send(packet);
    return {};
}

Core::Expected<void, std::string> CallThePortrait(const OldWindowStepView* view, OldPortraitPart& portrait,
                                                  PortraitCallKind call, std::string_view where, std::int32_t delta = 0)
{
    if (view == nullptr || !view->portrait)
        return Cut(std::string(where) + ": el personaje del retrato de las cartas (vtable 0x6ba008) es de Scenes "
                                        "(P-C212)");
    return view->portrait(portrait, call, delta);
}

Core::Expected<std::int32_t, std::string> CreateFromAStep(OldWindowManager& manager, const OldWindowRequest& request,
                                                          const OldWindowGlobals& globals,
                                                          const OldWindowStepView& view, std::string_view where)
{
    if (view.texts == nullptr || view.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": la fábrica de las ventanas viejas sin los textos ni la medida");
    return CreateTheOldWindow(manager, request, *view.texts, *view.measure, &globals, &view);
}

// 0x45aa50 sobre un EDIT o una lista: el mensaje, y su vtable + 0x28 (0x45a640 o 0x47a9d0) y 0x45abc0.
void PostToTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t type)
{
    edit.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!edit.base.queue.empty())
    {
        edit.base.current = edit.base.queue.front();
        edit.base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, edit.base);
    }
}

// 0x45aa50 sobre el retrato: su vtable + 0x28 (0x45a640) da 0 y queda 0x45abc0; la + 0x1c (0x403160) no hace nada.
void PostToTheBase(OldWindowManager& manager, OldWindowBase& base, std::int32_t type)
{
    base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, base);
    }
}

// +0x48 en 2 (0x497aa0): el retrato que se gira con el mouse.
constexpr std::int32_t TurnedPortrait = 2;
// 0x497a00: el ícono de la ayuda, 16 × 16 en (x + 1; y + alto − 0x11).
constexpr std::int32_t HelpIconSide = 0x10;
constexpr std::int32_t HelpIconBottom = 0x11;
// 0x6b89e4, 0x6b8830 y 0x6b8a38: la rueda por 0,02 y el zoom entre 0,8 y 1,1 (0x3f4ccccd y 0x3f8ccccd).
constexpr float WheelZoomStep = 0.02f;
constexpr float NearestZoom = 1.1f;
constexpr float FarthestZoom = 0.8f;

// 0x497a71..0x497b4b: el clic, el clic derecho o la rueda sobre el retrato.
void HandleThePortraitPointer(OldPortraitPart& portrait, const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = portrait.base;
    if (globals.leftClick)
    {
        portrait.help = 0;
        if (base.state != ShownWindow || globals.capture != 0)
            return;
        // 0x497a9b..0x497ac0: lo agarra, lo pone a girar (la vtable + 4 con 2), guarda la X y SetCapture (MuRet no
        // tiene la ventana de Windows).
        globals.capture = base.id;
        base.state = TurnedPortrait;
        portrait.dragX = static_cast<float>(view.mouseX);
        return;
    }
    if (globals.rightClick)
    {
        portrait.help = 0;
        portrait.angle = portrait.baseAngle;
        portrait.zoom = portrait.baseZoom;
        return;
    }
    if (globals.wheel == 0)
        return;
    portrait.help = 0;
    // 0x497b01..0x497b41: la cuenta en la x87, guardada en float; los topes se comparan con la de la pila.
    const double zoom =
        static_cast<double>(globals.wheel) * static_cast<double>(WheelZoomStep) + static_cast<double>(portrait.zoom);
    portrait.zoom = static_cast<float>(zoom);
    if (zoom > static_cast<double>(NearestZoom))
        portrait.zoom = NearestZoom;
    else if (zoom < static_cast<double>(FarthestZoom))
        portrait.zoom = FarthestZoom;
    globals.wheel = 0;
}

// 0x497970 (la vtable + 0x20 del retrato) después de la cola y de 0x45aa80: siempre devuelve 1.
Core::Expected<void, std::string> StepThePortraitMouse(OldPortraitPart& portrait, const OldWindowGlobals& globals,
                                                       const OldWindowStepView& view, std::string_view where)
{
    OldWindowBase& base = portrait.base;
    // 0x497973..0x4979a0: en blanco, solo el mouse sobre la UI.
    if (portrait.blank == 1)
    {
        if (Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
            globals.mouseOnUi = 1;
        return {};
    }
    // 0x4979a7..0x4979bd: siguiendo al héroe, con 0x78abaac en 1, la vtable + 0x3c.
    if (portrait.follow == 1 && view.field78abaac == 1)
    {
        if (auto copied = CallThePortrait(&view, portrait, PortraitCallKind::CopyTheHero, where); !copied.HasValue())
            return copied;
    }
    // 0x4979c0..0x4979cf.
    if (auto stepped = CallThePortrait(&view, portrait, PortraitCallKind::Step, where); !stepped.HasValue())
        return stepped;
    if ((base.flags & 1) == 0)
    {
        // 0x497bbe..0x497bfc.
        if (base.state == ShownWindow && Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
        {
            globals.mouseOnUi = 1;
            if (globals.leftClick)
            {
                globals.leftClick = false;
                globals.leftHeld = false;
            }
        }
        return {};
    }
    // 0x4979df..0x497a44: a la vista, el ícono de la ayuda la prende o la apaga con el clic.
    const bool onTheIcon =
        base.state == ShownWindow &&
        Steps::MouseIn(view, base.x + 1, base.y + base.height - HelpIconBottom, HelpIconSide, HelpIconSide, 1);
    if (onTheIcon)
    {
        globals.mouseOnUi = 1;
        if (globals.leftClick)
        {
            portrait.help = (portrait.help + 1) % 2;
            globals.leftClick = false;
            globals.leftHeld = false;
        }
    }
    else if (Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
    {
        // 0x497a49..0x497b4b.
        globals.mouseOnUi = 1;
        HandleThePortraitPointer(portrait, globals, view);
    }
    // 0x497b51..0x497bb6: girándolo, el clic lo gira con lo que corrió el mouse; sin clic, lo suelta.
    if (base.state != TurnedPortrait)
        return {};
    if (globals.leftClick)
    {
        globals.mouseOnUi = 1;
        const double mouseX = static_cast<double>(view.mouseX);
        portrait.angle =
            static_cast<float>(mouseX - static_cast<double>(portrait.dragX) + static_cast<double>(portrait.angle));
        portrait.dragX = static_cast<float>(view.mouseX);
        return {};
    }
    base.state = ShownWindow;
    if (globals.capture == base.id)
        globals.capture = 0;
    return {};
}

// 0x45aa80 sobre el retrato (la vtable + 0x18): la cola (0x45a640 da 0 y atiende 0x45abc0), la vtable + 0x1c
// (0x403160, nada), el mouse y la vtable + 0x20 (0x497970).
Core::Expected<void, std::string> StepThePortrait(OldWindowManager& manager, OldPortraitPart& portrait,
                                                  std::int32_t arg, const OldWindowGlobals& globals,
                                                  const OldWindowStepView& view, std::string_view where)
{
    OldWindowBase& base = portrait.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        Steps::PlaceAgainstTheParent(manager, base);
    }
    if (arg == 1)
        return {};
    if (!Steps::ClaimTheMouse(manager, base, globals, view))
        return {};
    return StepThePortraitMouse(portrait, globals, view, where);
}

void DrainTheList(OldWindowManager& manager, OldList& list, std::size_t size)
{
    while (!list.base.queue.empty())
    {
        list.base.current = list.base.queue.front();
        list.base.queue.pop_front();
        HandleTheListMessage(list, size);
        Steps::PlaceAgainstTheParent(manager, list.base);
    }
}

void PostToTheList(OldWindowManager& manager, OldList& list, std::size_t size, std::int32_t type)
{
    list.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    DrainTheList(manager, list, size);
}

// 0x4682a0(0), la vtable + 0x30 del EDIT.
void FocusTheEdit(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals)
{
    globals.keyboardFocus = ChatFocus::OldWindow;
    manager.focusedEdit = edit.base.id;
    globals.focusedList = edit.base.id;
}

// 0x45aa80 sobre una lista de una de estas ventanas: la cola y el mouse sobre ella. Los renglones (la vtable + 0x88)
// de las listas del chat y de la carta no hacen nada con el mouse que MuRet necesite: la elección es de 0x47b0f0.
void StepAList(OldWindowManager& manager, OldList& list, std::size_t size, std::int32_t arg,
               const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    DrainTheList(manager, list, size);
    if (arg == 1)
        return;
    if (!Steps::ClaimTheMouse(manager, list.base, globals, view))
        return;
    const OldListGlobals listGlobals{.leftClick = globals.leftClick,
                                     .wheel = globals.wheel,
                                     .capture = globals.capture,
                                     .focusedList = globals.focusedList,
                                     .mouseOnUi = globals.mouseOnUi,
                                     .sound = globals.sound,
                                     .drawn = manager.drawn == 1,
                                     .modal = manager.modal};
    const OldListView listView{.mouseX = view.mouseX, .mouseY = view.mouseY, .keyDown = [&view](std::uint8_t key) {
                                   return view.keyDown && view.keyDown(key);
                               }};
    static_cast<void>(StepTheListMouse(list, size, listGlobals, listView, [](std::int32_t, std::size_t) { return 1; }));
}

OldList MakeTheList(OldWindowManager& manager)
{
    OldList list;
    list.base = Steps::MakeTheBase(manager);
    list.base.rectMode = 2;
    return list;
}

OldEdit MakeTheEdit(OldWindowManager& manager, std::size_t limit)
{
    OldEdit edit;
    edit.base = Steps::MakeTheBase(manager);
    edit.text = TextField(limit, false, TextField::Charset::Latin1);
    return edit;
}

void PlaceTheBase(OldWindowBase& base, std::int32_t parent, std::int32_t mode, std::int32_t x, std::int32_t y)
{
    base.parent = parent;
    base.placeMode = mode;
    base.placeX = x;
    base.placeY = y;
}

void SizeTheBase(OldWindowBase& base, std::int32_t mode, std::int32_t x, std::int32_t y)
{
    base.sizeMode = mode;
    base.sizeX = x;
    base.sizeY = y;
}

void SetUpTheButton(OldButton& button, std::int32_t parent, std::int32_t command, std::string label, std::int32_t mode,
                    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    button.command = command;
    button.label = std::move(label);
    PlaceTheBase(button.base, parent, mode, x, y);
    button.base.width = width;
    button.base.height = height;
}

// 0x461600: "<994>" y, de atrás para adelante, los miembros que no son el héroe separados con ", "; al tercero, "...",
// aunque no haya más (Q-C1004).
std::string ChatTitle(const OldWindow& chat, const OldWindowTexts& texts, std::string_view hero)
{
    std::string title = texts.At(ChatTitleText);
    std::int32_t names = 0;
    for (auto it = chat.chat.members.rbegin(); it != chat.chat.members.rend(); ++it)
    {
        if (SameFriendName(it->name, hero))
            continue;
        if (names > 0)
            title += TitleSeparator;
        title += FriendName(it->name);
        ++names;
        if (names >= TitleNames)
        {
            title += TitleMore;
            break;
        }
    }
    return title;
}

// La vtable + 0x10 de la lista de renglones (+0x2a0): ancha con menos de 3 miembros, o más angosta (por la de los
// miembros) con 3 o más; con la de invitar a la vista, 0xa0 menos (0x493c90, 0x493e90 y 0x4944c0).
void SizeTheLines(OldWindow& chat, bool withMembers)
{
    const std::int32_t cut = chat.chat.mode < 2 ? (withMembers ? -0x50 : 0) : -0xa0;
    SizeTheBase(chat.chat.lines.base, 3, cut, -0x10);
}

const LetterEntry* SelectedLetter(OldWindow& tab)
{
    const std::optional<std::size_t> at = SelectedInTheList(tab.tab.list, tab.tab.letters.size());
    if (!at || *at >= tab.tab.letters.size())
        return nullptr;
    return &tab.tab.letters[*at];
}

// 0x48ee60(7, 100, 100, sprintf(1071, 1000), 0, 0).
Core::Expected<std::int32_t, std::string> OpenTheWriteWindow(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view, std::string_view where)
{
    const std::array<SprintfArgument, 1> cost{SprintfArgument(LetterCost)};
    const std::string title = FormatLikeSprintf(TextOf(view, WriteTitleText), cost);
    constexpr std::int32_t WritePlace = 100;
    return CreateFromAStep(
        manager,
        OldWindowRequest{
            .kind = LetterWriteCase, .x = WritePlace, .y = WritePlace, .data = title, .aux = 0, .flags = 0},
        globals, view, where);
}

// 0x4a177a: con la ventana armada, el 8 y el 9 con 0x45aa50.
Core::Expected<void, std::string> PlaceTheNewWindow(OldWindowManager& manager, std::int32_t id,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindow* window = FindTheOldWindow(manager, id);
    if (window == nullptr)
        return {};
    if (auto placed = Steps::PostAndStep(manager, *window, PlaceType, globals, view); !placed.HasValue())
        return placed;
    return Steps::PostAndStep(manager, *window, SizeType, globals, view);
}

// 0x4983e0 y 0x498420: el nombre (o el asunto) al EDIT, el foco al que sigue y +0x77c.
void PutTheName(OldWindowManager& manager, OldWindow& write, std::string_view name, const OldWindowGlobals& globals)
{
    write.letter.to.text.SetText(name);
    FocusTheEdit(manager, write.letter.subject, globals);
    write.letter.prepared = 1;
}

void PutTheSubject(OldWindowManager& manager, OldWindow& write, std::string_view subject,
                   const OldWindowGlobals& globals)
{
    write.letter.subject.text.SetText(subject);
    FocusTheEdit(manager, write.letter.text, globals);
    write.letter.prepared = 2;
}

// 0x498820 con el 10 y 1 (0x4988b2..0x499590): los tres EDIT, el filtro, el cuerpo con "\r\n" pasado a "\n", y el C5.
Core::Expected<void, std::string> SendTheLetter(OldWindowManager& manager, OldWindow& write,
                                                const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldLetterPart& letter = write.letter;
    if (letter.sending != 0)
        return {};
    std::string to(letter.to.text.Value().substr(0, ToLimit));
    std::string subject(letter.subject.text.Value().substr(0, SubjectLimit));
    std::string body(letter.text.text.Value().substr(0, 0x3ff));
    // 0x498948 y 0x498982: 0x622b40 con 0 sobre el asunto y el cuerpo; si salta, el texto 570.
    if (view.badWord && view.badWord(subject))
        subject = TextOf(view, FilteredText);
    if (view.badWord && view.badWord(body))
        body = TextOf(view, FilteredText);
    // 0x4989b6..0x498a10: sin nombre, sin asunto o sin cuerpo, la caja modal (8) y el foco a ese EDIT.
    const auto missing = [&](std::size_t text, OldEdit& edit, std::int32_t field) -> Core::Expected<void, std::string>
    {
        auto created = CreateFromAStep(
            manager,
            OldWindowRequest{
                .kind = ModalNoticeCase, .x = -1, .y = -1, .data = TextOf(view, text), .aux = 0, .flags = 0},
            globals, view, "0x498820(1)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        FocusTheEdit(manager, edit, globals);
        letter.prepared = field;
        return {};
    };
    if (to.empty())
        return missing(NoNameText, letter.to, 0);
    if (subject.empty())
        return missing(NoSubjectText, letter.subject, 1);
    if (body.empty())
        return missing(NoBodyText, letter.text, 2);
    // 0x498a30..0x498a99: '\r' sale como '\n' y se come el que sigue; "\r\n\r\n" deja un espacio en medio (Q-C1003).
    // Corta con más de 1000 de cada lado y copia hasta el '\0' inclusive.
    std::string converted;
    std::size_t from = 0;
    std::size_t into = 0;
    while (into <= BodyLimit && from <= BodyLimit && from <= body.size())
    {
        const char at = from < body.size() ? body[from] : '\0';
        if (at == '\r')
        {
            converted.resize(into + 1);
            converted[into] = '\n';
            const char next = from + 1 < body.size() ? body[from + 1] : '\0';
            if (next == '\n' && from + 3 < body.size() && body[from + 2] == '\r' && body[from + 3] == '\n')
            {
                ++into;
                converted.resize(into + 1);
                converted[into] = ' ';
            }
            ++from;
        }
        else
        {
            converted.resize(into + 1);
            converted[into] = at;
        }
        ++from;
        ++into;
    }
    converted = converted.substr(0, converted.find('\0'));
    if (converted.size() > BodyLimit)
        converted.resize(BodyLimit);
    letter.sending = 1;
    // 0x498ad0..0x498b27: la dirección y la acción del retrato.
    constexpr float AngleStep = 1.0f / 6.0f;
    constexpr float ZoomScale = 100.0f;
    constexpr float ZoomBase = 80.0f;
    constexpr float ZoomStep = 5.0f;
    constexpr float ZoomUnit = 0.1f;
    constexpr std::int32_t ActionBase = 0x78;
    const auto angle = static_cast<std::int32_t>(letter.portrait.angle * AngleStep);
    const auto zoom = static_cast<std::int32_t>(((letter.portrait.zoom * ZoomScale) - ZoomBase + ZoomStep) * ZoomUnit);
    const auto direction = static_cast<std::uint8_t>((angle & 0x3f) | (zoom << 6));
    const auto action = static_cast<std::uint8_t>(letter.portrait.action - ActionBase);
    return Send(view,
                FriendSend{.kind = FriendSendKind::LetterSend,
                           .value = direction,
                           .name = std::move(to),
                           .index = 0,
                           .room = 0,
                           .window = write.base.id,
                           .action = action,
                           .subject = std::move(subject),
                           .body = std::move(converted)},
                "0x498820(1)");
}

// 0x499840, la vtable + 0x4c de la de escribir: sin asunto ni cuerpo, 1 (se cierra); si no, la caja 5 con el 1009.
Core::Expected<bool, std::string> MayCloseTheLetter(OldWindowManager& manager, OldWindow& write,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    constexpr std::size_t Peek = 0xf;
    if (write.letter.subject.text.Value().substr(0, Peek).empty() &&
        write.letter.text.text.Value().substr(0, Peek).empty())
        return true;
    auto created = CreateFromAStep(manager,
                                   OldWindowRequest{.kind = ModalQuestionCase,
                                                    .x = -1,
                                                    .y = -1,
                                                    .data = TextOf(view, DiscardText),
                                                    .aux = write.base.id,
                                                    .flags = 0},
                                   globals, view, "0x499840");
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    return false;
}

// 0x498820, la vtable + 0x28 de la de escribir. Da 0.
Core::Expected<void, std::string> HandleTheWriteMessage(OldWindowManager& manager, OldWindow& write,
                                                        const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    const OldMessage& message = write.base.current;
    OldLetterPart& letter = write.letter;
    if (message.type == RaisedType)
    {
        // 0x4996e3: el foco al EDIT de +0x77c, que vuelve a 0 (Q-C1006).
        OldEdit& edit = letter.prepared == 2 ? letter.text : letter.prepared == 1 ? letter.subject : letter.to;
        if (letter.prepared == 0 || letter.prepared == 1 || letter.prepared == 2)
        {
            FocusTheEdit(manager, edit, globals);
            letter.prepared = 0;
        }
        return {};
    }
    if (message.type == AnswerType)
    {
        if (message.b == 1)
            PostToTheManager(manager, CloseMessage, write.base.id, 0);
        return {};
    }
    if (message.type != CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
        return SendTheLetter(manager, write, globals, view);
    case 2:
    {
        auto may = MayCloseTheLetter(manager, write, globals, view);
        if (!may.HasValue())
            return Core::Unexpected(std::move(may).Error());
        if (may.Value())
            PostToTheManager(manager, CloseMessage, write.base.id, 0);
        return {};
    }
    case 4:
        return CallThePortrait(&view, letter.portrait, PortraitCallKind::TurnAction, "0x49969b", PreviousAction);
    case 5:
        return CallThePortrait(&view, letter.portrait, PortraitCallKind::TurnAction, "0x4996bf", NextAction);
    default:
        return {};
    }
}

// 0x4944c0 con el 7: el texto del EDIT, el filtro y, distinto del último y no vacío, el mensaje por la conexión de la
// sala; el EDIT vacío y, con menos de 2 miembros, 0x495630(1).
Core::Expected<void, std::string> SayInTheChat(OldWindowManager& manager, OldWindow& chat,
                                               const OldWindowStepView& view)
{
    OldChatPart& part = chat.chat;
    std::string text(part.edit.text.Value());
    if (view.badWord && view.badWord(text))
        text = TextOf(view, FilteredText);
    if (text.substr(0, 0xff) != std::string_view(part.lastText).substr(0, 0xff))
    {
        part.lastText = text.substr(0, 0xff);
        // 0x493510: sin la conexión de la sala (0x49e420 da 0), no manda nada; con ella, el C1 04 (Scenes lo saca).
        if (!text.empty() && FindTheChatRoom(manager.chatRooms, part.room) != nullptr)
            manager.chatRooms.orders.push_back(
                ChatRoomOrder{.kind = ChatRoomOrder::Kind::Message, .room = part.room, .text = text});
    }
    part.edit.text.Clear();
    if (part.members.size() < 2)
    {
        if (view.texts == nullptr)
            return Cut("0x495630(1): el título del chat sin los textos");
        CloseTheChat(chat, 1, *view.texts);
    }
    return {};
}

// 0x4944c0 con el 10 y 1: muestra u oculta la lista de invitar.
Core::Expected<void, std::string> ToggleTheInvites(OldWindowManager& manager, OldWindow& chat,
                                                   const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldChatPart& part = chat.chat;
    OldFrame& frame = chat.frame;
    if (part.mode == 1)
    {
        part.mode = 2;
        chat.base.width += 0x50;
        frame.minWidth = ChatInvitesWidth;
        frame.minHeight = ChatMinHeight;
        frame.maxWidth = 0;
        frame.maxHeight = 0;
        SizeTheBase(part.lines.base, 3, -0xa0, -0x10);
        part.users.base.placeX = 0x9b;
        part.inviteButton.base.placeX = 0x86;
        PostToTheList(manager, part.lines, part.lineRows.size(), SizeType);
        PostToTheList(manager, part.lines, part.lineRows.size(), PlaceType);
        PostToTheList(manager, part.users, part.members.size(), PlaceType);
        part.inviteButton.label = TextOf(view, HideInvitesText);
        FillTheInvites(manager, chat, view.heroName);
        if (chat.base.width + chat.base.x > ScreenWidth)
            chat.base.x = ScreenWidth - chat.base.width;
        return Steps::SettleTheWindow(manager, chat, globals, view);
    }
    if (part.mode > 1)
    {
        part.mode = 1;
        chat.base.width -= 0x50;
        frame.minWidth = ChatWidth;
        frame.minHeight = ChatMinHeight;
        frame.maxWidth = 0;
        frame.maxHeight = 0;
        SizeTheLines(chat, part.members.size() >= 3);
        part.users.base.placeX = 0x4b;
        part.inviteButton.base.placeX = 0x36;
        PostToTheList(manager, part.lines, part.lineRows.size(), SizeType);
        PostToTheList(manager, part.lines, part.lineRows.size(), PlaceType);
        PostToTheList(manager, part.users, part.members.size(), PlaceType);
        part.inviteButton.label = TextOf(view, InviteButtonText);
    }
    return {};
}

// 0x4944c0 con el 10 y 2: el elegido de la lista de invitar, con 2..0x1d miembros, al C1 CB.
Core::Expected<void, std::string> InviteTheChosen(OldWindow& chat, const OldWindowStepView& view)
{
    OldChatPart& part = chat.chat;
    const std::optional<std::size_t> at = SelectedInTheList(part.invite, part.invitable.size());
    if (!at || *at >= part.invitable.size())
        return {};
    const auto members = static_cast<std::int32_t>(part.members.size());
    if (members <= 1)
        return {};
    constexpr std::int32_t MostMembers = 0x1d;
    if (members > MostMembers)
    {
        AddAChatLine(chat, 0xff, TextOf(view, InviteFullText), 1, 0);
        return {};
    }
    return Send(view,
                FriendSend{.kind = FriendSendKind::Invite,
                           .value = 0,
                           .name = FriendName(part.invitable[*at].name),
                           .index = 0,
                           .room = static_cast<std::uint16_t>(part.room),
                           .window = chat.base.id,
                           .action = 0,
                           .subject = {},
                           .body = {}},
                "0x4944c0(10, 2)");
}

// 0x4944c0, la vtable + 0x28 del chat. Da 0.
Core::Expected<void, std::string> HandleTheChatMessage(OldWindowManager& manager, OldWindow& chat,
                                                       const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldMessage& message = chat.base.current;
    // 0x4944d9: el doble clic en la lista de invitar es el 10 con 2.
    if (message.type == ChosenRowType && message.a == chat.chat.invite.base.id)
    {
        globals.sound = ButtonSound;
        message.type = CommandType;
        message.a = 2;
    }
    if (message.type == RaisedType)
    {
        FocusTheEdit(manager, chat.chat.edit, globals);
        globals.focusedList = chat.chat.users.base.id;
        return {};
    }
    if (message.type == EnterType)
        return SayInTheChat(manager, chat, view);
    if (message.type != CommandType || manager.modal != 0)
        return {};
    if (message.a == 1)
        return ToggleTheInvites(manager, chat, globals, view);
    // 0x4944c0 con el 2: con el EDIT sin el foco (la vtable + 0x5c da 0).
    if (message.a == 2 &&
        !(globals.keyboardFocus == ChatFocus::OldWindow && manager.focusedEdit == chat.chat.edit.base.id))
        return InviteTheChosen(chat, view);
    return {};
}

// 0x4a09d0: la marca de todas las de la copia (la vtable + 0x48 de la lista, 0x47f200) y del modelo (0x4a0270), y
// +0x414.
void CheckAllTheLetters(OldWindowManager& manager, OldWindow& tab, std::int32_t checked)
{
    for (LetterEntry& letter : tab.tab.letters)
        letter.selected = checked;
    for (LetterEntry& letter : manager.letterModel.letters)
        letter.selected = checked;
    tab.tab.allChecked = checked;
}

// La pestaña de las cartas que está en la de amigos.
OldWindow* TheLettersTab(OldWindowManager& manager)
{
    OldWindow* friends = FindTheOldWindow(manager, manager.friends);
    if (friends == nullptr || friends->tabs.size() != FriendsTabCount)
        return nullptr;
    return &friends->tabs[LettersTabNumber];
}

// 0x491570: la carta en el mapa de las ventanas de leer (+0x94 de [0x5763514]) con la ventana en 0, si no estaba; da
// si ya estaba.
bool ReserveTheLetterWindow(OldWindowManager& manager, std::uint16_t index)
{
    return !manager.letterWindows.try_emplace(index, 0).second;
}

// 0x491780: la ventana de leer anotada para la carta, o 0.
std::int32_t LetterWindowOf(const OldWindowManager& manager, std::uint16_t index)
{
    const auto found = manager.letterWindows.find(index);
    return found != manager.letterWindows.end() ? found->second : 0;
}

// 0x4a17b9 y 0x49a839: 0x491570 la anota (con la ventana en 0) si no estaba; si estaba, 0x491780 da su ventana, que
// sube (con 0, nada: el C7 todavía no llegó). Si no, la copia de 0x4a04a0 vuelve a 0x4dcb70 o sale el C1 06 C7.
Core::Expected<void, std::string> ReadTheLetter(OldWindowManager& manager, std::uint16_t index,
                                                const OldWindowStepView& view, std::string_view where)
{
    if (ReserveTheLetterWindow(manager, index))
    {
        if (const std::int32_t open = LetterWindowOf(manager, index); open != 0)
            PostToTheManager(manager, RaiseMessage, open, 0);
        return {};
    }
    if (const std::vector<std::byte>* kept = KeptLetter(manager.letterModel, index); kept != nullptr)
    {
        if (!view.replayLetter)
            return Cut(std::string(where) + ": 0x4dcb70 sobre la carta guardada sin el juego");
        const std::vector<std::byte> copy = *kept;
        return view.replayLetter(copy);
    }
    return Send(view,
                FriendSend{.kind = FriendSendKind::LetterRead,
                           .value = 0,
                           .name = {},
                           .index = index,
                           .room = 0,
                           .window = 0,
                           .action = 0,
                           .subject = {},
                           .body = {}},
                where);
}

// 0x4a0600: el lugar de la carta en el modelo, desde 1; 0 si no está.
std::int32_t PlaceOfTheLetter(const LetterModel& model, std::uint16_t index)
{
    std::int32_t place = 0;
    for (const LetterEntry& letter : model.letters)
    {
        ++place;
        if (letter.index == index)
            return place;
    }
    return 0;
}

// 0x49ff50 y 0x4a0070: el número de la carta que sigue a (o que está antes de) la `index` en el modelo; 0 si no está
// o no hay otra. Una carta con el número 0 cuenta como ninguna (Q-C1007).
std::uint16_t NeighbourOfTheLetter(const LetterModel& model, std::uint16_t index, bool next)
{
    const auto found = std::ranges::find(model.letters, index, &LetterEntry::index);
    if (found == model.letters.end())
        return 0;
    if (next)
    {
        const auto after = std::next(found);
        return after != model.letters.end() ? after->index : std::uint16_t{0};
    }
    return found != model.letters.begin() ? std::prev(found)->index : std::uint16_t{0};
}

// 0x4a0a10 con 0x4a0600 sobre la pestaña de las cartas, si la de amigos está: la lista elige esa carta (la vtable +
// 0x58, 0x47f5b0, como 0x47aef0).
void ChooseTheLetterInTheTab(OldWindowManager& manager, std::uint16_t index)
{
    OldWindow* tab = TheLettersTab(manager);
    if (tab == nullptr)
        return;
    SelectInTheList(tab->tab.list, tab->tab.letters.size(), PlaceOfTheLetter(manager.letterModel, index));
}

// 0x49a7a4 y 0x49ac6c, el 10 con 5 y con 6 de la de leer: con otra carta, la elige en la pestaña, se cierra, suelta la
// suya (0x4916b0), deja su lugar a la que viene (0x5762f0c y 0x5762f10) y lee la otra; sin otra, la pestaña elige la
// suya.
Core::Expected<void, std::string> ReadTheNeighbour(OldWindowManager& manager, OldWindow& read, bool next,
                                                   const OldWindowStepView& view, std::string_view where)
{
    const std::uint16_t index = read.letter.entry.index;
    const std::uint16_t other = NeighbourOfTheLetter(manager.letterModel, index, next);
    if (other == 0)
    {
        ChooseTheLetterInTheTab(manager, index);
        return {};
    }
    ChooseTheLetterInTheTab(manager, other);
    PostToTheManager(manager, CloseMessage, read.base.id, 0);
    manager.letterWindows.erase(index);
    manager.nextReadX = read.base.x;
    manager.nextReadY = read.base.y;
    return ReadTheLetter(manager, other, view, where);
}

// 0x49a1f0, la vtable + 0x28 de la de leer. Da 0.
Core::Expected<void, std::string> HandleTheReadMessage(OldWindowManager& manager, OldWindow& read,
                                                       const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    const OldMessage& message = read.base.current;
    const LetterEntry& entry = read.letter.entry;
    if (message.type == AnswerType)
    {
        // 0x49a22b: el sí de la pregunta del 1017: C1 06 C8 con la carta (0x49a3ab) y el 4 a sí misma.
        if (message.b != 1)
            return {};
        if (auto sent = Send(view,
                             FriendSend{.kind = FriendSendKind::LetterDelete,
                                        .value = 0,
                                        .name = {},
                                        .index = entry.index,
                                        .room = 0,
                                        .window = 0,
                                        .action = 0,
                                        .subject = {},
                                        .body = {}},
                             "0x49a1f0(12)");
            !sent.HasValue())
            return sent;
        PostToTheManager(manager, CloseMessage, read.base.id, 0);
        return {};
    }
    if (message.type != CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
    {
        // 0x49a625: responder: la de escribir (sin el 8 y el 9) con el remitente y sprintf(1016, asunto) cortado en
        // 0x20 por 0x459e30.
        auto opened = OpenTheWriteWindow(manager, globals, view, "0x49a1f0(1)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        OldWindow* write = FindTheOldWindow(manager, opened.Value());
        if (write == nullptr)
            return {};
        PutTheName(manager, *write, entry.name, globals);
        const std::array<SprintfArgument, 1> subject{SprintfArgument(std::string_view(entry.subject))};
        const std::string reply = FormatLikeSprintf(TextOf(view, ReplyText), subject);
        PutTheSubject(manager, *write, std::string_view(reply).substr(0, SubjectLimit), globals);
        return {};
    }
    case 2:
    {
        // 0x49a702: la pregunta del 1017, con ella misma de dueña.
        auto created = CreateFromAStep(manager,
                                       OldWindowRequest{.kind = ModalQuestionCase,
                                                        .x = -1,
                                                        .y = -1,
                                                        .data = TextOf(view, ReallyDeleteLettersText),
                                                        .aux = read.base.id,
                                                        .flags = 0},
                                       globals, view, "0x49a1f0(2)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return {};
    }
    case 3:
        PostToTheManager(manager, CloseMessage, read.base.id, 0);
        return {};
    case 5:
        return ReadTheNeighbour(manager, read, true, view, "0x49a7a4");
    case 6:
        return ReadTheNeighbour(manager, read, false, view, "0x49ac6c");
    default:
        return {};
    }
}
} // namespace

namespace Steps
{
bool IsALetterKind(OldWindowKind kind)
{
    return kind == OldWindowKind::ChatWindow || kind == OldWindowKind::LetterReadWindow ||
           kind == OldWindowKind::LetterWriteWindow;
}

Core::Expected<OldWindow, std::string> MakeTheLetterKind(OldWindowManager& manager, const OldWindowRequest& request,
                                                         const OldWindowTexts& texts, const OldWindowMeasure& measure,
                                                         const OldWindowStepView* view)
{
    OldWindow window = MakeTheFrame(manager);
    OldWindowBase& base = window.base;
    const std::int32_t id = base.id;
    if (request.kind == ChatCase || request.kind == CalledChatCase)
    {
        // 0x48eee4: el marco, el EDIT, los renglones, los miembros, un botón, los de invitar y otro botón; +0xb0 en 1.
        window.kind = OldWindowKind::ChatWindow;
        OldChatPart& chat = window.chat;
        chat.edit = MakeTheEdit(manager, ChatEditLimit);
        chat.lines = MakeTheList(manager);
        chat.users = MakeTheList(manager);
        chat.inviteButton = MakeTheButton(manager);
        chat.invite = MakeTheList(manager);
        chat.sendButton = MakeTheButton(manager);
        chat.mode = 1;
        // 0x493220.
        chat.lastText.clear();
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = WindowPlace;
        base.y = WindowPlace;
        base.width = ChatWidth;
        base.height = ChatHeight;
        window.frame.minWidth = ChatWidth;
        window.frame.minHeight = ChatMinHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        chat.edit.base.width = 0xb4;
        chat.edit.base.height = 0xe;
        chat.edit.filter = ChatEditFilter;
        PlaceTheBase(chat.edit.base, id, 2, 2, 0xc);
        chat.edit.base.state = 0;
        PlaceTheBase(chat.lines.base, id, 2, 0, 0x10);
        SizeTheBase(chat.lines.base, 3, 0, -0x10);
        PlaceTheBase(chat.users.base, id, 3, 0x4b, 0x10);
        SizeTheBase(chat.users.base, 2, 0x4b, -0x10);
        PlaceTheBase(chat.invite.base, id, 3, 0x4b, 0x10);
        SizeTheBase(chat.invite.base, 2, 0x4b, -0x10);
        SetUpTheButton(chat.inviteButton, id, 1, texts.At(InviteButtonText), 3, 0x36, 0xe, 0x35, 0xd);
        SetUpTheButton(chat.sendButton, id, 2, texts.At(InviteButtonText), 3, 0x4a, 0xe, 0x49, 0xd);
        chat.aux = 0;
        return window;
    }
    OldLetterPart& letter = window.letter;
    letter.portrait.base = MakeTheBase(manager);
    if (request.kind == LetterReadCase)
    {
        // 0x48f41f: el marco, el retrato (+0xb0), +0x774 en 2, la lista del cuerpo (+0x8b0) y cinco botones.
        window.kind = OldWindowKind::LetterReadWindow;
        letter.portraitMode = ReadPortraitMode;
        letter.lines = MakeTheList(manager);
        for (std::size_t at = 0; at < ReadButtonTexts.size(); ++at)
            letter.buttons.push_back(MakeTheButton(manager));
        // 0x4999d0.
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = WindowPlace;
        base.y = WindowPlace;
        base.width = ChatWidth;
        base.height = ReadHeight;
        window.frame.minWidth = ChatWidth;
        window.frame.minHeight = ReadHeight;
        base.flags = LetterFlags;
        PlaceTheBase(letter.lines.base, id, 2, 0, 0x14);
        SizeTheBase(letter.lines.base, 3, 0, -0x24);
        for (std::size_t at = 0; at < letter.buttons.size(); ++at)
            SetUpTheButton(letter.buttons[at], id, ReadButtonCommands[at], texts.At(ReadButtonTexts[at]), 2,
                           ReadButtonX[at], 0x10, ReadButtonWidth[at], 0xe);
        if (auto built = CallThePortrait(view, letter.portrait, PortraitCallKind::Build, "0x4999d0"); !built.HasValue())
            return Core::Unexpected(std::move(built).Error());
        letter.portrait.base.flags = 1;
        PlaceTheBase(letter.portrait.base, id, 1, 0x77, 0xf);
        SizeTheBase(letter.portrait.base, 2, 0x77, -0xf);
        SizeTheBase(letter.lines.base, 3, -PortraitWidth, -0x24);
        base.width += PortraitWidth;
        window.frame.minWidth = LetterMinWidth;
        window.frame.minHeight = ReadHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        return window;
    }
    // 0x48f4ee: el marco, el retrato (+0xb4), los tres EDIT y cuatro botones.
    window.kind = OldWindowKind::LetterWriteWindow;
    letter.to = MakeTheEdit(manager, ToLimit);
    letter.subject = MakeTheEdit(manager, SubjectLimit);
    letter.text = MakeTheEdit(manager, BodyLimit);
    for (std::size_t at = 0; at < WriteButtonTexts.size(); ++at)
        letter.buttons.push_back(MakeTheButton(manager));
    // 0x497f50.
    window.frame.title = std::string(request.data);
    base.parent = request.aux;
    base.x = WindowPlace;
    base.y = WindowPlace;
    base.width = ChatWidth;
    base.height = WriteHeight;
    window.frame.minWidth = ChatWidth;
    window.frame.minHeight = WriteMinHeight;
    base.flags = LetterFlags;
    // GetTextExtentPointA del 1000 con la fuente del DC ([0x57634f4]).
    const std::int32_t toWidth = measure.measure.Measure(measure.dcFont, texts.At(ToText)).width;
    letter.to.base.width = 0xee;
    letter.to.base.height = 0xe;
    PlaceTheBase(letter.to.base, id, 0, toWidth, 3);
    letter.subject.base.width = 0xee;
    letter.subject.base.height = 0xe;
    PlaceTheBase(letter.subject.base, id, 0, toWidth, 0x12);
    // 0x4980ef: la vtable + 0x38 con 1 (+0xc4) antes de 0xee × 0x87 y de crearlo (0x467fe0).
    letter.text.base.width = 0xee;
    letter.text.base.height = 0x87;
    MakeTheEditMultiline(letter.text, measure.measure, measure.dcFont, measure.screen);
    PlaceTheBase(letter.text.base, id, 0, 5, 0x21);
    for (std::size_t at = 0; at < letter.buttons.size(); ++at)
        SetUpTheButton(letter.buttons[at], id, WriteButtonCommands[at], texts.At(WriteButtonTexts[at]), 2,
                       WriteButtonX[at], 0x10, 0x32, 0xe);
    // El EDIT del nombre +0x30(0): el foco, que MuRet le da después de armarla (Steps::FocusTheChatEdit con el 7);
    // +0x77c en 0 y +0x9c en 1.
    letter.prepared = 0;
    window.frame.editField = 1;
    if (auto built = CallThePortrait(view, letter.portrait, PortraitCallKind::Build, "0x497f50"); !built.HasValue())
        return Core::Unexpected(std::move(built).Error());
    letter.portrait.base.flags = 1;
    PlaceTheBase(letter.portrait.base, id, 1, 0x77, 0x1e);
    letter.portrait.base.width = 0x77;
    letter.portrait.base.height = ClientHeight(window) - 0x31;
    if (auto copied = CallThePortrait(view, letter.portrait, PortraitCallKind::CopyTheHero, "0x497f50");
        !copied.HasValue())
        return Core::Unexpected(std::move(copied).Error());
    letter.portrait.follow = 1;
    letter.portrait.action = WritePortraitAction;
    if (auto acted = CallThePortrait(view, letter.portrait, PortraitCallKind::SetAction, "0x497f50"); !acted.HasValue())
        return Core::Unexpected(std::move(acted).Error());
    if (auto turned = SetThePortraitAngle(view, letter.portrait, WritePortraitAngle, "0x497f50"); !turned.HasValue())
        return Core::Unexpected(std::move(turned).Error());
    letter.withPortrait = 1;
    base.width += PortraitWidth;
    window.frame.minWidth = LetterMinWidth;
    window.frame.minHeight = WriteMinHeight;
    window.frame.maxWidth = 0;
    window.frame.maxHeight = 0;
    return window;
}

Core::Expected<void, std::string> HandleTheLetterKindsMessage(OldWindowManager& manager, OldWindow& window,
                                                              const OldWindowGlobals& globals,
                                                              const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::ChatWindow:
        return HandleTheChatMessage(manager, window, globals, view);
    case OldWindowKind::LetterReadWindow:
        return HandleTheReadMessage(manager, window, globals, view);
    case OldWindowKind::LetterWriteWindow:
        return HandleTheWriteMessage(manager, window, globals, view);
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheLetterKindsChildren(OldWindowManager& manager, OldWindow& window,
                                                             std::int32_t arg, const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view)
{
    if (window.kind == OldWindowKind::ChatWindow)
    {
        // 0x495450: con el ancho cambiado desde el paso anterior (+0x774), el EDIT a lo que queda al lado de los
        // botones; después los botones, las listas y el EDIT.
        OldChatPart& chat = window.chat;
        if (chat.aux != 0 && chat.aux != window.base.width)
        {
            std::int32_t width = Steps::ClientWidth(window) + (-7 - chat.inviteButton.base.width);
            if (chat.mode == 2)
                width += -7 - chat.sendButton.base.width;
            chat.edit.base.width = width;
            chat.edit.base.height = 0xe;
        }
        chat.aux = window.base.width;
        Steps::StepTheButton(manager, chat.inviteButton, arg, globals, view);
        StepAList(manager, chat.lines, chat.lineRows.size(), arg, globals, view);
        StepAList(manager, chat.users, chat.members.size(), arg, globals, view);
        StepAList(manager, chat.invite, chat.invitable.size(), arg, globals, view);
        Steps::StepTheEdit(manager, chat.edit, arg, globals, view);
        Steps::StepTheButton(manager, chat.sendButton, arg, globals, view);
        return {};
    }
    OldLetterPart& letter = window.letter;
    if (window.kind == OldWindowKind::LetterReadWindow)
    {
        // 0x49b180: la lista del cuerpo, los cinco botones y el retrato.
        StepAList(manager, letter.lines, letter.lineRows.size(), arg, globals, view);
        for (OldButton& button : letter.buttons)
            Steps::StepTheButton(manager, button, arg, globals, view);
        return StepThePortrait(manager, letter.portrait, arg, globals, view, "0x49b180");
    }
    // 0x499780: los tres EDIT, los dos primeros botones y, con +0xb0 en 1, los otros dos y el retrato.
    Steps::StepTheEdit(manager, letter.to, arg, globals, view);
    Steps::StepTheEdit(manager, letter.subject, arg, globals, view);
    Steps::StepTheEdit(manager, letter.text, arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[0], arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[1], arg, globals, view);
    if (letter.withPortrait != 1)
        return {};
    Steps::StepTheButton(manager, letter.buttons[2], arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[3], arg, globals, view);
    return StepThePortrait(manager, letter.portrait, arg, globals, view, "0x499780");
}

Core::Expected<void, std::string> SettleTheLetterKinds(OldWindowManager& manager, OldWindow& window,
                                                       const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    static_cast<void>(view);
    if (window.kind == OldWindowKind::ChatWindow)
    {
        // 0x493410: el 8 a los seis hijos; el 9 dos veces a los renglones, a los miembros y a los de invitar; el 0xe a
        // los miembros y a los de invitar; +0x9c en 1 y la lista de los miembros a 0x5762ec0.
        OldChatPart& chat = window.chat;
        PostToTheList(manager, chat.lines, chat.lineRows.size(), PlaceType);
        PostToTheList(manager, chat.users, chat.members.size(), PlaceType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), PlaceType);
        Steps::PostAndSettle(manager, chat.inviteButton, PlaceType);
        Steps::PostAndSettle(manager, chat.sendButton, PlaceType);
        PostToTheEdit(manager, chat.edit, PlaceType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
            PostToTheList(manager, chat.lines, chat.lineRows.size(), SizeType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
            PostToTheList(manager, chat.users, chat.members.size(), SizeType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
            PostToTheList(manager, chat.invite, chat.invitable.size(), SizeType);
        PostToTheList(manager, chat.users, chat.members.size(), ToTheEndType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), ToTheEndType);
        window.frame.editField = 1;
        globals.focusedList = chat.users.base.id;
        return {};
    }
    OldLetterPart& letter = window.letter;
    if (window.kind == OldWindowKind::LetterReadWindow)
    {
        // 0x499c00: el 8 a la lista, a los cinco botones y al retrato; el 9 dos veces a la lista y al retrato.
        PostToTheList(manager, letter.lines, letter.lineRows.size(), PlaceType);
        for (OldButton& button : letter.buttons)
            Steps::PostAndSettle(manager, button, PlaceType);
        PostToTheBase(manager, letter.portrait.base, PlaceType);
        PostToTheList(manager, letter.lines, letter.lineRows.size(), SizeType);
        PostToTheList(manager, letter.lines, letter.lineRows.size(), SizeType);
        PostToTheBase(manager, letter.portrait.base, SizeType);
        return {};
    }
    // 0x498350: el 8 a los tres EDIT, a los cuatro botones y al retrato.
    PostToTheEdit(manager, letter.to, PlaceType);
    PostToTheEdit(manager, letter.subject, PlaceType);
    PostToTheEdit(manager, letter.text, PlaceType);
    for (OldButton& button : letter.buttons)
        Steps::PostAndSettle(manager, button, PlaceType);
    PostToTheBase(manager, letter.portrait.base, PlaceType);
    return {};
}

void FocusTheChatEdit(OldWindowManager& manager, OldWindow& window, const OldWindowGlobals& globals)
{
    if (window.kind == OldWindowKind::ChatWindow)
        FocusTheEdit(manager, window.chat.edit, globals);
    if (window.kind == OldWindowKind::LetterWriteWindow)
        FocusTheEdit(manager, window.letter.to, globals);
}

Core::Expected<bool, std::string> MayCloseTheWindow(OldWindowManager& manager, OldWindow& window,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (window.kind != OldWindowKind::LetterWriteWindow)
        return true;
    return MayCloseTheLetter(manager, window, globals, view);
}

Core::Expected<void, std::string> ForgetTheLetterKind(OldWindowManager& manager, const OldWindow& window)
{
    if (window.kind == OldWindowKind::ChatWindow)
        return LeaveTheChatRoom(manager, window);
    // 0x4916b0 con +0x77c: el número de la carta (con los dos bytes de relleno de la plantilla, en 0, *inferido*).
    if (window.kind == OldWindowKind::LetterReadWindow)
        manager.letterWindows.erase(window.letter.entry.index);
    return {};
}

Core::Expected<void, std::string> HandleTheLettersTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                             const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view)
{
    OldMessage& message = tab.base.current;
    // 0x4a11c8: el doble clic sobre una carta es leerla.
    if (message.type == ChosenRowType)
    {
        globals.sound = ButtonSound;
        message.type = CommandType;
        message.a = 2;
    }
    if (message.type == AnswerType)
    {
        // "¿Borrar las marcadas?" con sí: un C1 C8 por cada una (0x4a1349..0x4a17a6).
        if (message.b != 1)
            return {};
        for (const LetterEntry& letter : tab.tab.letters)
        {
            if (letter.selected == 0)
                continue;
            if (auto sent = Send(view,
                                 FriendSend{.kind = FriendSendKind::LetterDelete,
                                            .value = 0,
                                            .name = {},
                                            .index = letter.index,
                                            .room = 0,
                                            .window = 0,
                                            .action = 0,
                                            .subject = {},
                                            .body = {}},
                                 "0x4a11b0(12)");
                !sent.HasValue())
                return sent;
        }
        return {};
    }
    if (message.type != CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
    {
        auto opened = OpenTheWriteWindow(manager, globals, view, "0x4a11b0(1)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return PlaceTheNewWindow(manager, opened.Value(), globals, view);
    }
    case 2:
    {
        // 0x4a17b9: 0x4a08c0, la elegida.
        const LetterEntry* selected = SelectedLetter(tab);
        if (selected == nullptr)
            return {};
        return ReadTheLetter(manager, selected->index, view, "0x4a11b0(2)");
    }
    case 3:
    {
        const LetterEntry* selected = SelectedLetter(tab);
        if (selected == nullptr)
            return {};
        const std::string name = selected->name;
        const std::string subject = selected->subject;
        auto opened = OpenTheWriteWindow(manager, globals, view, "0x4a11b0(3)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        OldWindow* write = FindTheOldWindow(manager, opened.Value());
        if (write == nullptr)
            return {};
        PutTheName(manager, *write, name, globals);
        const std::array<SprintfArgument, 1> argument{SprintfArgument(std::string_view(subject))};
        const std::string reply = FormatLikeSprintf(TextOf(view, ReplyText), argument).substr(0, SubjectLimit);
        PutTheSubject(manager, *write, reply, globals);
        return PlaceTheNewWindow(manager, opened.Value(), globals, view);
    }
    case 4:
    {
        const bool anyChecked =
            std::ranges::any_of(tab.tab.letters, [](const LetterEntry& letter) { return letter.selected == 1; });
        const bool question = anyChecked;
        auto created =
            CreateFromAStep(manager,
                            OldWindowRequest{.kind = question ? ModalQuestionCase : ModalNoticeCase,
                                             .x = -1,
                                             .y = -1,
                                             .data = TextOf(view, question ? ReallyDeleteLettersText : NoneCheckedText),
                                             .aux = question ? tab.base.id : 0,
                                             .flags = 0},
                            globals, view, "0x4a11b0(4)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return PlaceTheNewWindow(manager, created.Value(), globals, view);
    }
    case 5:
        return Send(view, FriendSend{.kind = FriendSendKind::LetterList, .name = {}}, "0x4a11b0(5)");
    default:
        return {};
    }
}

void AfterTheLettersTabMove(OldWindowManager& manager, OldWindow& tab, const OldWindowGlobals& globals,
                            const OldWindowStepView& view)
{
    // 0x4a2070: con el izquierdo apretado ([0x7d36874]), sobre los encabezados (0x13 de alto): la marca (0xa de ancho)
    // marca o desmarca todas (0x4a09d0) salvo con una caja modal; cada columna (+0x130..+0x13c de la lista; la primera
    // sin la marca) ordena por su modo (0x49f870) si no es el de ahora, y copia el modelo (0x4a0960). Fuera de los
    // encabezados el botón sigue apretado.
    if (!globals.leftHeld)
        return;
    const OldList& list = tab.tab.list;
    const std::int32_t x = Steps::ClientX(tab, 0);
    const std::int32_t y = Steps::ClientY(tab, 0);
    if (Steps::MouseIn(view, x, y, LetterMarkColumn, HeaderHeight, 1))
    {
        if (manager.modal == 0)
        {
            globals.sound = ButtonSound;
            CheckAllTheLetters(manager, tab, tab.tab.allChecked == 0 ? 1 : 0);
        }
        globals.leftHeld = false;
        return;
    }
    const std::array<std::int32_t, 4> from{x + LetterMarkColumn, x + list.headers, x + list.headers + list.nameWidth,
                                           x + list.headers + list.nameWidth + list.placeWidth};
    const std::array<std::int32_t, 4> widths{list.headers - LetterMarkColumn, list.nameWidth, list.placeWidth,
                                             list.subjectWidth};
    for (std::size_t column = 0; column < from.size(); ++column)
    {
        if (!Steps::MouseIn(view, from[column], y, widths[column], HeaderHeight, 1))
            continue;
        const auto mode = static_cast<std::int32_t>(column);
        if (manager.letterModel.sortMode != mode)
        {
            globals.sound = ButtonSound;
            SortTheLetters(manager.letterModel, mode);
            RefreshTheLetters(manager, tab);
        }
        globals.leftHeld = false;
        return;
    }
}

void RefreshTheLetters(OldWindowManager& manager, OldWindow& tab)
{
    // 0x4a0960: el número de la elegida (la vtable + 0x68 y + 0x64 de la lista; −1 sin ninguna), +0x1f0 en 1, la copia
    // del modelo (0x49f500) que da el lugar de esa carta (desde 1; 0 si no está), elegida con la vtable + 0x58, la
    // vista arriba (+0x34 con 0) y 0x4a09d0(0).
    std::optional<std::uint16_t> chosen;
    if (const LetterEntry* selected = SelectedLetter(tab); selected != nullptr)
        chosen = selected->index;
    tab.tab.list.refreshed = 1;
    tab.tab.letters.clear();
    std::int32_t at = 0;
    std::int32_t found = 0;
    for (const LetterEntry& letter : manager.letterModel.letters)
    {
        tab.tab.letters.push_back(letter);
        ++at;
        if (chosen && letter.index == *chosen)
            found = at;
    }
    SelectInTheList(tab.tab.list, tab.tab.letters.size(), found);
    ScrollTheList(tab.tab.list, tab.tab.letters.size(), 0);
    CheckAllTheLetters(manager, tab, 0);
}

Core::Expected<void, std::string> ChatWithTheFriend(OldWindowManager& manager, std::string_view name,
                                                    const OldWindowStepView& view)
{
    const std::int32_t open = FindTheChatWith(manager, name, view.heroName);
    if (open == 0)
    {
        if (manager.offlineSent != 0 || IsAPendingChat(manager, name))
            return {};
        AddAPendingChat(manager, name);
        return Send(view, FriendSend{.kind = FriendSendKind::RoomRequest, .value = 0, .name = FriendName(name)},
                    "0x49d0b0(3)");
    }
    if (open == -1)
        return {};
    // 0x49dba4: la vtable + 4 con 3 (oculta) y el 1 al administrador, que la muestra y la sube.
    if (OldWindow* chat = FindTheOldWindow(manager, open); chat != nullptr)
        chat->base.state = HiddenState;
    PostToTheManager(manager, RaiseMessage, open, 0);
    return {};
}

Core::Expected<std::int32_t, std::string> WriteToTheFriend(OldWindowManager& manager, const FriendEntry* selected,
                                                           const OldWindowGlobals& globals,
                                                           const OldWindowStepView& view)
{
    auto opened = OpenTheWriteWindow(manager, globals, view, "0x49d0b0(4)");
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    const std::int32_t id = opened.Value();
    // 0x49cad0: el nombre del elegido, si hay, a la que armó (0x490a00).
    if (OldWindow* write = id != 0 ? FindTheOldWindow(manager, id) : nullptr; write != nullptr && selected != nullptr)
        PutTheName(manager, *write, FriendName(selected->name), globals);
    return id;
}
} // namespace Steps

std::int32_t FindTheChatWith(OldWindowManager& manager, std::string_view name, std::string_view hero)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        const OldWindow* chat = FindTheOldWindow(manager, id);
        // El original le pide el amigo a lo que dé 0x490a00 sin mirar: una que ya no está no queda en +0xb4 (0x4a4b30).
        if (chat == nullptr)
            continue;
        std::int32_t kind = 0;
        const std::string friendName = ChatFriend(*chat, hero, kind);
        if (kind == 2 || kind == 0)
            continue;
        if (friendName.empty())
            return -1;
        if (SameFriendName(friendName, name))
            return id;
    }
    return 0;
}

bool IsAPendingChat(const OldWindowManager& manager, std::string_view name)
{
    return std::ranges::any_of(manager.windowList.list.pending,
                               [name](const std::string& pending) { return SameFriendName(pending, name); });
}

void AddAPendingChat(OldWindowManager& manager, std::string_view name)
{
    manager.windowList.list.pending.push_back(FriendName(name));
}

void ForgetAPendingChat(OldWindowManager& manager, std::string_view name)
{
    std::deque<std::string>& pending = manager.windowList.list.pending;
    const auto found =
        std::ranges::find_if(pending, [name](const std::string& entry) { return SameFriendName(entry, name); });
    if (found != pending.end())
        pending.erase(found);
}

void AddTheChatToTheList(OldWindowManager& manager, std::int32_t id)
{
    manager.windowList.list.chats.push_back(id);
}

void MarkTheChatWaiting(OldWindowManager& manager, std::int32_t id, std::optional<std::int32_t>& sound)
{
    sound = ChatWaitingSound;
    std::deque<std::int32_t>& waiting = manager.windowList.list.waiting;
    if (std::ranges::find(waiting, id) == waiting.end())
        waiting.push_back(id);
}

std::string ChatFriend(const OldWindow& chat, std::string_view hero, std::int32_t& kind)
{
    if (chat.chat.members.size() >= 3)
    {
        kind = 2;
        return {};
    }
    for (const FriendEntry& member : chat.chat.members)
    {
        if (!SameFriendName(member.name, hero))
        {
            kind = 1;
            return FriendName(member.name);
        }
    }
    kind = 0;
    return {};
}

void AddAChatLine(OldWindow& chat, std::uint8_t speaker, std::string_view text, std::int32_t kind, std::int32_t extra)
{
    std::string name;
    if (speaker != 0xff)
    {
        for (const FriendEntry& member : chat.chat.members)
        {
            if (member.flag == speaker)
            {
                name = FriendName(member.name);
                break;
            }
        }
    }
    chat.chat.lineRows.push_back(
        ChatLine{.name = std::move(name), .text = std::string(text), .kind = kind, .extra = extra});
}

std::int32_t AddAChatMember(OldWindowManager& manager, OldWindow& chat, std::string_view name, std::uint8_t index,
                            std::uint8_t state, const OldWindowTexts& texts, std::string_view hero)
{
    std::deque<FriendEntry>& members = chat.chat.members;
    const auto found = std::ranges::find_if(members, [name](const FriendEntry& member)
                                            { return FriendName(member.name) == FriendName(name); });
    if (found != members.end())
    {
        found->flag = index;
        found->state = state;
    }
    else
        members.push_back(FriendEntry{.selected = 0, .name = FriendName(name), .flag = index, .state = state});
    chat.frame.title = ChatTitle(chat, texts, hero);
    Steps::FillTheWindowsTab(manager);
    if (members.size() >= 2)
        CloseTheChat(chat, 0, texts);
    if (members.size() >= 3)
    {
        SizeTheLines(chat, true);
        PostToTheList(manager, chat.chat.lines, chat.chat.lineRows.size(), SizeType);
    }
    return static_cast<std::int32_t>(members.size());
}

void RemoveAChatMember(OldWindowManager& manager, OldWindow& chat, std::string_view name, const OldWindowTexts& texts,
                       std::string_view hero)
{
    std::deque<FriendEntry>& members = chat.chat.members;
    if (members.size() > 2)
    {
        const auto found = std::ranges::find_if(members, [name](const FriendEntry& member)
                                                { return SameFriendName(member.name, name); });
        if (found != members.end())
            members.erase(found);
        chat.frame.title = ChatTitle(chat, texts, hero);
        Steps::FillTheWindowsTab(manager);
    }
    if (members.size() < 3)
    {
        SizeTheLines(chat, false);
        PostToTheList(manager, chat.chat.lines, chat.chat.lineRows.size(), SizeType);
    }
}

void FillTheInvites(OldWindowManager& manager, OldWindow& chat, std::string_view hero)
{
    std::deque<FriendEntry>& invitable = chat.chat.invitable;
    invitable.assign(manager.friendModel.entries.begin(), manager.friendModel.entries.end());
    const auto remove = [&invitable](std::string_view name)
    {
        const auto found = std::ranges::find_if(invitable, [name](const FriendEntry& entry)
                                                { return FriendName(entry.name) == FriendName(name); });
        if (found != invitable.end())
            invitable.erase(found);
    };
    for (const FriendEntry& member : chat.chat.members)
    {
        if (FriendName(member.name) != FriendName(hero))
            remove(member.name);
    }
    for (std::size_t at = 0; at < invitable.size();)
    {
        if (invitable[at].state < FirstOfflineFriendState)
        {
            ++at;
            continue;
        }
        invitable.erase(invitable.begin() + static_cast<std::ptrdiff_t>(at));
    }
}

void CloseTheChat(OldWindow& chat, std::int32_t closed, const OldWindowTexts& texts)
{
    const std::string prefix = texts.At(ChatClosedText);
    std::string title = chat.frame.title.value_or(std::string());
    if (closed != 1)
    {
        chat.chat.edit.base.state = 0;
        if (title.starts_with(prefix))
            chat.frame.title = title.substr(prefix.size(), 0x80);
        return;
    }
    // La vtable + 0x58 del EDIT con 1: apagado.
    chat.chat.edit.base.state = 6;
    std::string shown = title.starts_with(prefix) ? std::string() : prefix;
    shown += title.substr(0, 0x80);
    chat.frame.title = shown;
}

void RefreshTheInvites(OldWindowManager& manager, std::string_view hero)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        OldWindow* chat = FindTheOldWindow(manager, id);
        if (chat != nullptr && chat->chat.mode == 2)
            FillTheInvites(manager, *chat, hero);
    }
}

void CloseTheChats(OldWindowManager& manager, const OldWindowTexts& texts)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        OldWindow* chat = FindTheOldWindow(manager, id);
        if (chat == nullptr)
            continue;
        AddAChatLine(*chat, 0xff, texts.At(ChatLeftText), 1, 0);
        CloseTheChat(*chat, 1, texts);
    }
}

void RefreshTheLettersTab(OldWindowManager& manager)
{
    if (OldWindow* tab = TheLettersTab(manager); tab != nullptr)
        Steps::RefreshTheLetters(manager, *tab);
}

void SetTheLetter(OldWindowManager& manager, OldWindow& read, const LetterEntry& entry, std::string_view body)
{
    read.letter.entry = entry;
    // strtok(copia, "\n") (0x6c6484): los renglones vacíos no quedan; cada uno a la vtable + 0x94 de la lista.
    body = body.substr(0, body.find('\0'));
    std::size_t from = 0;
    while (from < body.size())
    {
        const std::size_t end = std::min(body.find('\n', from), body.size());
        if (end > from)
            read.letter.lineRows.emplace_back(body.substr(from, end - from));
        from = end + 1;
    }
    PostToTheList(manager, read.letter.lines, read.letter.lineRows.size(), ToTheEndType);
}

Core::Expected<void, std::string> LeaveTheChatRoom(OldWindowManager& manager, const OldWindow& chat)
{
    // 0x493510: sin la conexión de la sala no manda el C1 03 01; con ella, la salida y 0x49dfd0, que la cierra.
    if (FindTheChatRoom(manager.chatRooms, chat.chat.room) != nullptr)
        manager.chatRooms.orders.push_back(
            ChatRoomOrder{.kind = ChatRoomOrder::Kind::Leave, .room = chat.chat.room, .text = {}});
    DropTheChatRoom(manager.chatRooms, chat.chat.room);
    return {};
}

void SettleTheLetterKindsForTheDraw(OldWindowManager& manager, OldWindow& window)
{
    const std::int32_t state = window.base.state;
    if (state != DraggedWindow && state != ResizedWindow)
        return;
    if (window.kind == OldWindowKind::ChatWindow)
    {
        // 0x493fd7..0x49408d: el 8 a los renglones, a los miembros, a los de invitar, a los dos botones y al EDIT; el 9
        // a las tres listas.
        OldChatPart& chat = window.chat;
        PostToTheList(manager, chat.lines, chat.lineRows.size(), PlaceType);
        PostToTheList(manager, chat.users, chat.members.size(), PlaceType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), PlaceType);
        Steps::PostAndSettle(manager, chat.inviteButton, PlaceType);
        Steps::PostAndSettle(manager, chat.sendButton, PlaceType);
        PostToTheEdit(manager, chat.edit, PlaceType);
        if (state != ResizedWindow)
            return;
        PostToTheList(manager, chat.lines, chat.lineRows.size(), SizeType);
        PostToTheList(manager, chat.users, chat.members.size(), SizeType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), SizeType);
        return;
    }
    OldLetterPart& letter = window.letter;
    if (window.kind == OldWindowKind::LetterReadWindow)
    {
        // 0x499d94..0x499e30: el 8 a la lista del cuerpo, a los cinco botones y al retrato; el 9 a la lista y al
        // retrato.
        PostToTheList(manager, letter.lines, letter.lineRows.size(), PlaceType);
        for (OldButton& button : letter.buttons)
            Steps::PostAndSettle(manager, button, PlaceType);
        PostToTheBase(manager, letter.portrait.base, PlaceType);
        if (state != ResizedWindow)
            return;
        PostToTheList(manager, letter.lines, letter.lineRows.size(), SizeType);
        PostToTheBase(manager, letter.portrait.base, SizeType);
        return;
    }
    if (window.kind != OldWindowKind::LetterWriteWindow || letter.buttons.size() != WriteButtonTexts.size())
        return;
    // 0x4984af..0x498532: el 8 a los botones +0xa44 y +0xacc, a los tres EDIT, a los botones +0xb54 y +0xbdc y al
    // retrato (+0xb4). Sin el 9, ni cambiando de tamaño.
    Steps::PostAndSettle(manager, letter.buttons[0], PlaceType);
    Steps::PostAndSettle(manager, letter.buttons[1], PlaceType);
    PostToTheEdit(manager, letter.to, PlaceType);
    PostToTheEdit(manager, letter.subject, PlaceType);
    PostToTheEdit(manager, letter.text, PlaceType);
    Steps::PostAndSettle(manager, letter.buttons[2], PlaceType);
    Steps::PostAndSettle(manager, letter.buttons[3], PlaceType);
    PostToTheBase(manager, letter.portrait.base, PlaceType);
}

void PingTheChatRooms(OldWindowManager& manager)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        const OldWindow* chat = FindTheOldWindow(manager, id);
        if (chat == nullptr || FindTheChatRoom(manager.chatRooms, chat->chat.room) == nullptr)
            continue;
        manager.chatRooms.orders.push_back(
            ChatRoomOrder{.kind = ChatRoomOrder::Kind::Ping, .room = chat->chat.room, .text = {}});
    }
}
Core::Expected<void, std::string> SetThePortraitAngle(const OldWindowStepView* view, OldPortraitPart& portrait,
                                                      float angle, std::string_view where)
{
    if (portrait.built == 0)
        return {};
    if (auto turned = CallThePortrait(view, portrait, PortraitCallKind::SetAngle, where); !turned.HasValue())
        return turned;
    portrait.baseAngle = angle;
    portrait.angle = angle;
    return {};
}

void SetThePortraitZoom(OldPortraitPart& portrait, float zoom)
{
    portrait.baseZoom = zoom;
    portrait.zoom = zoom;
}
} // namespace Client::UI
